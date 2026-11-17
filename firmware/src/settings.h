// User settings, saved in flash (NVS) so they survive power cuts. Set from the setup
// hotspot page, the touchscreen menu, or (later) Bluetooth.
#pragma once

#include <cstdint>

#include "planetary.h"

struct Settings {
  planetary::Location location;
  char place[48];           // human-readable place name, e.g. "Portland, Oregon"
  char tzName[48];          // IANA zone, e.g. "America/Los_Angeles" (for display and menus)
  char tz[64];              // POSIX TZ string, the form the C library uses
  uint8_t brightnessDay;    // 0..255
  uint8_t brightnessNight;  // 0..255
  int16_t screenRotation;   // degrees clockwise: 0, 90, 180, 270
};

namespace settings {

Settings defaults();          // Portland, Oregon
Settings& current();          // the live settings
void load();                  // read from flash (falls back to defaults)
void save();                  // write current() to flash
void applyTimezone();         // push current().tz into the C library

}  // namespace settings
