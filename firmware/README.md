# Firmware

Centre-screen firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.43 (ESP32-S3R8,
466×466 round AMOLED, PCF85063 RTC), built with PlatformIO, Arduino-ESP32 3.3 and LVGL 8.3.

## What it does now

- Computes sunrise and sunset on the board (NOAA equations) for the configured location
  (default: Portland, Oregon, in `src/settings.h`).
- Computes the temporal hour, the planetary hour's ruler, the day and night rulers, and every
  dial angle from the design doc: gold hand, hour hand, day-ring LED, screen arc and its hands.
- Draws the centre screen: civil time, the hour's ruler (amber by day, pale blue by night),
  the hour's start, length and end, and the hour arc with its day hand and hour hand.
- Dims at night and drifts the text a few pixels each minute against burn-in.
- Logs the full clock state over serial once a minute.

Not yet: the mechanical hands, the 102-LED chain, touch, the settings menu, the web app and Bluetooth.

## Time

At boot, time comes from the PCF85063 RTC. If the RTC has never been set, the firmware's
build time is used instead. To sync over Wi-Fi with NTP (which also sets the RTC), copy
`src/secrets.example.h` to `src/secrets.h` and fill in your network. `secrets.h` is git-ignored.

## Build

```sh
brew install platformio
cd firmware
pio test -e native            # host tests for the time maths
pio run -e amoled143 -t upload
pio device monitor
```

## Layout

| Path | What |
| --- | --- |
| `lib/planetary/` | Pure C++ maths: sunrise/sunset and the clock state. No Arduino, tested on the host. |
| `lib/amoled/` | Display bring-up (QSPI, SH8601 or CO5300 panel detected at boot), adapted from Waveshare's examples. |
| `src/ui.cpp` | The LVGL clock face. |
| `src/rtc.cpp` | PCF85063 RTC (stores UTC). |
| `src/settings.h` | Location, time zone, brightness. |
| `test/` | Unity tests run with `pio test -e native`. |

The driver files in `lib/amoled/src/` (`esp_lcd_sh8601.*`, `read_lcd_id_bsp.*`) come from
[waveshareteam/ESP32-S3-Touch-AMOLED-1.43](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43)
under the Apache-2.0 license.

## Notes on the maths

The design doc's formulas assume solar time (sunrise and sunset symmetric about noon). The
firmware uses the real sunrise and sunset: day hours divide sunrise to sunset into 12, and night
hours divide sunset to the next sunrise into 12. The day ring's civil day changes at temporal
midnight (h = 18, the bottom of the dial), so it always lines up with the printed 3-6-3 split.
