// Whether the clock knows the real time. False after the RTC lost power (or held a stale
// time) until the time is set from a phone or synced over NTP.
#pragma once

namespace time_status {

inline bool& flag() {
  static bool valid = false;
  return valid;
}
inline bool valid() { return flag(); }
inline void setValid(bool v) { flag() = v; }

// The last time known to be good, kept in flash. An RTC reading earlier than this means
// the RTC stopped (e.g. power was cut without a backup cell).
long lastGood();
void saveLastGood(long unixTime);

}  // namespace time_status
