# Planetary Hour Clock

A clock that shows the planetary hour, the day of the week, and the old unequal
(temporal) hours of day and night, all on one printed dial that stays correct
all year. Everything that changes with the seasons is handled by *speed*, not
drawing: the hands and LEDs speed up and slow down, so sunrise is always at the
same place on the dial.

- Two mechanical hands: a temporal hour hand and a gold planetary-hour hand
- An 84-LED day ring, plus 18 LEDs on the star and compass points
- A round AMOLED in the centre showing civil time, the hour's ruling planet, and how long
  the current hour is

The design, the dial and the maths are written up in the design doc,
[The planetary hours clock: how it works](https://claude.ai/artifact/MR7kYXaauj6WffYWu9YXEm).

## Status

| Part | State |
| --- | --- |
| Centre screen | Working: clock face, setup by phone or touchscreen, settings saved in flash |
| Hands | Servo board running our firmware; both servos turn freely past one revolution. Next: link it to the screen board over ESP-NOW (plan in [`hands/README.md`](hands/README.md#link-to-the-screen-board-planned-esp-now)). |
| Dial PCB | Generated (KiCad 9), DRC clean; LED-window test coupon ready to order |
| LED board | Not started |
| Power | Not decided |

## Hardware

| Part | What | Notes |
| --- | --- | --- |
| Centre screen and main controller | [Waveshare ESP32-S3-Touch-AMOLED-1.43](https://www.waveshare.com/esp32-s3-touch-amoled-1.43.htm) | 466×466 round AMOLED, touch, PCF85063 RTC |
| Hand drive | [Waveshare Servo Driver with ESP32](https://www.waveshare.com/Servo-Driver-with-ESP32.htm) (ESP32-D0WD-V3, 4 MB flash, CP2102 USB serial) + 2× Feetech STS3215 serial bus servos, 12 V version | On the desk, running `hands/`. The screen board will send the hand angles to it. |
| Servo power | 12 V DC brick, ≥ 2 A, 5.5 × 2.1 mm centre-positive plug | The board passes its input straight to the servos, so it must match their 12 V rating. USB alone runs the ESP32 but not the servos. |
| Main battery (optional) | 3.7 V single-cell LiPo, MX1.25 2-pin plug, ≥ 500 mAh | Charged by the board at 0.5 A. Check polarity before plugging in. |
| RTC backup | Rechargeable 3 V ML1220 on the SH1.0 2-pin "RTC" header | The header trickle-charges, so never use a CR (non-rechargeable) cell. Without one, the time is lost when power is cut and the clock asks to be set up again. |

## Layout

- `firmware/`: the screen board's firmware (PlatformIO). See [`firmware/README.md`](firmware/README.md).
- `hands/`: the servo board's firmware (PlatformIO). See [`hands/README.md`](hands/README.md).
- `hardware/`: the PCBs, generated as KiCad source. See [`hardware/README.md`](hardware/README.md).
- `tools/`: scripts that generate the firmware's time-zone and city tables, and their source data.
