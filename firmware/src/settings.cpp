#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

namespace settings {

namespace {

constexpr const char* kNamespace = "clock";
constexpr uint16_t kVersion = 1;  // bump when the stored layout changes

Settings live = defaults();
bool wasSaved = false;

void copy(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

}  // namespace

Settings defaults() {
  Settings s{};
  s.location = {45.5152, -122.6784};
  copy(s.place, sizeof s.place, "Portland, Oregon");
  copy(s.tzName, sizeof s.tzName, "America/Los_Angeles");
  copy(s.tz, sizeof s.tz, "PST8PDT,M3.2.0,M11.1.0");
  s.brightnessDay = 255;
  s.brightnessNight = 90;
  s.screenRotation = 90;
  return s;
}

Settings& current() { return live; }

bool saved() { return wasSaved; }

void load() {
  Preferences p;
  live = defaults();
  wasSaved = false;
  if (!p.begin(kNamespace, false)) return;
  if (p.getUShort("ver", 0) == kVersion) {
    wasSaved = true;
    live.location.latDeg = p.getDouble("lat", live.location.latDeg);
    live.location.lonDeg = p.getDouble("lon", live.location.lonDeg);
    p.getString("place", live.place, sizeof live.place);
    p.getString("tzName", live.tzName, sizeof live.tzName);
    p.getString("tz", live.tz, sizeof live.tz);
    live.brightnessDay = p.getUChar("briDay", live.brightnessDay);
    live.brightnessNight = p.getUChar("briNight", live.brightnessNight);
    live.screenRotation = p.getShort("rot", live.screenRotation);
  }
  p.end();
}

void save() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  p.putUShort("ver", kVersion);
  p.putDouble("lat", live.location.latDeg);
  p.putDouble("lon", live.location.lonDeg);
  p.putString("place", live.place);
  p.putString("tzName", live.tzName);
  p.putString("tz", live.tz);
  p.putUChar("briDay", live.brightnessDay);
  p.putUChar("briNight", live.brightnessNight);
  p.putShort("rot", live.screenRotation);
  p.end();
}

void applyTimezone() {
  setenv("TZ", live.tz, 1);
  tzset();
}

}  // namespace settings
