# Planetary Hour Clock: notes for Claude

A clock showing planetary hours and unequal (temporal) hours. Design doc:
https://claude.ai/artifact/MR7kYXaauj6WffYWu9YXEm (a Claude Docs doc; read it with the docs tools).
The firmware README (`firmware/README.md`) is the detailed reference; keep it and the top-level
README updated in the same commit as any feature change. Commit and push after each working step.

## Hardware on the desk

- Centre screen and main controller: Waveshare ESP32-S3-Touch-AMOLED-1.43 over USB-C. On macOS
  it appears as `/dev/cu.usbmodem*` (usually `/dev/cu.usbmodem1101`). Find it with `ls /dev/cu.usbmodem*`.
- Planned, not yet connected: Waveshare ESP32 Servo Driver board with two Feetech STS3215 servos
  for the hands; an LED ring.

## Build, test, flash

Use PlatformIO from its Python 3.13 virtualenv. Homebrew's `platformio` runs on a Python the
ESP32 platform rejects.

```sh
cd firmware
~/.platformio/penv/bin/pio test -e native          # host tests for the time maths (lib/planetary)
~/.platformio/penv/bin/pio run -e amoled143         # build
~/.platformio/penv/bin/pio run -e amoled143 -t upload --upload-port /dev/cu.usbmodem1101
```

A build can quietly swap PlatformIO Core for a `6.2.0.post1` that then refuses the platform
("IncompatiblePlatform"). Fix: `~/.platformio/penv/bin/pip install --force-reinstall --no-deps platformio==6.2.0`.

Other envs: `preview` forces the ruling planet (`PREVIEW_RULER` in `platformio.ini`) to check
colours; `bench` times screen redraws at boot.

## Talking to the clock over serial

115200 baud. The clock prints its own command list at boot, on USB connect, and on `help`.
Replies start with `ok:` or `error:`; `status` prints one line of JSON. Read the port with
pyserial from the venv (no interactive terminal needed), e.g.:

```sh
~/.platformio/penv/bin/python - <<'EOF'
import serial, time
s = serial.Serial('/dev/cu.usbmodem1101', 115200, timeout=0.5)
time.sleep(1); s.read(8000)                 # skip the help text printed on connect
for c in ["status", f"time {int(time.time())}", "status"]:
    s.write((c + "\n").encode()); time.sleep(1)
    print(s.read(8000).decode(errors="replace"))
EOF
```

Useful commands: `time <unix seconds>`, `tz <IANA zone>`, `city <text>` then `city <n>`,
`loc <lat> <lon> [name]`, `setup open|close`, `ble pair|forget`, `reboot`.

Only one program can hold the port: close any monitor before flashing (`pkill -f cu.usbmodem`).
Opening the port with DTR/RTS toggled resets the board; the plain open above does not.

A minute-by-minute state line is printed too (`h=… ruler=… gold=… hand=… led=…`). Occasional
`i2cWriteReadNonStop ... ESP_ERR_INVALID_STATE` lines are known: a skipped poll of the touch
controller, harmless.

## Things learned the hard way

- Never let serial output block: `Serial.setTxTimeoutMs(0)` is set because USB CDC writes with
  no host reading stalled the UI. Keep the 4 KB TX buffer so long replies aren't dropped.
- LVGL 8 coordinates top out at 8191 px: a roller (scroll wheel) taller than that garbles. Infinite
  rollers repeat their options 7 times, so keep long lists in normal mode and split huge lists.
- Roller line spacing must be set on both the main part and `LV_PART_SELECTED`.
- Screen rotation is done in the flush callback (`lib/amoled`), not LVGL's `sw_rotate`, which sent
  odd-sized strips the panel can't take. Touch points go through `amoled_panel_to_screen`.
- Internal RAM is tight with Wi-Fi + Bluetooth: the LVGL pool lives in PSRAM; the three DMA draw
  buffers are 38 lines each. Check the `memory:` line at boot after adding anything big.
- The clock's fonts are ASCII-only; place names shown on the clock come from the generated
  tables' ASCII display names (`tools/gen_city_table.py`).
- The screen is ~36 mm across (about 13 px/mm): touch targets need to be ~70 px or more.
