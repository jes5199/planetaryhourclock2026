// The centre screen: civil time, the hour's ruler, the hour's start/length/end,
// and the hour arc with its two virtual hands.
#pragma once

#include <ctime>

#include "planetary.h"

namespace ui {

void begin();
// `shown` is the time on the dial (real, or the demo's); `realNow` paces the burn-in drift.
// `banner`, if set, replaces the hour countdown (e.g. "DEMO MON").
void update(const planetary::ClockState& s, time_t shown, time_t realNow, const char* banner = nullptr);
void showTimeNotSet();  // "--:--" and "SET TIME" in place of the hour, hands hidden

void showClock();
bool onClock();  // true while the clock face is showing

}  // namespace ui
