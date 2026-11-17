// User settings. For now these are compile-time defaults; later they will be
// stored in NVS and set from the touchscreen menu, the web app or Bluetooth.
#pragma once

#include "planetary.h"

struct Settings {
  planetary::Location location;
  // POSIX TZ string, see https://github.com/nayarsystems/posix_tz_db
  const char* timezone;
  uint8_t brightnessDay;    // 0..255
  uint8_t brightnessNight;  // 0..255
  int screenRotation;       // degrees clockwise: 0, 90, 180, 270
};

// Default: Portland, Oregon.
inline Settings defaultSettings() {
  return Settings{
      {45.5152, -122.6784},
      "PST8PDT,M3.2.0,M11.1.0",
      255,
      90,
      90,
  };
}
