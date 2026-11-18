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

## Servo bus

The servo port is on GPIO 18 (RX) and 19 (TX) at 1 Mbaud, half-duplex. `lib/sts` speaks the
Feetech STS packet protocol: `FF FF id len instr params… checksum`, little-endian registers. New
STS3215s all ship as ID 1, so the second servo has to be given a new ID before both share the bus.

## Stock firmware

Waveshare's demo firmware was backed up before this was flashed (see the top-level `CLAUDE.md`
for where it is and how to restore it).
