# Mega Drive / Genesis Controller → BLE Gamepad

Firmware that lets an ESP32 read a stock Sega Mega Drive/Genesis controller
(3-button or 6-button) and present it to your PC/phone/console as a
Bluetooth LE gamepad. Also works with Sega Master system and Atari 2600 
joystick controllers

## Parts

- Any ESP32 dev board (classic ESP32; not S2, which has no Bluetooth)
- A Sega Mega Drive/Genesis controller (or a DB9 extension cable you can
  cut and wire up)
- A DB9 (9-pin) female connector/breakout if you want it detachable,
  or just solder directly to a cut cable

## Wiring

| DB9 Pin | Signal        | ESP32 GPIO |
|--------:|---------------|:----------:|
| 1       | Up            | 32         |
| 2       | Down          | 33         |
| 3       | Left          | 25          |
| 4       | Right         | 26         |
| 5       | +5V           | **3V3**    |
| 6       | Data0 (TL)    | 27         |
| 7       | Select (TH)   | 13         |
| 8       | GND           | GND        |
| 9       | Data1 (TR)    | 14         |

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

