#include "ui.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "amoled.h"
#include "lvgl.h"

extern "C" {
LV_FONT_DECLARE(font_time_96)  // Montserrat Medium 96 px, digits and colon (src/fonts)
}

namespace ui {

namespace {

constexpr int kCx = AMOLED_WIDTH / 2;
constexpr int kCy = AMOLED_HEIGHT / 2;
constexpr int kArcRadius = 212;  // centreline of the hour arc
constexpr int kArcWidth = 14;
constexpr int kSunSize = 34;                          // the day hand: a sun (or moon) riding the arc
constexpr int kHourHandIn = 188, kHourHandOut = 231;  // the hour hand: a thin needle across the arc
constexpr int kDotSize = 12;

struct Palette {
  lv_color_t arc;
  lv_color_t sun;        // the day hand
};
const Palette kDay{lv_color_hex(0xE8A33A), lv_color_hex(0xFFE066)};
const Palette kNight{lv_color_hex(0x5A4FCF), lv_color_hex(0xDCE6FF)};
// Planet colours from jes5199/dirigible (app/assets/stylesheets/planets.css), in Chaldean
// order. Saturn uses dirigible's lifted #A9A9A9, since #696969 nearly vanishes on black.
const lv_color_t kPlanetColor[7] = {
    lv_color_hex(0xA9A9A9),  // Saturn
    lv_color_hex(0x9370DB),  // Jupiter
    lv_color_hex(0xFF6347),  // Mars
    lv_color_hex(0xFFD700),  // Sun
    lv_color_hex(0x90EE90),  // Venus
    lv_color_hex(0x87CEEB),  // Mercury
    lv_color_hex(0xFFFFFF),  // Moon
};
const lv_color_t kHourHand = lv_color_hex(0xFFFFFF);
const lv_color_t kTrack = lv_color_hex(0x1A1A1A);

lv_obj_t* arc;
lv_obj_t* sun;
lv_obj_t* hourHand;
lv_obj_t* hourDot;
lv_obj_t* textBox;
lv_obj_t* timeLabel;
lv_obj_t* rulerLabel;
lv_obj_t* spanLabel;
lv_obj_t* lengthLabel;
lv_obj_t* countdownLabel;
lv_point_t hourHandPts[2];

// Dial angles are clockwise from the top (sunrise left, noon top, sunset right, midnight
// bottom); LVGL's are clockwise from 3 o'clock. The display driver's rotation already
// makes LVGL's frame the viewer's frame, so nothing else is needed here.
uint16_t toLv(double deg) {
  int a = static_cast<int>(std::lround(deg + 270.0)) % 360;
  return static_cast<uint16_t>(a < 0 ? a + 360 : a);
}

lv_point_t polar(double deg, int radius) {
  const double r = deg * M_PI / 180.0;
  return {static_cast<lv_coord_t>(std::lround(kCx + radius * std::sin(r))),
          static_cast<lv_coord_t>(std::lround(kCy - radius * std::cos(r)))};
}

lv_obj_t* makeArc(int radius, int width) {
  lv_obj_t* a = lv_arc_create(lv_scr_act());
  lv_obj_set_size(a, 2 * radius + width, 2 * radius + width);
  lv_obj_center(a);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(a, 0, LV_PART_MAIN);
  for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
    lv_obj_set_style_arc_width(a, width, part);
    lv_obj_set_style_arc_rounded(a, true, part);
  }
  lv_arc_set_mode(a, LV_ARC_MODE_NORMAL);
  lv_arc_set_range(a, 0, 1000);
  return a;
}

lv_obj_t* makeDisc(int size, lv_color_t color, int border) {
  lv_obj_t* d = lv_obj_create(lv_scr_act());
  lv_obj_set_size(d, size, size);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, color, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(d, border, 0);
  lv_obj_set_style_border_color(d, lv_color_black(), 0);
  lv_obj_set_style_pad_all(d, 0, 0);
  lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return d;
}

void placeDisc(lv_obj_t* d, int size, double deg, int radius) {
  const lv_point_t p = polar(deg, radius);
  lv_obj_set_pos(d, p.x - size / 2, p.y - size / 2);
}

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int y) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  lv_label_set_text(l, "");
  return l;
}

void formatHm(time_t t, char* out, size_t n) {
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

}  // namespace

void begin() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // A faint full ring: the hour arc fills exactly half of it at the equinox.
  lv_obj_t* track = makeArc(kArcRadius, 2);
  lv_obj_set_style_arc_color(track, kTrack, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(track, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(track, 0, 360);

  // The hour arc: one colour for the whole day (or night).
  arc = makeArc(kArcRadius, kArcWidth);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

  // The day hand is the sun riding the arc; a black rim lifts it off the arc.
  sun = makeDisc(kSunSize, kDay.sun, 3);

  // The hour hand is the only line on the screen: a thin needle with a dot inside the arc.
  hourHand = lv_line_create(scr);
  lv_obj_set_style_line_width(hourHand, 4, 0);
  lv_obj_set_style_line_color(hourHand, kHourHand, 0);
  lv_obj_set_style_line_rounded(hourHand, true, 0);
  hourDot = makeDisc(kDotSize, kHourHand, 0);

  // Text lives in a transparent box so it can drift a few pixels against burn-in.
  textBox = lv_obj_create(scr);
  lv_obj_set_size(textBox, 360, 330);
  lv_obj_center(textBox);
  lv_obj_set_style_bg_opa(textBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(textBox, 0, 0);
  lv_obj_set_style_pad_all(textBox, 0, 0);
  lv_obj_clear_flag(textBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  timeLabel = makeLabel(textBox, &font_time_96, lv_color_white(), -70);
  rulerLabel = makeLabel(textBox, &lv_font_montserrat_48, lv_color_white(), -4);
  spanLabel = makeLabel(textBox, &lv_font_montserrat_48, lv_color_hex(0xC8C8C8), 46);
  lengthLabel = makeLabel(textBox, &lv_font_montserrat_48, lv_color_hex(0x9A9A9A), 96);
  countdownLabel = makeLabel(textBox, &lv_font_montserrat_28, lv_color_hex(0x707070), 140);
}

void update(const planetary::ClockState& s, time_t now) {
  const Palette& pal = s.isDay ? kDay : kNight;
  char buf[48], a[8], b[8];

  formatHm(now, buf, sizeof buf);
  lv_label_set_text(timeLabel, buf);

  lv_label_set_text(rulerLabel, planetary::planetName(s.ruler));
  lv_obj_set_style_text_color(rulerLabel, kPlanetColor[static_cast<int>(s.ruler)], 0);

  formatHm(s.hourStart, a, sizeof a);
  formatHm(s.hourEnd, b, sizeof b);
  snprintf(buf, sizeof buf, "%s-%s", a, b);
  lv_label_set_text(spanLabel, buf);
  snprintf(buf, sizeof buf, "%d min", static_cast<int>(std::lround(s.hourLengthMin)));
  lv_label_set_text(lengthLabel, buf);
  // Time left in this hour, counting down (minutes can pass 59 in a long hour).
  const long left = s.hourEnd > now ? static_cast<long>(s.hourEnd - now) : 0;
  snprintf(buf, sizeof buf, "%ld:%02ld left", left / 60, left % 60);
  lv_label_set_text(countdownLabel, buf);

  lv_obj_set_style_arc_color(arc, pal.arc, LV_PART_MAIN);
  lv_arc_set_bg_angles(arc, toLv(s.arcStartDeg), toLv(s.arcStartDeg + s.arcSpanDeg));

  lv_obj_set_style_bg_color(sun, pal.sun, 0);
  placeDisc(sun, kSunSize, s.screenDayHandDeg, kArcRadius);

  hourHandPts[0] = polar(s.screenHourHandDeg, kHourHandIn);
  hourHandPts[1] = polar(s.screenHourHandDeg, kHourHandOut);
  lv_line_set_points(hourHand, hourHandPts, 2);
  placeDisc(hourDot, kDotSize, s.screenHourHandDeg, kHourHandIn);

  // Burn-in protection: drift the text through a small square, one step per minute.
  static const int8_t kDrift[8][2] = {{0, 0}, {3, 0}, {3, 3}, {0, 3}, {-3, 3}, {-3, 0}, {-3, -3}, {0, -3}};
  const int step = static_cast<int>((now / 60) % 8);
  lv_obj_align(textBox, LV_ALIGN_CENTER, kDrift[step][0], kDrift[step][1]);
}

}  // namespace ui
