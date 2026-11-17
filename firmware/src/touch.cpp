#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

namespace touch {

namespace {

constexpr uint8_t kAddr = 0x38;
constexpr uint32_t kPollMs = 10;
constexpr uint32_t kTapMaxMs = 350;  // a press shorter than this is a tap
constexpr uint32_t kTapGapMs = 450;  // taps closer together than this count as a series

bool down = false;
uint16_t lastX = 0, lastY = 0;
uint32_t lastPoll = 0;
uint32_t downSince = 0;
uint32_t lastTapEnd = 0;
int tapCount = 0;
bool longFired = false;
bool tripleReady = false;

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

bool poll(uint16_t* x, uint16_t* y) {
  uint8_t n;
  if (!readRegs(0x02, &n, 1) || (n & 0x0F) == 0) return false;
  uint8_t b[4];
  if (!readRegs(0x03, b, 4)) return false;
  *x = static_cast<uint16_t>((b[0] & 0x0F) << 8 | b[1]);
  *y = static_cast<uint16_t>((b[2] & 0x0F) << 8 | b[3]);
  return true;
}

}  // namespace

bool begin() {
  Wire.beginTransmission(kAddr);
  Wire.write(0x00);  // device mode register
  Wire.write(0x00);  // normal operating mode
  return Wire.endTransmission() == 0;
}

void update() {
  const uint32_t now = millis();
  if (now - lastPoll < kPollMs) return;
  lastPoll = now;

  uint16_t x, y;
  const bool isDown = poll(&x, &y);
  if (isDown) {
    lastX = x;
    lastY = y;
    if (!down) downSince = now;
  } else if (down) {
    // Released: a short press is a tap; taps in quick succession make a series.
    if (now - downSince <= kTapMaxMs) {
      tapCount = (now - lastTapEnd <= kTapGapMs + kTapMaxMs) ? tapCount + 1 : 1;
      lastTapEnd = now;
      if (tapCount == 3) {
        tripleReady = true;
        tapCount = 0;
      }
    } else {
      tapCount = 0;
    }
    longFired = false;
  }
  down = isDown;
}

bool read(uint16_t* x, uint16_t* y) {
  *x = lastX;
  *y = lastY;
  return down;
}

bool longPress(uint32_t ms) {
  if (down && !longFired && millis() - downSince >= ms) {
    longFired = true;
    tapCount = 0;
    return true;
  }
  return false;
}

bool tripleTap() {
  const bool r = tripleReady;
  tripleReady = false;
  return r;
}

}  // namespace touch
