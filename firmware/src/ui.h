// The centre screen: civil time, the hour's ruler, the hour's start/length/end,
// and the hour arc with its two virtual hands.
#pragma once

#include <ctime>

#include "planetary.h"

namespace ui {

void begin();
void update(const planetary::ClockState& s, time_t now);
void showTimeNotSet();  // "--:--" and "SET TIME" in place of the hour, hands hidden

void showClock();
bool onClock();  // true while the clock face is showing

}  // namespace ui
