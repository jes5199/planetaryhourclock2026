// Planetary-hour clock state: everything the dial, hands, LEDs and screen need,
// computed from one instant and one location. Pure C++, host-testable.
//
// Angles are degrees clockwise from the top (12 o'clock), in [0, 360).
// See the design doc "The planetary hours clock: how it works", section "The maths".
#pragma once

#include <cstdint>

namespace planetary {

// Chaldean order, the order in which planetary hours repeat.
enum class Planet : uint8_t { Saturn = 0, Jupiter, Mars, Sun, Venus, Mercury, Moon };

const char* planetName(Planet p);  // "SATURN", "JUPITER", ...

struct Location {
  double latDeg;   // north positive
  double lonDeg;   // east positive
};

struct ClockState {
  // --- Temporal time ---
  double h;               // temporal hours since sunrise, [0, 24); 12+ is night
  bool isDay;             // h < 12
  int hourIndex;          // floor(h), 0..23
  int planetaryWeekday;   // weekday of the planetary day (starts at sunrise), Sunday = 0
  Planet ruler;           // ruler of the current planetary hour
  Planet dayRuler;        // ruler of the planetary day
  Planet nightRuler;      // ruler of the current/most recent night (first hour after sunset)

  // --- Civil anchors (unix seconds, UTC) ---
  int64_t periodStart;    // sunrise (day) or sunset (night) that began this half
  int64_t periodEnd;      // the sunset or next sunrise that ends it
  int64_t hourStart;      // start of the current temporal hour
  int64_t hourEnd;        // end of the current temporal hour
  double hourLengthMin;   // length of the current temporal hour, civil minutes
  double dayLengthHours;  // L: civil hours of daylight for the current planetary day

  // --- Dial ---
  double goldHandDeg;     // gold (planet) hand
  double hourHandDeg;     // temporal hour hand
  int dayRingWeekday;     // civil weekday shown on the day ring (changes at temporal midnight)
  int dayRingLed;         // 0..83, index of the bright day-ring LED

  // --- Screen ---
  double arcSpanDeg;      // S: arc length in degrees
  double arcStartDeg;     // a0: arc start angle
  double screenDayHandDeg;   // white "day" hand on the arc
  double screenHourHandDeg;  // bright "hour" hand on the arc
};

// `utcOffsetSec` is the local civil offset from UTC at `now` (for the local date and weekday).
ClockState computeClock(int64_t now, int32_t utcOffsetSec, const Location& loc);

// Weekday (Sunday = 0) of an epoch day number.
int weekdayOfEpochDay(int64_t epochDay);

// Normalize degrees to [0, 360).
double wrapDeg(double d);

}  // namespace planetary
