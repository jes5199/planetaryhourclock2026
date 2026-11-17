#include "setup_screens.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "cities.h"
#include "lvgl.h"
#include "settings.h"
#include "setup_mode.h"
#include "tz.h"

namespace setup_screens {

namespace {

// The screen is about 36 mm across for 466 px (~13 px/mm), so touch targets need to be large:
// buttons are full-width pills at the top and bottom of the circle, where there is room.
const lv_color_t kAccent = lv_color_hex(0xF0B84B);
const lv_color_t kDim = lv_color_hex(0xB0B0B0);
constexpr int kButtonW = 210, kButtonH = 78;
constexpr int kTopY = -165, kBottomY = 165;
constexpr int kMaxCities = 60;
constexpr int kFirstYear = 2025, kLastYear = 2045;

lv_obj_t* homeScreen;
lv_obj_t* homeQr;
lv_obj_t* homeInfo;
lv_obj_t* timeScreen;
lv_obj_t* timeRollers[5];  // hour, minute, day, month, year
lv_obj_t* pickScreen;
lv_obj_t* pickTitle;
lv_obj_t* pickRoller;
lv_obj_t* coordScreen;
lv_obj_t* coordRollers[6];  // N/S, lat degrees, lat tenths, E/W, lon degrees, lon tenths

// Place picker state: step 0 region, 1 zone, 2 state/province (skipped if the zone has only
// one), 3 city; then the coordinates screen.
int pickStep = 0;
std::vector<std::string> regions;
std::vector<int> zonesInRegion;  // zone indexes for the chosen region
int chosenRegion = -1;
std::string chosenZone;
constexpr int kMaxAreas = 80;
cities::Area areaList[kMaxAreas];
size_t areaCount = 0;
int chosenArea = -1;
cities::Result cityList[kMaxCities];
size_t cityCount = 0;
std::string chosenPlace;

lv_obj_t* makeScreen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, lv_color_black(), 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_color_t color, int x, int y) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_CENTER, x, y);
  return l;
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, int y, lv_event_cb_t cb, bool primary) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, kButtonW, kButtonH);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, y);
  lv_obj_set_style_radius(b, kButtonH / 2, 0);
  lv_obj_set_style_bg_color(b, primary ? kAccent : lv_color_hex(0x2A2A36), 0);
  lv_obj_set_ext_click_area(b, 12);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_color(l, primary ? lv_color_black() : lv_color_white(), 0);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

lv_obj_t* makeRoller(lv_obj_t* parent, int w, int x, int y, int rows) {
  lv_obj_t* r = lv_roller_create(parent);
  lv_obj_set_width(r, w);
  // The selected row is drawn separately with the SELECTED part's style, so its line
  // spacing must match the main part's or the highlighted text drifts off its row.
  lv_obj_set_style_text_line_space(r, 6, 0);
  lv_obj_set_style_text_line_space(r, 6, LV_PART_SELECTED);
  lv_roller_set_visible_row_count(r, rows);
  lv_obj_align(r, LV_ALIGN_CENTER, x, y);
  lv_obj_set_style_bg_color(r, lv_color_black(), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_text_color(r, kDim, 0);
  lv_obj_set_style_bg_color(r, kAccent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(r, lv_color_black(), LV_PART_SELECTED);
  return r;
}

std::string numberList(int from, int to, const char* fmt) {
  std::string s;
  char buf[8];
  for (int i = from; i <= to; ++i) {
    snprintf(buf, sizeof buf, fmt, i);
    if (i > from) s += '\n';
    s += buf;
  }
  return s;
}

std::string prettyZone(const char* name) {
  // "America/Argentina/Buenos_Aires" -> "Argentina/Buenos Aires" (region dropped).
  const char* slash = strchr(name, '/');
  std::string s = slash ? slash + 1 : name;
  for (char& c : s) if (c == '_') c = ' ';
  return s;
}

std::string regionOf(const char* name) {
  const char* slash = strchr(name, '/');
  return slash ? std::string(name, slash - name) : std::string(name);
}

// ---- Home ---------------------------------------------------------------------

void openTime(lv_event_t*);
void openPlace(lv_event_t*);

void buildHome() {
  homeScreen = makeScreen();
  makeButton(homeScreen, "Time", kTopY, openTime, true);
  homeQr = lv_qrcode_create(homeScreen, 180, lv_color_black(), lv_color_white());
  lv_obj_set_style_border_color(homeQr, lv_color_white(), 0);
  lv_obj_set_style_border_width(homeQr, 10, 0);
  lv_obj_align(homeQr, LV_ALIGN_CENTER, 0, -12);
  homeInfo = makeLabel(homeScreen, "", kDim, 0, 104);
  makeButton(homeScreen, "Place", kBottomY, openPlace, true);
}

void backHome(lv_event_t*) { lv_scr_load(homeScreen); }

// ---- Time ---------------------------------------------------------------------

void saveTime(lv_event_t*) {
  struct tm t {};
  t.tm_hour = lv_roller_get_selected(timeRollers[0]);
  t.tm_min = lv_roller_get_selected(timeRollers[1]);
  t.tm_mday = lv_roller_get_selected(timeRollers[2]) + 1;
  t.tm_mon = lv_roller_get_selected(timeRollers[3]);
  t.tm_year = kFirstYear + lv_roller_get_selected(timeRollers[4]) - 1900;
  t.tm_isdst = -1;  // let the time zone rules decide
  setup_mode::applyTime(static_cast<long>(mktime(&t)));
  Serial.printf("setup: touch set time %04d-%02d-%02d %02d:%02d\n", t.tm_year + 1900, t.tm_mon + 1,
                t.tm_mday, t.tm_hour, t.tm_min);
  lv_scr_load(homeScreen);
}

void buildTime() {
  timeScreen = makeScreen();
  makeButton(timeScreen, "Cancel", kTopY, backHome, false);
  timeRollers[0] = makeRoller(timeScreen, 96, -58, -52, 3);
  lv_roller_set_options(timeRollers[0], numberList(0, 23, "%02d").c_str(), LV_ROLLER_MODE_INFINITE);
  makeLabel(timeScreen, ":", lv_color_white(), 0, -52);
  timeRollers[1] = makeRoller(timeScreen, 96, 58, -52, 3);
  // Not INFINITE: LVGL repeats an infinite roller's options 7 times, and 60 x 7 rows is taller
  // than LVGL 8's coordinate limit (8191 px), which garbles the selection.
  lv_roller_set_options(timeRollers[1], numberList(0, 59, "%02d").c_str(), LV_ROLLER_MODE_NORMAL);

  timeRollers[2] = makeRoller(timeScreen, 76, -112, 60, 3);
  lv_roller_set_options(timeRollers[2], numberList(1, 31, "%d").c_str(), LV_ROLLER_MODE_INFINITE);
  timeRollers[3] = makeRoller(timeScreen, 96, -16, 60, 3);
  lv_roller_set_options(timeRollers[3], "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec",
                        LV_ROLLER_MODE_INFINITE);
  timeRollers[4] = makeRoller(timeScreen, 104, 98, 60, 3);
  lv_roller_set_options(timeRollers[4], numberList(kFirstYear, kLastYear, "%d").c_str(),
                        LV_ROLLER_MODE_NORMAL);
  makeButton(timeScreen, "Set", kBottomY, saveTime, true);
}

void openTime(lv_event_t*) {
  if (!timeScreen) buildTime();
  // Start from the clock's current local time.
  const time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  lv_roller_set_selected(timeRollers[0], lt.tm_hour, LV_ANIM_OFF);
  lv_roller_set_selected(timeRollers[1], lt.tm_min, LV_ANIM_OFF);
  lv_roller_set_selected(timeRollers[2], lt.tm_mday - 1, LV_ANIM_OFF);
  lv_roller_set_selected(timeRollers[3], lt.tm_mon, LV_ANIM_OFF);
  int year = lt.tm_year + 1900;
  if (year < kFirstYear) year = kFirstYear;
  if (year > kLastYear) year = kLastYear;
  lv_roller_set_selected(timeRollers[4], year - kFirstYear, LV_ANIM_OFF);
  lv_scr_load(timeScreen);
}

// ---- Coordinates (after choosing a city) -----------------------------------------

void setCoordRollers(double lat, double lon) {
  auto split = [](double v, int maxDeg, int* deg, int* tenth) {
    int t = static_cast<int>(lround(fabs(v) * 10.0));
    if (t > maxDeg * 10 + 9) t = maxDeg * 10 + 9;
    *deg = t / 10;
    *tenth = t % 10;
  };
  int d, t;
  split(lat, 89, &d, &t);
  lv_roller_set_selected(coordRollers[0], lat < 0 ? 1 : 0, LV_ANIM_OFF);
  lv_roller_set_selected(coordRollers[1], d, LV_ANIM_OFF);
  lv_roller_set_selected(coordRollers[2], t, LV_ANIM_OFF);
  split(lon, 179, &d, &t);
  lv_roller_set_selected(coordRollers[3], lon < 0 ? 1 : 0, LV_ANIM_OFF);
  lv_roller_set_selected(coordRollers[4], d, LV_ANIM_OFF);
  lv_roller_set_selected(coordRollers[5], t, LV_ANIM_OFF);
}

void coordBack(lv_event_t*) { lv_scr_load(pickScreen); }

void saveCoords(lv_event_t*) {
  const double lat = (lv_roller_get_selected(coordRollers[1]) + lv_roller_get_selected(coordRollers[2]) / 10.0) *
                     (lv_roller_get_selected(coordRollers[0]) ? -1 : 1);
  const double lon = (lv_roller_get_selected(coordRollers[4]) + lv_roller_get_selected(coordRollers[5]) / 10.0) *
                     (lv_roller_get_selected(coordRollers[3]) ? -1 : 1);
  setup_mode::applyZone(chosenZone.c_str());
  setup_mode::applyLocation(lat, lon, chosenPlace.c_str());
  Serial.printf("setup: touch set place %s (%.1f, %.1f) %s\n", chosenPlace.c_str(), lat, lon, chosenZone.c_str());
  lv_scr_load(homeScreen);
}

void buildCoords() {
  coordScreen = makeScreen();
  makeButton(coordScreen, "Back", kTopY, coordBack, false);
  coordRollers[0] = makeRoller(coordScreen, 64, -106, -52, 3);
  lv_roller_set_options(coordRollers[0], "N\nS", LV_ROLLER_MODE_NORMAL);
  coordRollers[1] = makeRoller(coordScreen, 96, -16, -52, 3);
  lv_roller_set_options(coordRollers[1], numberList(0, 89, "%d").c_str(), LV_ROLLER_MODE_NORMAL);
  coordRollers[2] = makeRoller(coordScreen, 76, 78, -52, 3);
  lv_roller_set_options(coordRollers[2], numberList(0, 9, ".%d").c_str(), LV_ROLLER_MODE_INFINITE);

  coordRollers[3] = makeRoller(coordScreen, 64, -106, 60, 3);
  lv_roller_set_options(coordRollers[3], "E\nW", LV_ROLLER_MODE_NORMAL);
  coordRollers[4] = makeRoller(coordScreen, 96, -16, 60, 3);
  lv_roller_set_options(coordRollers[4], numberList(0, 179, "%d").c_str(), LV_ROLLER_MODE_NORMAL);
  coordRollers[5] = makeRoller(coordScreen, 76, 78, 60, 3);
  lv_roller_set_options(coordRollers[5], numberList(0, 9, ".%d").c_str(), LV_ROLLER_MODE_INFINITE);
  makeButton(coordScreen, "Save", kBottomY, saveCoords, true);
}

void openCoords(double lat, double lon) {
  if (!coordScreen) buildCoords();
  setCoordRollers(lat, lon);
  lv_scr_load(coordScreen);
}

// ---- Place: region -> zone -> city (A-Z) -> coordinates --------------------------

void showPickStep();

void pickBack(lv_event_t*) {
  if (pickStep == 0) {
    lv_scr_load(homeScreen);
  } else {
    --pickStep;
    if (pickStep == 2 && areaCount <= 1) --pickStep;  // the area step was skipped
    showPickStep();
  }
}

void pickNext(lv_event_t*) {
  const uint16_t sel = lv_roller_get_selected(pickRoller);
  if (pickStep == 0) {
    chosenRegion = sel;
    pickStep = 1;
    showPickStep();
  } else if (pickStep == 1) {
    const std::string zone = tz::zoneName(zonesInRegion[sel]);
    if (zone != chosenZone) chosenArea = -1;
    chosenZone = zone;
    areaCount = cities::areasInZone(chosenZone.c_str(), areaList, kMaxAreas);
    pickStep = areaCount > 1 ? 2 : 3;
    if (areaCount == 1) chosenArea = 0;
    showPickStep();
  } else if (pickStep == 2) {
    chosenArea = sel;
    pickStep = 3;
    showPickStep();
  } else if (sel < cityCount) {
    const cities::Result& c = cityList[sel];
    chosenPlace = std::string(c.name) + ", " + (*c.region ? c.region : c.country);
    openCoords(c.lat, c.lon);
  } else {
    // No cities listed for this zone: keep the place name, adjust the coordinates.
    chosenPlace = settings::current().place;
    openCoords(settings::current().location.latDeg, settings::current().location.lonDeg);
  }
}

void buildPick() {
  pickScreen = makeScreen();
  makeButton(pickScreen, "Back", kTopY, pickBack, false);
  pickTitle = makeLabel(pickScreen, "", lv_color_white(), 0, -96);
  pickRoller = makeRoller(pickScreen, 380, 0, 14, 3);
  makeButton(pickScreen, "Next", kBottomY, pickNext, true);

  // Regions are the first part of the zone names: Africa, America, Asia, Europe, ...
  for (int i = 0; i < tz::zoneCount(); ++i) {
    const std::string r = regionOf(tz::zoneName(i));
    if (regions.empty() || regions.back() != r) regions.push_back(r);  // table is sorted
  }
}

void showPickStep() {
  const char* currentZone = settings::current().tzName;
  std::string options;
  int select = 0;

  if (pickStep == 0) {
    lv_label_set_text(pickTitle, "Region");
    const std::string mine = regionOf(currentZone);
    for (size_t i = 0; i < regions.size(); ++i) {
      if (i) options += '\n';
      options += regions[i];
      if (regions[i] == mine) select = i;
    }
    if (chosenRegion >= 0) select = chosenRegion;
  } else if (pickStep == 1) {
    lv_label_set_text(pickTitle, "Time zone");
    const std::string& region = regions[chosenRegion];
    zonesInRegion.clear();
    for (int i = 0; i < tz::zoneCount(); ++i) {
      const char* name = tz::zoneName(i);
      if (regionOf(name) != region) continue;
      if (!zonesInRegion.empty()) options += '\n';
      options += prettyZone(name);
      if (chosenZone.empty() ? strcmp(name, currentZone) == 0 : chosenZone == name) select = zonesInRegion.size();
      zonesInRegion.push_back(i);
    }
  } else if (pickStep == 2) {
    lv_label_set_text(pickTitle, "State / province");
    // Name the country too when a zone spans several.
    bool severalCountries = false;
    for (size_t i = 1; i < areaCount; ++i) severalCountries |= areaList[i].country != areaList[0].country;
    const char* place = settings::current().place;
    for (size_t i = 0; i < areaCount; ++i) {
      if (i) options += '\n';
      options += areaList[i].name;
      if (severalCountries && strcmp(areaList[i].name, areaList[i].country_name) != 0) {
        options += " (";
        options += areaList[i].country_name;
        options += ")";
      }
      const char* comma = strstr(place, ", ");
      if (comma && strcmp(comma + 2, areaList[i].name) == 0) select = i;
    }
    if (chosenArea >= 0) select = chosenArea;
  } else {
    lv_label_set_text(pickTitle, "Nearest city");
    // The area's largest cities (or the zone's, if it has no areas), listed A-Z.
    if (chosenArea >= 0 && chosenArea < static_cast<int>(areaCount)) {
      cityCount = cities::inArea(chosenZone.c_str(), areaList[chosenArea], cityList, kMaxCities);
    } else {
      cityCount = cities::inZone(chosenZone.c_str(), cityList, kMaxCities);
    }
    std::sort(cityList, cityList + cityCount,
              [](const cities::Result& a, const cities::Result& b) { return strcmp(a.name, b.name) < 0; });
    const char* place = settings::current().place;
    for (size_t i = 0; i < cityCount; ++i) {
      if (i) options += '\n';
      options += cityList[i].name;
      const size_t n = strlen(cityList[i].name);
      if (strncmp(place, cityList[i].name, n) == 0 && place[n] == ',') select = i;
    }
    if (cityCount == 0) options = "(no cities listed)";
  }
  lv_roller_set_options(pickRoller, options.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(pickRoller, select, LV_ANIM_OFF);
}

void openPlace(lv_event_t*) {
  if (!pickScreen) buildPick();
  pickStep = 0;
  chosenRegion = -1;
  chosenZone.clear();
  chosenArea = -1;
  areaCount = 0;
  showPickStep();
  lv_scr_load(pickScreen);
}

}  // namespace

void showHome(const char* ssid, const char* password, const char* wifiQr) {
  if (!homeScreen) buildHome();
  lv_qrcode_update(homeQr, wifiQr, strlen(wifiQr));
  lv_label_set_text_fmt(homeInfo, "%s  %s", ssid, password);
  lv_scr_load(homeScreen);
}

bool onHome() { return homeScreen && lv_scr_act() == homeScreen; }

}  // namespace setup_screens
