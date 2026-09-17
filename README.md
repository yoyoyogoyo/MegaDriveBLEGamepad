# Mega Drive / Genesis Controller → BLE Gamepad

Firmware that lets an ESP32 read a stock Sega Mega Drive/Genesis controller
(3-button or 6-button) and present it to your PC/phone/console as a
Bluetooth LE gamepad.

## Parts

- Any ESP32 dev board (classic ESP32; not S2, which has no Bluetooth)
- A Sega Mega Drive/Genesis controller (or a DB9 extension cable you can
  cut and wire up)
- A DB9 (9-pin) female connector/breakout if you want it detachable,
  or just solder directly to a cut cable

## Wiring

| DB9 Pin | Signal        | ESP32 GPIO |
|--------:|---------------|:----------:|
| 1       | Up            | 16         |
| 2       | Down          | 17         |
| 3       | Left          | 5          |
| 4       | Right         | 18         |
| 5       | +5V           | **3V3**    |
| 6       | Data0 (TL)    | 19         |
| 7       | Select (TH)   | 21         |
| 8       | GND           | GND        |
| 9       | Data1 (TR)    | 22         |

**Important:** power the controller from the ESP32's 3.3V pin, not 5V.
ESP32 GPIOs are not 5V tolerant, and Mega Drive pads work fine at 3.3V
(they're simple CMOS logic / switches internally).

DB9 pin numbering, viewed from the *front* of the female port on the
controller's plug (i.e. the pins you'd solder to on a breakout are
mirrored) — top row left-to-right is 1,2,3,4,5, bottom row is 6,7,8,9.
Double check with a multimeter/continuity tester against your specific
connector before powering it up.

Pins are configured as inputs with internal pull-ups; the controller
pulls a line low when a button or direction is pressed.

## Software setup

1. Install the Arduino IDE and the ESP32 board package.
2. In Library Manager, install **"ESP32 BLE Gamepad"** by lemmingDev.
   It will pull in NimBLE-Arduino as a dependency automatically.
3. Open `MegaDriveBLEGamepad.ino`, select your ESP32 board, and flash it.
4. Power the ESP32, then pair "MegaDrive Pad" from your device's
   Bluetooth settings like any other BLE gamepad.

## How the 6-button detection works

Standard Mega Drive controllers multiplex their buttons over a handful
of pins using the SELECT (TH) line:

- `TH=1`: pins give Up, Down, Left, Right, B, C
- `TH=0`: pins give Up, Down, (grounded), (grounded), A, Start

Six-button pads respond to a longer sequence of TH toggles, and on the
final low pulse they drive the direction pins with Z, Y, X, Mode instead
of leaving them grounded. The firmware always runs this full sequence
each poll (~80 times/second) and checks whether those extra buttons show
up — if they don't, it just behaves as a 3-button pad with A/B/C/Start,
which works for both types of controller with no configuration needed.

## Notes / things you may want to tweak

- **GPIO choice**: the pins above avoid ESP32 strapping pins and
  input-only pins, but feel free to remap them (just keep SELECT on a
  regular output-capable GPIO).
- **Poll rate**: `POLL_INTERVAL_MS` in the sketch controls how often a
  report is sent (default ~80Hz); lower it if you want less input lag,
  at the cost of a bit more power draw / BLE traffic.
- **Button/D-pad mapping**: the mapping to `BUTTON_1..8` and the hat
  switch is arbitrary — remap it in `loop()` to match what your target
  platform expects (e.g. swap A/B/C to match a standard 3-button
  layout some emulators expect).
- **Two controllers**: to support two pads at once, just duplicate the
  pin set and `MDState` read, and expand `BleGamepadConfiguration` (or
  run two separate BLE gamepad instances if your target supports that).
