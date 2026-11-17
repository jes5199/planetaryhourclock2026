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

// Browsing by place, for the touchscreen: continent -> country -> area -> city.
struct Country {
  int id;
  const char* name;
};
struct Area {
  int country;           // opaque ids, for inArea()
  int region;
  const char* name;      // e.g. "Oregon" (the country name if the city has no division)
};

// Continents with cities, in display order: codes ("NA") and names ("North America").
size_t continents(const char** codes, const char** names, size_t max);
// Countries with cities in a continent, A-Z.
size_t countriesIn(const char* continentCode, Country* out, size_t max);
// States/provinces/regions with cities in a country, A-Z.
size_t areasIn(int country, Area* out, size_t max);
// The largest cities in an area, biggest first.
size_t inArea(const Area& area, Result* out, size_t max);
// Where the largest city of a time zone is: its continent code, country and area (for
// preselecting the pickers). False if the zone has no cities.
bool locateZone(const char* tzName, const char** continent, int* country, int* region);

// The most populous city in an IANA zone (a good default guess), or false if none.
bool largestIn(const char* tzName, Result* out);

}  // namespace cities
