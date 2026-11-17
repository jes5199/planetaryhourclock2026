#include "cities.h"

#include <cstdint>
#include <algorithm>
#include <cstring>

#include "tz.h"

namespace cities {

namespace {

struct City {
  const char* name;     // UTF-8
  const char* display;  // ASCII, shortened ("St" for "Saint", "&" for "and"); nullptr = same as name
  const char* search;   // lowercase ASCII form of the full name
  uint16_t region;
  uint8_t country;
  float lat, lon;
  uint32_t population;
  int16_t tz;  // index into tz::zoneName(), -1 if unknown
};

#include "city_table.inc"

constexpr size_t kCount = sizeof kCities / sizeof kCities[0];

bool matches(const char* name, const char* q, size_t qlen) {
  for (const char* p = name; *p; ++p) {
    if ((p == name || p[-1] == ' ' || p[-1] == '-') && strncmp(p, q, qlen) == 0) return true;
  }
  return false;
}

Result toResult(const City& c) {
  return {c.name, c.display ? c.display : c.name, kRegions[c.region], kCountries[c.country][1], c.lat, c.lon,
          c.tz >= 0 ? tz::zoneName(c.tz) : nullptr};
}

}  // namespace

size_t search(const char* query, const char* preferTz, Result* out, size_t max) {
  const size_t qlen = strlen(query);
  if (qlen == 0 || max == 0) return 0;
  const int preferred = preferTz ? tz::zoneIndex(preferTz) : -1;
  size_t n = 0;
  // Two passes: the phone's own time zone first, then everywhere else. The table is
  // sorted by population, so each pass lists bigger places first.
  for (int pass = 0; pass < 2 && n < max; ++pass) {
    if (pass == 0 && preferred < 0) continue;
    for (size_t i = 0; i < kCount && n < max; ++i) {
      const City& c = kCities[i];
      if ((c.tz == preferred) != (pass == 0)) continue;
      if (matches(c.search, query, qlen)) out[n++] = toResult(c);
    }
  }
  return n;
}

size_t inZone(const char* tzName, Result* out, size_t max) {
  const int z = tz::zoneIndex(tzName);
  size_t n = 0;
  for (size_t i = 0; i < kCount && n < max && z >= 0; ++i) {
    if (kCities[i].tz == z) out[n++] = toResult(kCities[i]);
  }
  return n;
}

size_t continents(const char** codes, const char** names, size_t max) {
  static const char* const kOrder[][2] = {{"AF", "Africa"},        {"AS", "Asia"},
                                          {"EU", "Europe"},        {"NA", "North America"},
                                          {"OC", "Oceania"},       {"SA", "South America"},
                                          {"AN", "Antarctica"}};
  size_t n = 0;
  for (const auto& c : kOrder) {
    if (n == max) break;
    codes[n] = c[0];
    names[n] = c[1];
    ++n;
  }
  return n;
}

size_t countriesIn(const char* continentCode, Country* out, size_t max) {
  constexpr size_t kCountryCount = sizeof kCountries / sizeof kCountries[0];
  size_t n = 0;
  for (size_t i = 0; i < kCountryCount && n < max; ++i) {
    if (strcmp(kCountries[i][2], continentCode) == 0) out[n++] = {static_cast<int>(i), kCountries[i][1]};
  }
  std::sort(out, out + n, [](const Country& a, const Country& b) { return strcmp(a.name, b.name) < 0; });
  return n;
}

size_t areasIn(int country, Area* out, size_t max) {
  size_t n = 0;
  for (size_t i = 0; i < kCount; ++i) {
    const City& c = kCities[i];
    if (c.country != country) continue;
    bool seen = false;
    for (size_t j = 0; j < n && !seen; ++j) seen = out[j].region == c.region;
    if (seen || n == max) continue;
    const char* region = kRegions[c.region];
    out[n++] = {c.country, c.region, *region ? region : kCountries[c.country][1]};
  }
  std::sort(out, out + n, [](const Area& a, const Area& b) { return strcmp(a.name, b.name) < 0; });
  return n;
}

size_t inArea(const Area& area, Result* out, size_t max) {
  size_t n = 0;
  for (size_t i = 0; i < kCount && n < max; ++i) {
    const City& c = kCities[i];
    if (c.country == area.country && c.region == area.region) out[n++] = toResult(c);
  }
  return n;
}

bool locateZone(const char* tzName, const char** continent, int* country, int* region) {
  const int z = tz::zoneIndex(tzName);
  for (size_t i = 0; i < kCount && z >= 0; ++i) {
    if (kCities[i].tz == z) {
      *continent = kCountries[kCities[i].country][2];
      *country = kCities[i].country;
      *region = kCities[i].region;
      return true;
    }
  }
  return false;
}

bool largestIn(const char* tzName, Result* out) {
  const int z = tz::zoneIndex(tzName);
  if (z < 0) return false;
  for (size_t i = 0; i < kCount; ++i) {
    if (kCities[i].tz == z) {
      *out = toResult(kCities[i]);
      return true;
    }
  }
  return false;
}

}  // namespace cities
