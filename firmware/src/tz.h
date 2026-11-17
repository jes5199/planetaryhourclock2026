// IANA time zone names ("America/Los_Angeles") to POSIX TZ strings, which is what the
// C library needs. Table from posix_tz_db (MIT), see tools/gen_tz_table.py.
#pragma once

#include <cstddef>

namespace tz {

// POSIX string for an IANA name (handles a few old aliases browsers still report),
// or nullptr if unknown.
const char* posixFor(const char* ianaName);

// Index of an IANA name (after aliases) in the zone table, or -1; and the reverse.
int zoneIndex(const char* ianaName);
const char* zoneName(int index);
int zoneCount();

// A fixed-offset POSIX string, no daylight saving: minutes east of UTC (e.g. 330 for India).
void fixedOffset(int minutesEast, char* out, size_t n);

}  // namespace tz
