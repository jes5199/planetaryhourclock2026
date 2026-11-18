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
    "  watch <id> [seconds]      print the position 10 times a second (default 5 s)\n";

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
  uint8_t volt, temp, mode, torque, moving, minV, maxV;
  if (!bus.readWord(id, sts::kModel, &model)) return reply(false, "servo %d did not answer", id);
  const bool ok = bus.readWord(id, sts::kPosition, &pos) && bus.readWord(id, sts::kSpeed, &speed) &&
                  bus.readWord(id, sts::kLoad, &load) && bus.readByte(id, sts::kVoltage, &volt) &&
                  bus.readByte(id, sts::kTemperature, &temp) && bus.readByte(id, sts::kMoving, &moving) &&
                  bus.readWord(id, sts::kCurrent, &current) && bus.readByte(id, sts::kMode, &mode) &&
                  bus.readByte(id, sts::kTorqueEnable, &torque) &&
                  bus.readWord(id, sts::kMinAngle, &minA) && bus.readWord(id, sts::kMaxAngle, &maxA) &&
                  bus.readByte(id, sts::kMinVoltage, &minV) && bus.readByte(id, sts::kMaxVoltage, &maxV);
  if (!ok) return reply(false, "servo %d stopped answering", id);
  Serial.printf(
      "{\"id\":%d,\"model\":%u,\"position\":%u,\"speed\":%d,\"load_pct\":%.1f,\"current_ma\":%.0f,"
      "\"voltage\":%.1f,\"temperature_c\":%u,\"moving\":%u,\"mode\":%u,\"torque\":%u,"
      "\"angle_limits\":[%u,%u],\"voltage_limits\":[%.1f,%.1f],\"error\":%u}\n",
      id, model, pos, sts::signedSpeed(speed), sts::signedLoad(load) / 10.0, current * 6.5, volt / 10.0,
      temp, moving, mode, torque, minA, maxA, minV / 10.0, maxV / 10.0, bus.lastError());
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

void cmdWatch(int id, int seconds) {
  const uint32_t end = millis() + seconds * 1000u;
  while (millis() < end) {
    uint16_t pos;
    if (bus.readWord(id, sts::kPosition, &pos))
      Serial.printf("position %u (%.1f deg)\n", pos, pos * 360.0 / sts::kStepsPerTurn);
    else
      Serial.println("no answer");
    delay(100);
  }
  reply(true, "watch done");
}

void handle(char* line) {
  char* argv[4] = {};
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 4; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
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
