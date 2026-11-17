#include "rtc.h"

#include <Arduino.h>
#include <Wire.h>

#include "sun.h"

namespace rtc {

namespace {

constexpr uint8_t kAddr = 0x51;
constexpr uint8_t kRegSeconds = 0x04;

uint8_t toBcd(int v) { return static_cast<uint8_t>((v / 10) << 4 | (v % 10)); }
int fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

}  // namespace

bool begin() {
  Wire.beginTransmission(kAddr);
  return Wire.endTransmission() == 0;
}

bool read(time_t* utcOut) {
  Wire.beginTransmission(kAddr);
  Wire.write(kRegSeconds);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAddr, static_cast<uint8_t>(7)) != 7) return false;
  uint8_t r[7];
  for (auto& b : r) b = Wire.read();

  if (r[0] & 0x80) return false;  // OS flag: oscillator stopped, time not trustworthy
  const int sec = fromBcd(r[0] & 0x7F);
  const int min = fromBcd(r[1] & 0x7F);
  const int hour = fromBcd(r[2] & 0x3F);
  const int day = fromBcd(r[3] & 0x3F);
  const int month = fromBcd(r[5] & 0x1F);
  const int year = 2000 + fromBcd(r[6]);
  if (month < 1 || month > 12 || day < 1 || day > 31 || year < 2024) return false;

  *utcOut = static_cast<time_t>(planetary::daysFromCivil(year, month, day) * 86400 +
                                hour * 3600 + min * 60 + sec);
  return true;
}

bool write(time_t utc) {
  struct tm t;
  gmtime_r(&utc, &t);
  Wire.beginTransmission(kAddr);
  Wire.write(kRegSeconds);
  Wire.write(toBcd(t.tm_sec));  // also clears the OS flag
  Wire.write(toBcd(t.tm_min));
  Wire.write(toBcd(t.tm_hour));
  Wire.write(toBcd(t.tm_mday));
  Wire.write(toBcd(t.tm_wday));
  Wire.write(toBcd(t.tm_mon + 1));
  Wire.write(toBcd(t.tm_year - 100));
  return Wire.endTransmission() == 0;
}

}  // namespace rtc
