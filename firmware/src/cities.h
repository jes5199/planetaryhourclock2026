// Built-in list of the world's cities over 15,000 people (GeoNames, CC BY 4.0),
// for choosing the clock's location without GPS.
#pragma once

#include <cstddef>

namespace cities {

struct Result {
  const char* name;      // UTF-8, e.g. "São Paulo"
  const char* region;    // state/province, may be ""
  const char* country;   // e.g. "United States"
  float lat, lon;
  const char* tzName;    // IANA zone, or nullptr if unknown
};

// Cities whose name, or any word of it, starts with `query` (lowercase ASCII; the page
// strips accents). Cities in `preferTz` come first, then by population. Returns the count.
size_t search(const char* query, const char* preferTz, Result* out, size_t max);

// The largest cities in an IANA zone, biggest first. Returns the count.
size_t inZone(const char* tzName, Result* out, size_t max);

// A first-level division (state, province, region) within a time zone.
struct Area {
  int region;            // opaque ids, for inArea()
  int country;
  const char* name;      // e.g. "Oregon" (falls back to the country name)
  const char* country_name;
};

// The areas that have cities in a zone, A-Z. Returns the count.
size_t areasInZone(const char* tzName, Area* out, size_t max);

// The largest cities in one area of a zone, biggest first. Returns the count.
size_t inArea(const char* tzName, const Area& area, Result* out, size_t max);

// The most populous city in an IANA zone (a good default guess), or false if none.
bool largestIn(const char* tzName, Result* out);

}  // namespace cities
