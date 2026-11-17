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

constexpr int kZoneCount = sizeof kZones / sizeof kZones[0];

int find(const char* name) {
  int lo = 0, hi = kZoneCount;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    const int c = strcmp(name, kZones[mid].name);
    if (c == 0) return mid;
    if (c < 0) hi = mid;
    else lo = mid + 1;
  }
  return -1;
}

}  // namespace

int zoneIndex(const char* ianaName) {
  if (!ianaName || !*ianaName) return -1;
  const int i = find(ianaName);
  if (i >= 0) return i;
  for (const Zone& a : kAliases) {
    if (strcmp(ianaName, a.name) == 0) return find(a.posix);
  }
  return -1;
}

const char* zoneName(int index) { return index >= 0 && index < kZoneCount ? kZones[index].name : nullptr; }

const char* posixFor(const char* ianaName) {
  const int i = zoneIndex(ianaName);
  return i >= 0 ? kZones[i].posix : nullptr;
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
