#include "planetary.h"

#include <cmath>

#include "sun.h"

namespace planetary {

namespace {

constexpr double kWedgeDeg = 360.0 / 7.0;

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

int mod7(int64_t x) {
  int r = static_cast<int>(x % 7);
  return r < 0 ? r + 7 : r;
}

}  // namespace

const char* planetName(Planet p) {
  static const char* const kNames[] = {"SATURN", "JUPITER", "MARS", "SUN",
                                       "VENUS",  "MERCURY", "MOON"};
  return kNames[static_cast<int>(p)];
}

int weekdayOfEpochDay(int64_t epochDay) {
  return mod7(epochDay + 4);  // 1970-01-01 was a Thursday
}

double wrapDeg(double d) {
  double r = std::fmod(d, 360.0);
  if (r < 0) r += 360.0;
  if (r >= 360.0) r -= 360.0;
  return r;
}

ClockState computeClock(int64_t now, int32_t utcOffsetSec, const Location& loc) {
  ClockState s{};

  const int64_t today = floorDiv(now + utcOffsetSec, 86400);
  const SunTimes sunToday = sunTimes(today, loc.latDeg, loc.lonDeg);

  int64_t planetaryDay;  // local epoch day on which the planetary day began (at sunrise)
  int64_t dayRise, daySet;
  if (now < sunToday.sunrise) {
    // Before sunrise: still the night of yesterday's planetary day.
    const SunTimes sunYesterday = sunTimes(today - 1, loc.latDeg, loc.lonDeg);
    planetaryDay = today - 1;
    dayRise = sunYesterday.sunrise;
    daySet = sunYesterday.sunset;
    s.isDay = false;
    s.periodStart = sunYesterday.sunset;
    s.periodEnd = sunToday.sunrise;
  } else if (now < sunToday.sunset) {
    planetaryDay = today;
    dayRise = sunToday.sunrise;
    daySet = sunToday.sunset;
    s.isDay = true;
    s.periodStart = sunToday.sunrise;
    s.periodEnd = sunToday.sunset;
  } else {
    const SunTimes sunTomorrow = sunTimes(today + 1, loc.latDeg, loc.lonDeg);
    planetaryDay = today;
    dayRise = sunToday.sunrise;
    daySet = sunToday.sunset;
    s.isDay = false;
    s.periodStart = sunToday.sunset;
    s.periodEnd = sunTomorrow.sunrise;
  }

  const double periodSec = static_cast<double>(s.periodEnd - s.periodStart);
  const double elapsed = static_cast<double>(now - s.periodStart);
  double frac = periodSec > 0 ? elapsed / periodSec : 0.0;
  if (frac < 0) frac = 0;
  if (frac >= 1) frac = std::nextafter(1.0, 0.0);

  s.h = (s.isDay ? 0.0 : 12.0) + 12.0 * frac;
  s.hourIndex = static_cast<int>(std::floor(s.h));
  s.dayLengthHours = static_cast<double>(daySet - dayRise) / 3600.0;

  // Planetary day/hour rulers. n counts days from Saturday = 0.
  s.planetaryWeekday = weekdayOfEpochDay(planetaryDay);
  const int n = mod7(s.planetaryWeekday + 1);
  const double H = 24.0 * n + s.h;
  s.ruler = static_cast<Planet>(mod7(static_cast<int64_t>(std::floor(H))));
  s.dayRuler = static_cast<Planet>(mod7(3 * n));
  s.nightRuler = static_cast<Planet>(mod7(3 * n + 5));

  // Current temporal hour in civil time.
  const double hourSec = periodSec / 12.0;
  const int hourInPeriod = s.hourIndex % 12;
  s.hourStart = s.periodStart + static_cast<int64_t>(std::llround(hourInPeriod * hourSec));
  s.hourEnd = s.periodStart + static_cast<int64_t>(std::llround((hourInPeriod + 1) * hourSec));
  s.hourLengthMin = hourSec / 60.0;

  // Dial hands.
  s.goldHandDeg = wrapDeg((H - 3.5) * kWedgeDeg);
  s.hourHandDeg = wrapDeg(15.0 * (s.h - 6.0));

  // Day ring: the civil day changes at temporal midnight (h = 18), where the dial puts it.
  s.dayRingWeekday = mod7(s.planetaryWeekday + (s.h >= 18.0 ? 1 : 0));
  const int slice = mod7(3 * s.dayRingWeekday);
  const double f = std::fmod(0.25 + s.h / 24.0, 1.0);
  s.dayRingLed = 12 * slice + static_cast<int>(std::floor(12.0 * f));

  // Screen arc and its two virtual hands.
  s.arcSpanDeg = 15.0 * periodSec / 3600.0;
  const double a0 = (s.isDay ? 0.0 : 180.0) - s.arcSpanDeg / 2.0;
  s.arcStartDeg = wrapDeg(a0);
  s.screenDayHandDeg = wrapDeg(a0 + s.arcSpanDeg * frac);
  s.screenHourHandDeg = wrapDeg(a0 + s.arcSpanDeg * (s.h - std::floor(s.h)));

  return s;
}

}  // namespace planetary
