// Hand drive for the planetary hour clock, on the Waveshare Servo Driver with ESP32.
//
// For now: a serial console for finding and exercising the servos (115200 baud).
// Later: take hand angles from the screen board and drive the two hands.

#include <Arduino.h>

#include "sts.h"

namespace {

constexpr int kServoRx = 18;  // the board's serial bus servo port
constexpr int kServoTx = 19;

sts::Bus bus;

const char kHelp[] =
    "\n"
    "Planetary hour clock: hand drive (Waveshare Servo Driver with ESP32)\n"
    "Serial bus servos need the 12 V barrel jack powered; USB only runs the ESP32.\n"
    "Commands (one per line; replies start with ok: or error:):\n"
    "  help                      this text\n"
    "  scan                      ping every servo ID (0-253) and list the ones that answer\n"
    "  info <id>                 one line of JSON: position, voltage, temperature, load, limits\n"
    "  move <id> <pos> [speed]   go to position 0-4095 (4096 per turn), speed in steps/s (default 1000)\n"
    "  torque <id> on|off        hold position, or go limp so the hand can be turned by hand\n"
    "  watch <id> [seconds]      print the position 10 times a second (default 5 s)\n"
    "  mode <id> 0|1|3           0 position (one turn, 0-4095), 1 wheel, 3 step (relative moves);\n"
    "                            saved in the servo\n"
    "  setid <id> <new>          give a servo a new ID (1-253); saved in the servo. New STS3215s are\n"
    "                            all ID 1, so connect one at a time\n"
    "  limits <id> <min> <max>   angle limits, 0-4095 (factory 0 4095); 0 0 removes them, which\n"
    "                            step mode needs to go past one turn; saved in the servo\n"
    "  turn <id> <turns> [speed] step mode only: rotate by whole or fractional turns (+ or -, up to\n"
    "                            about 8), speed in steps/s (default 1000)\n";

void reply(bool ok, const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.printf("%s %s\n", ok ? "ok:" : "error:", buf);
}

void cmdScan() {
  int found = 0;
  Serial.print("scanning:");
  for (int id = 0; id < sts::kBroadcast; ++id) {
    if (bus.ping(id)) {
      Serial.printf(" %d", id);
      ++found;
    }
  }
  Serial.println();
  reply(found > 0, "%d servo%s found%s", found, found == 1 ? "" : "s",
        found ? "" : " (is the 12 V supply plugged in?)");
}

void cmdInfo(int id) {
  uint16_t model, pos, speed, load, minA, maxA, current;
  uint8_t volt, temp, mode, torque, moving, minV, maxV, lock;
  if (!bus.readWord(id, sts::kModel, &model)) return reply(false, "servo %d did not answer", id);
  const bool ok = bus.readWord(id, sts::kPosition, &pos) && bus.readWord(id, sts::kSpeed, &speed) &&
                  bus.readWord(id, sts::kLoad, &load) && bus.readByte(id, sts::kVoltage, &volt) &&
                  bus.readByte(id, sts::kTemperature, &temp) && bus.readByte(id, sts::kMoving, &moving) &&
                  bus.readWord(id, sts::kCurrent, &current) && bus.readByte(id, sts::kMode, &mode) &&
                  bus.readByte(id, sts::kTorqueEnable, &torque) &&
                  bus.readWord(id, sts::kMinAngle, &minA) && bus.readWord(id, sts::kMaxAngle, &maxA) &&
                  bus.readByte(id, sts::kMinVoltage, &minV) && bus.readByte(id, sts::kMaxVoltage, &maxV) && bus.readByte(id, sts::kLock, &lock);
  if (!ok) return reply(false, "servo %d stopped answering", id);
  Serial.printf(
      "{\"id\":%d,\"model\":%u,\"position\":%d,\"speed\":%d,\"load_pct\":%.1f,\"current_ma\":%.0f,"
      "\"voltage\":%.1f,\"temperature_c\":%u,\"moving\":%u,\"mode\":%u,\"torque\":%u,"
      "\"angle_limits\":[%u,%u],\"voltage_limits\":[%.1f,%.1f],\"eeprom_locked\":%u,\"error\":%u}\n",
      id, model, sts::signedPosition(pos), sts::signedSpeed(speed), sts::signedLoad(load) / 10.0, current * 6.5, volt / 10.0,
      temp, moving, mode, torque, minA, maxA, minV / 10.0, maxV / 10.0, lock, bus.lastError());
  reply(true, "info %d", id);
}

void cmdMove(int id, int pos, int speed) {
  if (pos < 0 || pos >= sts::kStepsPerTurn) return reply(false, "position must be 0-4095");
  if (!bus.moveTo(id, pos, speed)) return reply(false, "servo %d did not answer", id);
  reply(true, "servo %d moving to %d at %d steps/s", id, pos, speed);
}

void cmdTorque(int id, bool on) {
  if (!bus.writeByte(id, sts::kTorqueEnable, on ? 1 : 0)) return reply(false, "servo %d did not answer", id);
  reply(true, "servo %d torque %s", id, on ? "on" : "off");
}

// Write to the servo's EEPROM area, which is locked against writes by default.
// Saving to EEPROM makes the servo slow to reply, so these writes wait longer and retry; the
// lock goes back on even if the write itself failed.
bool writeEeprom(int id, uint8_t addr, const uint8_t* data, uint8_t len) {
  bus.setTimeoutUs(30000);
  auto retry = [](auto f) { for (int i = 0; i < 3; ++i) if (f()) return true; return false; };
  bool ok = retry([&] { return bus.writeByte(id, sts::kLock, 0); }) &&
            retry([&] { return bus.write(id, addr, data, len); });
  ok = retry([&] { return bus.writeByte(id, sts::kLock, 1); }) && ok;
  bus.setTimeoutUs(3000);
  return ok;
}

void cmdMode(int id, int mode) {
  if (mode != 0 && mode != 1 && mode != 3) return reply(false, "mode must be 0, 1 or 3");
  const uint8_t m = mode;
  if (!writeEeprom(id, sts::kMode, &m, 1)) return reply(false, "servo %d did not answer", id);
  reply(true, "servo %d in mode %d", id, mode);
}

void cmdSetId(int id, int newId) {
  if (newId < 1 || newId > 253) return reply(false, "new ID must be 1-253");
  if (bus.ping(newId)) return reply(false, "ID %d is already taken on this bus", newId);
  if (!bus.ping(id)) return reply(false, "servo %d did not answer", id);
  // After the ID write the servo answers to its new ID, so the lock goes back on there.
  bus.setTimeoutUs(30000);
  auto retry = [](auto f) { for (int i = 0; i < 3; ++i) if (f()) return true; return false; };
  const bool unlocked = retry([&] { return bus.writeByte(id, sts::kLock, 0); });
  if (unlocked) bus.writeByte(id, sts::kId, newId);  // the reply may come from either ID; check below
  delay(50);
  const bool moved = bus.ping(newId);
  const bool locked = retry([&] { return bus.writeByte(moved ? newId : id, sts::kLock, 1); });
  bus.setTimeoutUs(3000);
  if (!moved) return reply(false, "servo %d did not take the new ID", id);
  reply(locked, "servo %d is now ID %d%s", id, newId, locked ? "" : " (but its EEPROM did not re-lock)");
}

void cmdLimits(int id, int lo, int hi) {
  if (lo < 0 || hi > 4095 || lo > hi) return reply(false, "limits must be 0 <= min <= max <= 4095");
  const uint8_t b[4] = {uint8_t(lo & 0xFF), uint8_t(lo >> 8), uint8_t(hi & 0xFF), uint8_t(hi >> 8)};
  if (!writeEeprom(id, sts::kMinAngle, b, sizeof b)) return reply(false, "servo %d did not answer", id);
  reply(true, "servo %d angle limits %d-%d%s", id, lo, hi, lo == 0 && hi == 0 ? " (none)" : "");
}

void cmdTurn(int id, float turns, int speed) {
  uint8_t mode;
  if (!bus.readByte(id, sts::kMode, &mode)) return reply(false, "servo %d did not answer", id);
  if (mode != 3) return reply(false, "servo %d is in mode %u; send 'mode %d 3' first", id, mode, id);
  const long steps = lroundf(turns * sts::kStepsPerTurn);
  if (steps < -32767 || steps > 32767) return reply(false, "at most about 8 turns at once");
  if (!bus.moveTo(id, static_cast<int16_t>(steps), speed)) return reply(false, "servo %d did not answer", id);
  reply(true, "servo %d turning %ld steps at %d steps/s", id, steps, speed);
}

void cmdWatch(int id, int seconds) {
  const uint32_t end = millis() + seconds * 1000u;
  while (millis() < end) {
    uint16_t pos;
    if (bus.readWord(id, sts::kPosition, &pos))
      Serial.printf("position %d (%.1f deg)\n", sts::signedPosition(pos), sts::signedPosition(pos) * 360.0 / sts::kStepsPerTurn);
    else
      Serial.println("no answer");
    delay(100);
  }
  reply(true, "watch done");
}

void handle(char* line) {
  char* argv[5] = {};
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 5; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
  if (argc == 0) return;
  const char* cmd = argv[0];
  const int a1 = argc > 1 ? atoi(argv[1]) : -1;
  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    Serial.print(kHelp);
  } else if (!strcmp(cmd, "scan")) {
    cmdScan();
  } else if (!strcmp(cmd, "info") && argc == 2) {
    cmdInfo(a1);
  } else if (!strcmp(cmd, "move") && argc >= 3) {
    cmdMove(a1, atoi(argv[2]), argc > 3 ? atoi(argv[3]) : 1000);
  } else if (!strcmp(cmd, "torque") && argc == 3) {
    cmdTorque(a1, !strcmp(argv[2], "on"));
  } else if (!strcmp(cmd, "mode") && argc == 3) {
    cmdMode(a1, atoi(argv[2]));
  } else if (!strcmp(cmd, "setid") && argc == 3) {
    cmdSetId(a1, atoi(argv[2]));
  } else if (!strcmp(cmd, "limits") && argc == 4) {
    cmdLimits(a1, atoi(argv[2]), atoi(argv[3]));
  } else if (!strcmp(cmd, "turn") && argc >= 3) {
    cmdTurn(a1, atof(argv[2]), argc > 3 ? atoi(argv[3]) : 1000);
  } else if (!strcmp(cmd, "watch") && argc >= 2) {
    cmdWatch(a1, argc > 2 ? atoi(argv[2]) : 5);
  } else {
    reply(false, "unknown command '%s'; send 'help'", cmd);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  bus.begin(Serial2, kServoRx, kServoTx);
  delay(200);
  Serial.print(kHelp);
  cmdScan();
}

void loop() {
  static char line[96];
  static size_t n = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      line[n] = '\0';
      if (n) handle(line);
      n = 0;
    } else if (n < sizeof line - 1) {
      line[n++] = c;
    }
  }
}
