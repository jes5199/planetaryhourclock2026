#include "setup_screens.h"

#include <Arduino.h>

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

const lv_color_t kAccent = lv_color_hex(0xF0B84B);
const lv_color_t kDim = lv_color_hex(0xB0B0B0);
constexpr int kMaxCities = 40;

lv_obj_t* homeScreen;
lv_obj_t* homeQr;
lv_obj_t* homeInfo;
lv_obj_t* timeScreen;
lv_obj_t* timeRollers[5];  // hour, minute, day, month, year
lv_obj_t* pickScreen;
lv_obj_t* pickTitle;
lv_obj_t* pickRoller;
lv_obj_t* pickNext;

constexpr int kFirstYear = 2025, kLastYear = 2045;

// Place picker state: step 0 region, 1 zone, 2 city.
int pickStep = 0;
std::vector<std::string> regions;
std::vector<int> zonesInRegion;  // zone indexes for the chosen region
int chosenRegion = -1;
std::string chosenZone;
cities::Result cityList[kMaxCities];
size_t cityCount = 0;

lv_obj_t* makeScreen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, lv_color_black(), 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_color_t color, int y) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  return l;
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, int x, int y, int w, lv_event_cb_t cb,
                     bool primary) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, 54);
  lv_obj_align(b, LV_ALIGN_CENTER, x, y);
  lv_obj_set_style_radius(b, 27, 0);
  lv_obj_set_style_bg_color(b, primary ? kAccent : lv_color_hex(0x2A2A36), 0);
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
  lv_roller_set_visible_row_count(r, rows);
  lv_obj_align(r, LV_ALIGN_CENTER, x, y);
  lv_obj_set_style_bg_color(r, lv_color_black(), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_text_color(r, kDim, 0);
  lv_obj_set_style_bg_color(r, kAccent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(r, lv_color_black(), LV_PART_SELECTED);
  return r;
}

std::string numberList(int from, int to, bool pad) {
  std::string s;
  char buf[8];
  for (int i = from; i <= to; ++i) {
    snprintf(buf, sizeof buf, pad ? "%02d" : "%d", i);
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
  makeLabel(homeScreen, "Set up", lv_color_white(), -185);
  homeQr = lv_qrcode_create(homeScreen, 170, lv_color_black(), lv_color_white());
  lv_obj_set_style_border_color(homeQr, lv_color_white(), 0);
  lv_obj_set_style_border_width(homeQr, 10, 0);
  lv_obj_align(homeQr, LV_ALIGN_CENTER, 0, -50);
  homeInfo = makeLabel(homeScreen, "", kDim, 88);
  makeButton(homeScreen, "Time", -64, 165, 120, openTime, true);
  makeButton(homeScreen, "Place", 64, 165, 120, openPlace, true);
}

// ---- Time ---------------------------------------------------------------------

void backHome(lv_event_t*) { lv_scr_load(homeScreen); }

void saveTime(lv_event_t*) {
  struct tm t {};
  t.tm_hour = lv_roller_get_selected(timeRollers[0]);
  t.tm_min = lv_roller_get_selected(timeRollers[1]);
  t.tm_mday = lv_roller_get_selected(timeRollers[2]) + 1;
  t.tm_mon = lv_roller_get_selected(timeRollers[3]);
  t.tm_year = kFirstYear + lv_roller_get_selected(timeRollers[4]) - 1900;
  t.tm_isdst = -1;  // let the time zone rules decide
  const time_t when = mktime(&t);
  setup_mode::applyTime(static_cast<long>(when));
  Serial.printf("setup: touch set time %04d-%02d-%02d %02d:%02d\n", t.tm_year + 1900, t.tm_mon + 1,
                t.tm_mday, t.tm_hour, t.tm_min);
  lv_scr_load(homeScreen);
}

void buildTime() {
  timeScreen = makeScreen();
  makeLabel(timeScreen, "Time", lv_color_white(), -185);
  timeRollers[0] = makeRoller(timeScreen, 100, -62, -75, 3);
  lv_roller_set_options(timeRollers[0], numberList(0, 23, true).c_str(), LV_ROLLER_MODE_INFINITE);
  makeLabel(timeScreen, ":", lv_color_white(), -75);
  timeRollers[1] = makeRoller(timeScreen, 100, 62, -75, 3);
  lv_roller_set_options(timeRollers[1], numberList(0, 59, true).c_str(), LV_ROLLER_MODE_INFINITE);

  timeRollers[2] = makeRoller(timeScreen, 80, -118, 50, 3);
  lv_roller_set_options(timeRollers[2], numberList(1, 31, false).c_str(), LV_ROLLER_MODE_INFINITE);
  timeRollers[3] = makeRoller(timeScreen, 100, -14, 50, 3);
  lv_roller_set_options(timeRollers[3], "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec",
                        LV_ROLLER_MODE_INFINITE);
  timeRollers[4] = makeRoller(timeScreen, 110, 104, 50, 3);
  lv_roller_set_options(timeRollers[4], numberList(kFirstYear, kLastYear, false).c_str(),
                        LV_ROLLER_MODE_NORMAL);

  makeButton(timeScreen, "Cancel", -70, 165, 128, backHome, false);
  makeButton(timeScreen, "Set", 70, 165, 128, saveTime, true);
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
  const int year = lt.tm_year + 1900;
  lv_roller_set_selected(timeRollers[4], year < kFirstYear ? 0 : year > kLastYear ? kLastYear - kFirstYear : year - kFirstYear,
                         LV_ANIM_OFF);
  lv_scr_load(timeScreen);
}

// ---- Place: region -> zone -> city ---------------------------------------------

void showPickStep();

void pickBack(lv_event_t*) {
  if (pickStep == 0) lv_scr_load(homeScreen);
  else {
    --pickStep;
    showPickStep();
  }
}

void pickNextCb(lv_event_t*) {
  const uint16_t sel = lv_roller_get_selected(pickRoller);
  if (pickStep == 0) {
    chosenRegion = sel;
    pickStep = 1;
    showPickStep();
  } else if (pickStep == 1) {
    chosenZone = tz::zoneName(zonesInRegion[sel]);
    pickStep = 2;
    showPickStep();
  } else {
    setup_mode::applyZone(chosenZone.c_str());
    if (sel < cityCount) {
      const cities::Result& c = cityList[sel];
      char place[64];
      snprintf(place, sizeof place, "%s, %s", c.name, *c.region ? c.region : c.country);
      setup_mode::applyLocation(c.lat, c.lon, place);
      Serial.printf("setup: touch set place %s (%s)\n", place, chosenZone.c_str());
    }
    lv_scr_load(homeScreen);
  }
}

void buildPick() {
  pickScreen = makeScreen();
  pickTitle = makeLabel(pickScreen, "", lv_color_white(), -185);
  pickRoller = makeRoller(pickScreen, 380, 0, -5, 5);
  makeButton(pickScreen, "Back", -70, 165, 128, pickBack, false);
  pickNext = makeButton(pickScreen, "Next", 70, 165, 128, pickNextCb, true);

  // Regions are the first part of the zone names: Africa, America, Asia, Europe, ...
  for (int i = 0; i < tz::zoneCount(); ++i) {
    const std::string r = regionOf(tz::zoneName(i));
    if (regions.empty() || regions.back() != r) regions.push_back(r);  // table is sorted
  }
}

void setNextLabel(const char* text) { lv_label_set_text(lv_obj_get_child(pickNext, 0), text); }

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
      if (chosenRegion < 0 && regions[i] == mine) select = i;
    }
    if (chosenRegion >= 0) select = chosenRegion;
    setNextLabel("Next");
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
    setNextLabel("Next");
  } else {
    lv_label_set_text(pickTitle, "Nearest city");
    cityCount = cities::inZone(chosenZone.c_str(), cityList, kMaxCities);
    const char* place = settings::current().place;
    for (size_t i = 0; i < cityCount; ++i) {
      if (i) options += '\n';
      options += cityList[i].name;
      if (strncmp(place, cityList[i].name, strlen(cityList[i].name)) == 0) select = i;
    }
    if (cityCount == 0) options = "(keep current place)";
    setNextLabel("Save");
  }
  lv_roller_set_options(pickRoller, options.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(pickRoller, select, LV_ANIM_OFF);
}

void openPlace(lv_event_t*) {
  if (!pickScreen) buildPick();
  pickStep = 0;
  chosenRegion = -1;
  chosenZone.clear();
  showPickStep();
  lv_scr_load(pickScreen);
}

}  // namespace

void showHome(const char* ssid, const char* password, const char* wifiQr) {
  if (!homeScreen) buildHome();
  lv_qrcode_update(homeQr, wifiQr, strlen(wifiQr));
  lv_label_set_text_fmt(homeInfo, "%s\n%s", ssid, password);
  lv_scr_load(homeScreen);
}

bool onHome() { return homeScreen && lv_scr_act() == homeScreen; }

}  // namespace setup_screens
