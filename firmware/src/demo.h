// Demo mode: runs one week of clock time in a few minutes, starting from now, so the whole
// cycle (days, nights, planetary hours, the midnight jumps) can be watched. Triple-tap the
// clock face (or send `demo` over serial) to start or stop; it stops by itself after the week.
// The real clock keeps running underneath.
#pragma once

#include <ctime>

namespace demo {

constexpr unsigned kWeekRealSeconds = 180;  // one simulated week takes 3 minutes

void start();
void stop();
bool active();
time_t now();            // the simulated time (only meaningful while active)
const char* banner();    // "DEMO MON": shown in place of the hour countdown

}  // namespace demo
