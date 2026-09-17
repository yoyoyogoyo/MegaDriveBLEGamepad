/*
 * Sega Mega Drive / Genesis Controller -> BLE HID Gamepad
 * for ESP32
 *
 * Reads a stock 3-button or 6-button Mega Drive/Genesis controller
 * and re-broadcasts it as a Bluetooth LE HID gamepad.
 *
 * LIBRARY REQUIRED (install via Arduino Library Manager):
 *   "ESP32 BLE Gamepad" by lemmingDev
 *   (this pulls in NimBLE-Arduino automatically as a dependency)
 *
 * WIRING (ESP32 GPIO <-> Mega Drive DB9 pin):
 *   Pin 1 (Up)     -> GPIO 32
 *   Pin 2 (Down)   -> GPIO 33
 *   Pin 3 (Left)   -> GPIO 25
 *   Pin 4 (Right)  -> GPIO 26
 *   Pin 5 (+5V)    -> ESP32 3V3  (NOT 5V! ESP32 GPIOs are not 5V
 *                                 tolerant. Genesis pads run fine on 3.3V.)
 *   Pin 6 (Data0/TL) -> GPIO 27
 *   Pin 7 (Select/TH)-> GPIO 13   (this one is an OUTPUT from the ESP32)
 *   Pin 8 (GND)    -> GND
 *   Pin 9 (Data1/TR) -> GPIO 14
 *
 * All the controller's input lines are wired with the ESP32's internal
 * pull-ups enabled. The controller is essentially open-collector: it
 * pulls a line LOW when a button/direction is active, so reads are
 * inverted in software (see readPinActive()).
 *
 * Feel free to remap the GPIOs below to whatever's convenient for your
 * board/enclosure -- just avoid strapping pins (0, 2, 12, 15) and
 * input-only pins (34-39, which can't have pull-ups and can't drive
 * SELECT anyway).
 */

#include <BleGamepad.h>

// ---- Pin assignment (change to suit your wiring) ----
static const int PIN_UP     = 32;
static const int PIN_DOWN   = 33;
static const int PIN_LEFT   = 25;
static const int PIN_RIGHT  = 26;
static const int PIN_DATA0  = 27; // DB9 pin 6
static const int PIN_SELECT = 13; // DB9 pin 7 (TH), driven by us
static const int PIN_DATA1  = 14; // DB9 pin 9

// Set true to print raw pin states + decoded buttons over Serial every
// DEBUG_INTERVAL_MS. Runs regardless of BLE connection state, so you can
// see what the ESP32 thinks the controller is doing even before pairing.
static const bool DEBUG_SERIAL = true;
static const unsigned long DEBUG_INTERVAL_MS = 200;

// Settle time between toggling SELECT and sampling the data lines.
// Real consoles use only a few microseconds; 20us gives plenty of
// margin without slowing the poll loop down noticeably.
static const int SETTLE_US = 20;

// How often to poll the pad and send a BLE HID report.
static const int POLL_INTERVAL_MS = 12; // ~80Hz

BleGamepad bleGamepad("MegaDrive Pad", "DIY", 100);

struct MDState {
  bool up = false, down = false, left = false, right = false;
  bool a = false, b = false, c = false, start = false;
  bool x = false, y = false, z = false, mode = false;
  bool sixButton = false;
};

static inline bool readPinActive(int pin) {
  return digitalRead(pin) == LOW; // controller pulls lines low when active
}

// Implements the Mega Drive controller read cycle as documented at
// https://www.raspberryfield.life/2019/03/25/sega-mega-drive-genesis-6-button-xyz-controller/
//
//   idle, TH=1 : Up, Down, Left, Right, B, C
//   pulse1 LOW : Up, Down, (grounded), (grounded), A, Start
//   pulse1 HIGH: back to idle reading
//   pulse2 LOW : same as pulse1 LOW
//   pulse2 HIGH: back to idle reading
//   pulse3 LOW : Up AND Down also forced low here on a 6-button pad
//                (this is the 6-button identification signal -- on a
//                3-button pad only Left/Right are forced low, same as
//                every other LOW state)
//   pulse3 HIGH: on a 6-button pad, Up/Down/Left/Right now carry
//                Z/Y/X/Mode instead of directions
//   pulse4 LOW : Up, Down, (grounded), (grounded), A, Start (same as before)
//   pulse4 HIGH: back to idle reading -- cycle complete
//
// Crucially, the 6-button data is read on pulse3's HIGH half, not its
// LOW half -- the LOW half is only used to detect whether a 6-button
// pad is present at all.
struct RawSnapshot {
  int up, down, left, right, data0, data1;   // idle-state raw reads
  int id_up, id_down;                        // pulse3 LOW: 6-button ID check
  int xyz_up, xyz_down, xyz_left, xyz_right;  // pulse3 HIGH: Z/Y/X/Mode data
};
RawSnapshot lastRaw;

MDState readController() {
  MDState s;

  // --- Idle state: TH = 1 ---
  digitalWrite(PIN_SELECT, HIGH);
  delayMicroseconds(SETTLE_US);
  lastRaw.up     = digitalRead(PIN_UP);
  lastRaw.down   = digitalRead(PIN_DOWN);
  lastRaw.left   = digitalRead(PIN_LEFT);
  lastRaw.right  = digitalRead(PIN_RIGHT);
  lastRaw.data0  = digitalRead(PIN_DATA0);
  lastRaw.data1  = digitalRead(PIN_DATA1);
  s.up    = lastRaw.up == LOW;
  s.down  = lastRaw.down == LOW;
  s.left  = lastRaw.left == LOW;
  s.right = lastRaw.right == LOW;
  s.b     = lastRaw.data0 == LOW;
  s.c     = lastRaw.data1 == LOW;

  // --- Pulse 1: LOW then HIGH ---
  digitalWrite(PIN_SELECT, LOW);
  delayMicroseconds(SETTLE_US);
  s.a     = readPinActive(PIN_DATA0);
  s.start = readPinActive(PIN_DATA1);
  digitalWrite(PIN_SELECT, HIGH);
  delayMicroseconds(SETTLE_US);

  // --- Pulse 2: LOW then HIGH ---
  digitalWrite(PIN_SELECT, LOW);
  delayMicroseconds(SETTLE_US);
  digitalWrite(PIN_SELECT, HIGH);
  delayMicroseconds(SETTLE_US);

  // --- Pulse 3 LOW: this is the 6-button ID check. Up AND Down both
  //     reading low here (in addition to the always-low Left/Right)
  //     means a 6-button pad is present. ---
  digitalWrite(PIN_SELECT, LOW);
  delayMicroseconds(SETTLE_US);
  lastRaw.id_up   = digitalRead(PIN_UP);
  lastRaw.id_down = digitalRead(PIN_DOWN);
  bool sixButtonID = (lastRaw.id_up == LOW && lastRaw.id_down == LOW);

  // --- Pulse 3 HIGH: on a 6-button pad, this is where Z/Y/X/Mode
  //     actually appear on the direction pins. ---
  digitalWrite(PIN_SELECT, HIGH);
  delayMicroseconds(SETTLE_US);
  lastRaw.xyz_up    = digitalRead(PIN_UP);
  lastRaw.xyz_down  = digitalRead(PIN_DOWN);
  lastRaw.xyz_left  = digitalRead(PIN_LEFT);
  lastRaw.xyz_right = digitalRead(PIN_RIGHT);

  if (sixButtonID) {
    s.sixButton = true;
    s.z    = lastRaw.xyz_up == LOW;
    s.y    = lastRaw.xyz_down == LOW;
    s.x    = lastRaw.xyz_left == LOW;
    s.mode = lastRaw.xyz_right == LOW;
  }

  // --- Pulse 4: LOW then HIGH -- resets the pad back to idle state ---
  digitalWrite(PIN_SELECT, LOW);
  delayMicroseconds(SETTLE_US);
  digitalWrite(PIN_SELECT, HIGH);
  delayMicroseconds(SETTLE_US);

  return s;
}

void setup() {
  if (DEBUG_SERIAL) {
    Serial.begin(115200);
    delay(500); // give the USB-serial monitor time to attach
    Serial.println();
    Serial.println("Mega Drive BLE Gamepad - debug mode");
    Serial.println("If Up/Down/Left/Right/Data0/Data1 all read 1 (idle)");
    Serial.println("no matter what you press, check wiring/GND/pin numbers.");
    Serial.println("If they're stuck at 0 permanently, check for a short");
    Serial.println("or a pin that isn't a valid input-with-pullup pin.");
    Serial.println();
  }

  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_LEFT, INPUT_PULLUP);
  pinMode(PIN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_DATA0, INPUT_PULLUP);
  pinMode(PIN_DATA1, INPUT_PULLUP);
  pinMode(PIN_SELECT, OUTPUT);
  digitalWrite(PIN_SELECT, HIGH);

  BleGamepadConfiguration config;
  config.setAutoReport(false);
  config.setControllerType(CONTROLLER_TYPE_GAMEPAD);
  config.setButtonCount(8);
  config.setHatSwitchCount(1);
  config.setIncludeXAxis(false);
  config.setIncludeYAxis(false);
  config.setIncludeZAxis(false);
  config.setIncludeRxAxis(false);
  config.setIncludeRyAxis(false);
  config.setIncludeRzAxis(false);
  config.setIncludeSlider1(false);
  config.setIncludeSlider2(false);

  bleGamepad.begin(&config);
}

unsigned long lastDebugPrint = 0;

void printDebug(const MDState &s, bool connected) {
  Serial.print("conn=");
  Serial.print(connected ? "Y" : "N");
  Serial.print("  RAW(up,down,left,right,d0,d1)=");
  Serial.print(lastRaw.up);   Serial.print(",");
  Serial.print(lastRaw.down); Serial.print(",");
  Serial.print(lastRaw.left); Serial.print(",");
  Serial.print(lastRaw.right);Serial.print(",");
  Serial.print(lastRaw.data0);Serial.print(",");
  Serial.print(lastRaw.data1);
  Serial.print("  (1=idle/not pressed, 0=pressed, since active-low)");

  Serial.print("  | 6btnID(up,down)=");
  Serial.print(lastRaw.id_up); Serial.print(",");
  Serial.print(lastRaw.id_down);
  Serial.print(" (both 0 => 6-button pad detected)");

  Serial.print("  | XYZ/MODE RAW(up,down,left,right)=");
  Serial.print(lastRaw.xyz_up);    Serial.print(",");
  Serial.print(lastRaw.xyz_down);  Serial.print(",");
  Serial.print(lastRaw.xyz_left);  Serial.print(",");
  Serial.print(lastRaw.xyz_right);

  Serial.print("  | btn: ");
  if (s.up) Serial.print("UP ");
  if (s.down) Serial.print("DOWN ");
  if (s.left) Serial.print("LEFT ");
  if (s.right) Serial.print("RIGHT ");
  if (s.a) Serial.print("A ");
  if (s.b) Serial.print("B ");
  if (s.c) Serial.print("C ");
  if (s.start) Serial.print("START ");
  if (s.x) Serial.print("X ");
  if (s.y) Serial.print("Y ");
  if (s.z) Serial.print("Z ");
  if (s.mode) Serial.print("MODE ");

  Serial.print(" | padType=");
  Serial.println(s.sixButton ? "6-button" : "3-button (or nothing pressed extra)");
}

void loop() {
  MDState s = readController(); // always read, so debug works even unpaired
  bool connected = bleGamepad.isConnected();

  if (connected) {
    // D-pad -> hat switch
    uint8_t hat = DPAD_CENTERED;
    if (s.up && s.right) hat = DPAD_UP_RIGHT;
    else if (s.down && s.right) hat = DPAD_DOWN_RIGHT;
    else if (s.down && s.left) hat = DPAD_DOWN_LEFT;
    else if (s.up && s.left) hat = DPAD_UP_LEFT;
    else if (s.up) hat = DPAD_UP;
    else if (s.down) hat = DPAD_DOWN;
    else if (s.left) hat = DPAD_LEFT;
    else if (s.right) hat = DPAD_RIGHT;
    bleGamepad.setHat1(hat);

    // Buttons 1-8: A, B, C, Start, X, Y, Z, Mode
    // (X/Y/Z/Mode simply stay unpressed if it's a 3-button pad)
    s.a     ? bleGamepad.press(BUTTON_1) : bleGamepad.release(BUTTON_1);
    s.b     ? bleGamepad.press(BUTTON_2) : bleGamepad.release(BUTTON_2);
    s.c     ? bleGamepad.press(BUTTON_3) : bleGamepad.release(BUTTON_3);
    s.start ? bleGamepad.press(BUTTON_4) : bleGamepad.release(BUTTON_4);
    s.x     ? bleGamepad.press(BUTTON_5) : bleGamepad.release(BUTTON_5);
    s.y     ? bleGamepad.press(BUTTON_6) : bleGamepad.release(BUTTON_6);
    s.z     ? bleGamepad.press(BUTTON_7) : bleGamepad.release(BUTTON_7);
    s.mode  ? bleGamepad.press(BUTTON_8) : bleGamepad.release(BUTTON_8);

    bleGamepad.sendReport();
  }

  if (DEBUG_SERIAL) {
    unsigned long now = millis();
    if (now - lastDebugPrint >= DEBUG_INTERVAL_MS) {
      lastDebugPrint = now;
      printDebug(s, connected);
    }
  }

  delay(POLL_INTERVAL_MS);
}
