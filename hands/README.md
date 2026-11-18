# Hand drive firmware

Runs on the Waveshare [Servo Driver with ESP32](https://www.waveshare.com/Servo-Driver-with-ESP32.htm)
(an original ESP32, 4 MB flash, CP2102 USB serial) and drives the clock's two hands with Feetech
STS3215 serial bus servos (12 V version).

Status: a serial console for finding and exercising servos. Taking the hand angles from the
screen board comes next.

## Power

The servos run from the board's 5.5 × 2.1 mm barrel jack: 6–12 V, matched to the servos, ≥ 2 A.
The board passes this voltage straight to the servo port. USB alone runs the ESP32, so a scan
finds nothing without the barrel jack. A 10 V brick works with the 12 V servos (they read ~9.9 V
and accept 4–14 V), with a little less torque than at 12 V.

## Build and flash

```sh
cd hands
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbserial-0001
```

Uploads run at 115200 baud: faster transfers came back corrupted on this board.

## Serial console (115200 baud)

The command list prints at boot, followed by a scan of the servo bus.

| Command | Does |
| --- | --- |
| `help` | List the commands |
| `scan` | Ping every servo ID (0–253) and list the ones that answer |
| `info <id>` | One line of JSON: position, speed, load, current, voltage, temperature, mode, torque, limits |
| `move <id> <pos> [speed]` | Go to a position, 0–4095 per turn, at a speed in steps/s (default 1000) |
| `torque <id> on\|off` | Hold position, or go limp so the servo can be turned by hand |
| `watch <id> [seconds]` | Print the position ten times a second |
| `mode <id> 0\|1\|3` | 0 position (one turn), 1 wheel, 3 step (relative moves). Saved in the servo. |
| `setid <id> <new>` | Give a servo a new ID (1–253). Saved in the servo. |
| `limits <id> <min> <max>` | Angle limits, 0–4095 (factory 0 4095); `0 0` removes them. Saved in the servo. |
| `turn <id> <turns> [speed]` | Step mode only: rotate by whole or fractional turns, + or −, up to about 8 |

## Servo bus

The servo port is on GPIO 18 (RX) and 19 (TX) at 1 Mbaud, half-duplex. `lib/sts` speaks the
Feetech STS packet protocol: `FF FF id len instr params… checksum`, little-endian registers. New
STS3215s all ship as ID 1, so each servo was connected alone and given its own ID:

| ID | Servo | Settings |
| --- | --- | --- |
| 1 | first servo | step mode, no angle limits |
| 2 | second servo (was ID 1; changed with `setid 1 2`) | step mode, no angle limits |

## Going round more than once

A clock hand never stops, but in position mode (mode 0) a servo has one turn of travel, 0–4095:
from 4095 to 0 it swings all the way back. What works on the STS3215 (tested on servo 1):

- `mode 1 3` (step mode: the goal position is a relative move, ±32767 steps, about 8 turns)
- `limits 1 0 0` (with the factory 0–4095 limits still set, step moves are clamped to one turn)
- `turn 1 2 1500` then turned exactly two turns at ~1525 steps/s and ended where it started.

With the limits off, the position register reads past 4095: during that move it counted down
from about 8196 to 4.

Mode and limits live in the servo's EEPROM, which is locked against writes (register 55). Saving
makes the servo slow to reply, so EEPROM writes wait 30 ms, retry, and always re-lock; `info`
shows `eeprom_locked`.

## Stock firmware

Waveshare's demo firmware was backed up before this was flashed (see the top-level `CLAUDE.md`
for where it is and how to restore it).
