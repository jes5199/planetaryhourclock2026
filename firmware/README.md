# Firmware

Firmware for the centre screen, a Waveshare ESP32-S3-Touch-AMOLED-1.43 (ESP32-S3R8, 466×466
round AMOLED, FT3168 touch, PCF85063 RTC). Built with PlatformIO, Arduino-ESP32 3.3 and LVGL 8.3.

## What it does

**Time maths.** Sunrise and sunset are computed on the board (NOAA equations) for the clock's
location. From those come the temporal hour, the ruling planet of the hour, day and night, and
every dial angle from the design doc: gold hand, hour hand, day-ring LED, and the screen's arc.

**Clock face** (top to bottom):

- Civil time, 96 px
- The hour's ruling planet, in that planet's colour
- The hour's start and end, its length in minutes, and a countdown to the next hour, in the
  planet's colour dimmed
- Around the edge, the hour arc: amber by day (centred at the top), indigo by night (centred
  at the bottom). A sun (or pale moon) rides it, showing how far through the day or night it
  is; a thin needle with a dot sweeps it once per planetary hour.

All numerals are fixed-width. The text drifts a few pixels each minute against burn-in, and
the screen dims at night. The screen is rotated 90° clockwise to suit how the board is mounted
(`screenRotation` in settings).

Planet colours come from the "Planet Colors" palette of
[dirigible](https://github.com/jes5199/dirigible) (`app/assets/stylesheets/planets.css`): Sun
`#FFD700`, Moon `#FFFFFF`, Mercury `#87CEEB`, Venus `#90EE90`, Mars `#FF6347`, Jupiter
`#9370DB`, Saturn `#A9A9A9` (dirigible's lifted grey for dark backgrounds).

**Serial log.** The full clock state is printed once a minute at 115200 baud.

## Setting up a clock

Long-press the clock face for 1.5 s. A new clock, or one whose RTC lost the time, opens setup
by itself and shows `--:--` / `SET TIME` until the time is set.

The setup screen shows a Wi-Fi QR code and two buttons. Either route works on its own:

- **With a phone.** Scan the QR code with the camera. The phone joins the clock's own hotspot
  (WPA2, password unique to each clock and saved in flash) and the setup page opens by
  itself; if not, browse to `http://192.168.4.1`. One button sets the time and time zone from
  the phone. Search for the nearest city, or type coordinates.
- **On the clock.** **Time** sets the hour, minute and date with scroll wheels. **Place**
  picks a region, then a time zone, then the nearest city.

Long-press the setup screen, tap Done on the phone, or wait 10 minutes to go back to the clock.
The Wi-Fi radio is off whenever setup is closed.

Phone GPS isn't used: browsers only allow it on HTTPS pages, and the clock's local page can't be
HTTPS. The nearest city is close enough: 50 km moves sunrise by about 2 minutes.

## Time keeping

- The PCF85063 RTC keeps UTC. It only keeps time while it has power: from USB, the LiPo, or a
  backup cell on the RTC header (see the main README).
- At boot, an RTC time is trusted only if it is later than both the firmware's build time and
  the last known-good time saved in flash (saved every 10 minutes and at every time set).
  Otherwise the clock runs but asks to be set up.
- Optional NTP: copy `src/secrets.example.h` to `src/secrets.h` (git-ignored) with your Wi-Fi
  details, and the clock syncs and corrects the RTC.

## Build

PlatformIO needs Python 3.10–3.14. Homebrew's `platformio` runs on a newer Python that the
ESP32 platform rejects, so install it in a virtualenv:

```sh
python3 -m venv ~/.platformio/penv && ~/.platformio/penv/bin/pip install platformio
cd firmware
~/.platformio/penv/bin/pio test -e native            # host tests for the time maths
~/.platformio/penv/bin/pio run -e amoled143 -t upload
~/.platformio/penv/bin/pio device monitor
```

`pio run -e preview -t upload` builds a design preview that forces the ruling planet (set
`PREVIEW_RULER` in `platformio.ini`: Saturn = 0 … Moon = 6), to check each planet's colours.

## Layout

| Path | What |
| --- | --- |
| `lib/planetary/` | Pure C++ maths: sunrise and sunset, and the clock state. No Arduino; tested on the host. |
| `lib/amoled/` | Display bring-up (QSPI; SH8601 or CO5300 panel detected at boot) and rotation, adapted from Waveshare's examples. |
| `src/main.cpp` | Startup, time sources, the main loop. |
| `src/ui.cpp` | The clock face. |
| `src/tabular_font.cpp` | Fixed-width digits for any LVGL font. |
| `src/fonts/` | The 96 px time font (Montserrat Medium, made with `lv_font_conv`). |
| `src/setup_mode.cpp` | Hotspot, captive portal and web API; shared apply functions. |
| `src/setup_page.h` | The phone setup page (HTML). |
| `src/setup_screens.cpp` | On-clock setup: QR home, Time and Place screens. |
| `src/settings.cpp` | Settings saved in flash (location, place, time zone, brightness, rotation). |
| `src/time_status.cpp` | Whether the time is trustworthy; the known-good time in flash. |
| `src/tz.cpp`, `src/tz_table.inc` | IANA → POSIX time-zone lookup. |
| `src/cities.cpp`, `src/city_table.inc` | City search and per-zone city lists. |
| `src/rtc.cpp`, `src/touch.cpp` | PCF85063 RTC and FT3168 touch, on the shared I2C bus (SDA 47, SCL 48). |
| `test/` | Unity tests, run with `pio test -e native`. |

### Setup web API

| Request | Does |
| --- | --- |
| `GET /api/state` | Current time, zone, place and coordinates as JSON |
| `GET /api/cities?q=<text>&tz=<zone>` | Up to 8 cities matching the text, the phone's zone first; with no `q`, the zone's largest city |
| `GET /set?time=<unix ms>&tz=<IANA>&off=<min east>&lat=&lon=&place=` | Sets any subset; `&back=1` redirects to the page instead of replying `ok` |
| `GET /done` | Closes setup |

## Data and code from others

- City list: [GeoNames](https://www.geonames.org) cities over 15,000 people, CC BY 4.0.
  Regenerate with `python3 tools/gen_city_table.py`.
- Time zones: [posix_tz_db](https://github.com/nayarsystems/posix_tz_db), MIT
  (`tools/data/posix_tz_db/`). Regenerate with `python3 tools/gen_tz_table.py`.
- Display driver files in `lib/amoled/src/` (`esp_lcd_sh8601.*`, `read_lcd_id_bsp.*`) from
  [waveshareteam/ESP32-S3-Touch-AMOLED-1.43](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43),
  Apache-2.0.
- Fonts: Montserrat (SIL Open Font License), as shipped with LVGL.

## Notes on the maths

The design doc's formulas assume solar time (sunrise and sunset symmetric about noon). The
firmware uses the real sunrise and sunset: day hours divide sunrise to sunset into 12, and night
hours divide sunset to the next sunrise into 12. The day ring's civil day changes at temporal
midnight (h = 18, the bottom of the dial), so it always lines up with the printed 3-6-3 split.

All dial angles are clockwise from the top, with sunrise at the left, noon at the top, sunset at
the right and midnight at the bottom. The screen's arc uses the same convention.
