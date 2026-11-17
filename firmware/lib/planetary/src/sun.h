// Sunrise and sunset from the NOAA solar calculator equations.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <cstdint>

namespace planetary {

struct SunTimes {
  int64_t sunrise;   // unix seconds (UTC)
  int64_t sunset;    // unix seconds (UTC)
  bool polarDay;     // sun never sets: sunrise/sunset clamped to solar midnight
  bool polarNight;   // sun never rises: sunrise/sunset both at solar noon
};

// Days since 1970-01-01 for a proleptic Gregorian calendar date.
int64_t daysFromCivil(int y, unsigned m, unsigned d);

// Sunrise and sunset for a calendar date at a location.
// `epochDay` is the local calendar date as days since 1970-01-01.
// Latitude is north-positive, longitude east-positive, both in degrees.
// The returned times are the events nearest that date's local solar noon.
SunTimes sunTimes(int64_t epochDay, double latDeg, double lonDeg);

}  // namespace planetary
