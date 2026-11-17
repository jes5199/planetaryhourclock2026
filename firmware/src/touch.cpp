#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

namespace touch {

namespace {

constexpr uint8_t kAddr = 0x38;

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

}  // namespace

bool begin() {
  Wire.beginTransmission(kAddr);
  Wire.write(0x00);  // device mode register
  Wire.write(0x00);  // normal operating mode
  return Wire.endTransmission() == 0;
}

bool read(uint16_t* x, uint16_t* y) {
  uint8_t n;
  if (!readRegs(0x02, &n, 1) || (n & 0x0F) == 0) return false;
  uint8_t b[4];
  if (!readRegs(0x03, b, 4)) return false;
  *x = static_cast<uint16_t>((b[0] & 0x0F) << 8 | b[1]);
  *y = static_cast<uint16_t>((b[2] & 0x0F) << 8 | b[3]);
  return true;
}

bool longPress(uint32_t ms) {
  static uint32_t lastPoll = 0;
  static uint32_t downSince = 0;
  static bool fired = false;
  const uint32_t now = millis();
  if (now - lastPoll < 40) return false;
  lastPoll = now;

  uint16_t x, y;
  if (!read(&x, &y)) {
    downSince = 0;
    fired = false;
    return false;
  }
  if (downSince == 0) downSince = now;
  if (!fired && now - downSince >= ms) {
    fired = true;
    return true;
  }
  return false;
}

}  // namespace touch
