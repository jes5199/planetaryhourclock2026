#include "tz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tz {

namespace {

struct Zone {
  const char* name;
  const char* posix;
};

const Zone kZones[] = {
#include "tz_table.inc"
};

// Old or new names that browsers report but the table spells differently.
const Zone kAliases[] = {
    {"Asia/Calcutta", "Asia/Kolkata"},          {"Asia/Saigon", "Asia/Ho_Chi_Minh"},
    {"Asia/Katmandu", "Asia/Kathmandu"},        {"Asia/Rangoon", "Asia/Yangon"},
    {"America/Indianapolis", "America/Indiana/Indianapolis"},
    {"Europe/Kyiv", "Europe/Kiev"},             {"Pacific/Kanton", "Pacific/Enderbury"},
    {"UTC", "Etc/UTC"},                         {"GMT", "Etc/UTC"},
};

const char* lookup(const char* name) {
  size_t lo = 0, hi = sizeof kZones / sizeof kZones[0];
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = strcmp(name, kZones[mid].name);
    if (c == 0) return kZones[mid].posix;
    if (c < 0) hi = mid;
    else lo = mid + 1;
  }
  return nullptr;
}

}  // namespace

const char* posixFor(const char* ianaName) {
  if (!ianaName || !*ianaName) return nullptr;
  if (const char* p = lookup(ianaName)) return p;
  for (const Zone& a : kAliases) {
    if (strcmp(ianaName, a.name) == 0) return lookup(a.posix);
  }
  return nullptr;
}

void fixedOffset(int minutesEast, char* out, size_t n) {
  // POSIX offsets are west-positive: UTC+5:30 is "<+0530>-5:30".
  const char sign = minutesEast >= 0 ? '+' : '-';
  const int m = std::abs(minutesEast);
  if (m % 60) {
    snprintf(out, n, "<%c%02d%02d>%c%d:%02d", sign, m / 60, m % 60, sign == '+' ? '-' : '+', m / 60, m % 60);
  } else {
    snprintf(out, n, "<%c%02d>%c%d", sign, m / 60, sign == '+' ? '-' : '+', m / 60);
  }
}

}  // namespace tz
