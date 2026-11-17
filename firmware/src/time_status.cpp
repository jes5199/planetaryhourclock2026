#include "time_status.h"

#include <Preferences.h>

namespace time_status {

long lastGood() {
  Preferences p;
  if (!p.begin("time", true)) return 0;
  const long t = p.getLong("lastGood", 0);
  p.end();
  return t;
}

void saveLastGood(long unixTime) {
  Preferences p;
  if (!p.begin("time", false)) return;
  p.putLong("lastGood", unixTime);
  p.end();
}

}  // namespace time_status
