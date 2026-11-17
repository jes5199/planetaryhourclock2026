#include "ui.h"

#include <cmath>
#include <cstdio>

#include "amoled.h"
#include "lvgl.h"

namespace ui {

namespace {

constexpr int kCx = AMOLED_WIDTH / 2;
constexpr int kCy = AMOLED_HEIGHT / 2;
constexpr int kArcRadius = 214;  // centreline of the hour arc
constexpr int kArcWidth = 14;
constexpr int kDayHandIn = 194, kDayHandOut = 230;    // short and thick
constexpr int kHourHandIn = 172, kHourHandOut = 230;  // long and thin
constexpr int kDotSize = 12;

const lv_color_t kDayArc = lv_color_hex(0xE8A33A);    // amber
const lv_color_t kNightArc = lv_color_hex(0x5A4FCF);  // indigo, bright enough on AMOLED
const lv_color_t kDayText = lv_color_hex(0xF2B84B);
const lv_color_t kNightText = lv_color_hex(0xA9C8FF);  // pale blue
const lv_color_t kTrack = lv_color_hex(0x202020);
const lv_color_t kDayHand = lv_color_hex(0xFFFFFF);
const lv_color_t kHourHand = lv_color_hex(0xFFF1C4);

lv_obj_t* arc;
lv_obj_t* dayHand;
lv_obj_t* hourHand;
lv_obj_t* hourDot;
lv_obj_t* textBox;
lv_obj_t* timeLabel;
lv_obj_t* rulerLabel;
lv_obj_t* spanLabel;
lv_obj_t* lengthLabel;
lv_point_t dayHandPts[2];
lv_point_t hourHandPts[2];

// Our angles are clockwise from the top; LVGL's are clockwise from 3 o'clock.
uint16_t toLv(double deg) {
  int a = static_cast<int>(std::lround(deg + 270.0)) % 360;
  return static_cast<uint16_t>(a < 0 ? a + 360 : a);
}

lv_point_t polar(double deg, int radius) {
  const double r = deg * M_PI / 180.0;
  return {static_cast<lv_coord_t>(kCx + radius * std::sin(r)),
          static_cast<lv_coord_t>(kCy - radius * std::cos(r))};
}

lv_obj_t* makeArc(int radius, int width, lv_color_t color) {
  lv_obj_t* a = lv_arc_create(lv_scr_act());
  lv_obj_set_size(a, 2 * radius + width, 2 * radius + width);
  lv_obj_center(a);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
  lv_obj_set_style_pad_all(a, 0, LV_PART_MAIN);
  return a;
}

lv_obj_t* makeHand(lv_point_t* pts, int width, lv_color_t color) {
  lv_obj_t* l = lv_line_create(lv_scr_act());
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_color(l, color, 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  lv_line_set_points(l, pts, 2);
  return l;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int y) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  lv_label_set_text(l, "");
  return l;
}

void setHand(lv_obj_t* line, lv_point_t* pts, double deg, int r1, int r2) {
  pts[0] = polar(deg, r1);
  pts[1] = polar(deg, r2);
  lv_line_set_points(line, pts, 2);
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
  lv_obj_t* track = makeArc(kArcRadius, 2, kTrack);
  lv_arc_set_bg_angles(track, 0, 360);

  arc = makeArc(kArcRadius, kArcWidth, kDayArc);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

  // Like a watch: the slow day hand is short and thick (an hour hand),
  // the fast hour hand is long and thin with a dot (a minute hand).
  dayHand = makeHand(dayHandPts, 12, kDayHand);
  hourHand = makeHand(hourHandPts, 3, kHourHand);
  hourDot = lv_obj_create(scr);
  lv_obj_set_size(hourDot, kDotSize, kDotSize);
  lv_obj_set_style_radius(hourDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hourDot, kHourHand, 0);
  lv_obj_set_style_border_width(hourDot, 0, 0);
  lv_obj_clear_flag(hourDot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  // Text lives in a transparent box so it can drift a few pixels against burn-in.
  textBox = lv_obj_create(scr);
  lv_obj_set_size(textBox, 340, 300);
  lv_obj_center(textBox);
  lv_obj_set_style_bg_opa(textBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(textBox, 0, 0);
  lv_obj_set_style_pad_all(textBox, 0, 0);
  lv_obj_clear_flag(textBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  timeLabel = makeLabel(textBox, &lv_font_montserrat_48, lv_color_white(), -64);
  rulerLabel = makeLabel(textBox, &lv_font_montserrat_40, kDayText, -8);
  spanLabel = makeLabel(textBox, &lv_font_montserrat_28, lv_color_hex(0xC8C8C8), 40);
  lengthLabel = makeLabel(textBox, &lv_font_montserrat_28, lv_color_hex(0x9A9A9A), 78);
}

void update(const planetary::ClockState& s, time_t now) {
  char buf[48], a[8], b[8];

  formatHm(now, buf, sizeof buf);
  lv_label_set_text(timeLabel, buf);

  lv_label_set_text(rulerLabel, planetary::planetName(s.ruler));
  lv_obj_set_style_text_color(rulerLabel, s.isDay ? kDayText : kNightText, 0);

  formatHm(s.hourStart, a, sizeof a);
  formatHm(s.hourEnd, b, sizeof b);
  snprintf(buf, sizeof buf, "%s - %s", a, b);
  lv_label_set_text(spanLabel, buf);
  snprintf(buf, sizeof buf, "%d min", static_cast<int>(std::lround(s.hourLengthMin)));
  lv_label_set_text(lengthLabel, buf);

  // Hour arc.
  lv_obj_set_style_arc_color(arc, s.isDay ? kDayArc : kNightArc, LV_PART_MAIN);
  const uint16_t start = toLv(s.arcStartDeg);
  const uint16_t end = toLv(s.arcStartDeg + s.arcSpanDeg);
  lv_arc_set_bg_angles(arc, start, end);

  // Virtual hands crossing the arc.
  setHand(dayHand, dayHandPts, s.screenDayHandDeg, kDayHandIn, kDayHandOut);
  setHand(hourHand, hourHandPts, s.screenHourHandDeg, kHourHandIn, kHourHandOut);
  const lv_point_t dot = polar(s.screenHourHandDeg, kHourHandIn);
  lv_obj_set_pos(hourDot, dot.x - kDotSize / 2, dot.y - kDotSize / 2);

  // Burn-in protection: drift the text through a small square, one step per minute.
  static const int8_t kDrift[8][2] = {{0, 0}, {3, 0}, {3, 3}, {0, 3}, {-3, 3}, {-3, 0}, {-3, -3}, {0, -3}};
  const int step = static_cast<int>((now / 60) % 8);
  lv_obj_align(textBox, LV_ALIGN_CENTER, kDrift[step][0], kDrift[step][1]);
}

}  // namespace ui
