// Host tests for the planetary-hour maths: `pio test -e native`
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "planetary.h"
#include "sun.h"

using namespace planetary;

static const Location kPortland{45.5152, -122.6784};
static const int32_t kPST = -8 * 3600;
static const int32_t kPDT = -7 * 3600;

// Unix seconds for a local wall-clock time at a fixed UTC offset.
static int64_t localTime(int y, int mo, int d, int hh, int mm, int32_t offset) {
  return daysFromCivil(y, mo, d) * 86400 + hh * 3600 + mm * 60 - offset;
}

static void assertNearSec(int64_t expected, int64_t actual, int64_t tolSec) {
  char msg[96];
  snprintf(msg, sizeof msg, "expected %lld, got %lld (diff %lld s)", (long long)expected,
           (long long)actual, (long long)(actual - expected));
  TEST_ASSERT_TRUE_MESSAGE(std::llabs(actual - expected) <= tolSec, msg);
}

static void assertAngle(double expected, double actual, double tol) {
  double diff = std::fmod(actual - expected + 540.0, 360.0) - 180.0;
  char msg[96];
  snprintf(msg, sizeof msg, "expected %.3f deg, got %.3f deg", expected, actual);
  TEST_ASSERT_TRUE_MESSAGE(std::fabs(diff) <= tol, msg);
}

void setUp() {}
void tearDown() {}

// --- Calendar ---------------------------------------------------------------

void test_weekdays() {
  TEST_ASSERT_EQUAL(4, weekdayOfEpochDay(0));                          // 1970-01-01 Thu
  TEST_ASSERT_EQUAL(1, weekdayOfEpochDay(daysFromCivil(2026, 11, 16)));  // Monday
  TEST_ASSERT_EQUAL(6, weekdayOfEpochDay(daysFromCivil(2026, 11, 14)));  // Saturday
}

// --- Sunrise / sunset (reference: timeanddate.com, +/- 2 min) --------

void test_portland_summer_solstice() {
  SunTimes t = sunTimes(daysFromCivil(2026, 6, 21), kPortland.latDeg, kPortland.lonDeg);
  assertNearSec(localTime(2026, 6, 21, 5, 22, kPDT), t.sunrise, 120);
  assertNearSec(localTime(2026, 6, 21, 21, 3, kPDT), t.sunset, 120);
}

void test_portland_winter_solstice() {
  SunTimes t = sunTimes(daysFromCivil(2026, 12, 21), kPortland.latDeg, kPortland.lonDeg);
  assertNearSec(localTime(2026, 12, 21, 7, 47, kPST), t.sunrise, 120);
  assertNearSec(localTime(2026, 12, 21, 16, 30, kPST), t.sunset, 120);
}

void test_greenwich_equinox() {
  SunTimes t = sunTimes(daysFromCivil(2026, 3, 20), 51.4769, 0.0);
  assertNearSec(localTime(2026, 3, 20, 6, 3, 0), t.sunrise, 120);
  assertNearSec(localTime(2026, 3, 20, 18, 13, 0), t.sunset, 120);
}

void test_polar_night_is_finite() {
  SunTimes t = sunTimes(daysFromCivil(2026, 12, 21), 69.65, 18.96);  // Tromso
  TEST_ASSERT_TRUE(t.polarNight);
  ClockState s = computeClock(localTime(2026, 12, 21, 12, 0, 3600), 3600, {69.65, 18.96});
  TEST_ASSERT_FALSE(s.isDay);
  TEST_ASSERT_TRUE(std::isfinite(s.h));
  TEST_ASSERT_TRUE(s.h >= 12.0 && s.h < 24.0);
}

// --- Planetary hours -------------------------------------------------------

void test_saturday_first_hour_is_saturn() {
  const int64_t day = daysFromCivil(2026, 11, 14);
  SunTimes t = sunTimes(day, kPortland.latDeg, kPortland.lonDeg);
  ClockState s = computeClock(t.sunrise + 1, kPST, kPortland);
  TEST_ASSERT_TRUE(s.isDay);
  TEST_ASSERT_EQUAL(0, s.hourIndex);
  TEST_ASSERT_EQUAL(6, s.planetaryWeekday);
  TEST_ASSERT_EQUAL((int)Planet::Saturn, (int)s.ruler);
  TEST_ASSERT_EQUAL((int)Planet::Saturn, (int)s.dayRuler);
  assertAngle(180.0, s.goldHandDeg, 0.5);  // start of Saturn's wedge (5th from top)
  assertAngle(270.0, s.hourHandDeg, 0.5);  // sunrise at the left
}

void test_monday_night_ruled_by_venus() {
  const int64_t day = daysFromCivil(2026, 11, 16);
  SunTimes t = sunTimes(day, kPortland.latDeg, kPortland.lonDeg);
  ClockState s = computeClock(t.sunset + 60, kPST, kPortland);
  TEST_ASSERT_FALSE(s.isDay);
  TEST_ASSERT_EQUAL(12, s.hourIndex);
  TEST_ASSERT_EQUAL((int)Planet::Moon, (int)s.dayRuler);
  TEST_ASSERT_EQUAL((int)Planet::Venus, (int)s.nightRuler);
  TEST_ASSERT_EQUAL((int)Planet::Venus, (int)s.ruler);
  assertAngle(90.0, s.hourHandDeg, 0.5);  // sunset at the right
}

void test_before_sunrise_belongs_to_previous_planetary_day() {
  ClockState s = computeClock(localTime(2026, 11, 16, 3, 0, kPST), kPST, kPortland);
  TEST_ASSERT_FALSE(s.isDay);
  TEST_ASSERT_EQUAL(0, s.planetaryWeekday);  // Sunday's planetary day
  TEST_ASSERT_EQUAL(1, s.dayRingWeekday);    // but past midnight: Monday on the day ring
  TEST_ASSERT_TRUE(s.h > 18.0 && s.h < 24.0);
}

void test_rulers_advance_in_chaldean_order_through_a_day() {
  const int64_t day = daysFromCivil(2026, 6, 21);  // Sunday, long summer day
  SunTimes t = sunTimes(day, kPortland.latDeg, kPortland.lonDeg);
  SunTimes next = sunTimes(day + 1, kPortland.latDeg, kPortland.lonDeg);
  int expected = (int)Planet::Sun;
  for (int i = 0; i < 24; ++i) {
    int64_t at;
    if (i < 12) at = t.sunrise + (t.sunset - t.sunrise) * (2 * i + 1) / 24;
    else at = t.sunset + (next.sunrise - t.sunset) * (2 * (i - 12) + 1) / 24;
    ClockState s = computeClock(at, kPDT, kPortland);
    TEST_ASSERT_EQUAL(i, s.hourIndex);
    TEST_ASSERT_EQUAL(expected, (int)s.ruler);
    TEST_ASSERT_TRUE(s.hourStart <= at && at < s.hourEnd);
    expected = (expected + 1) % 7;
  }
  // Next sunrise: Monday's first hour is the Moon's, continuing the sequence.
  ClockState mon = computeClock(next.sunrise + 1, kPDT, kPortland);
  TEST_ASSERT_EQUAL(expected, (int)mon.ruler);
  TEST_ASSERT_EQUAL((int)Planet::Moon, (int)mon.ruler);
}

void test_gold_hand_starts_each_day_at_its_rulers_wedge() {
  // Planet ring clockwise from the top: Sun, Venus, Mercury, Moon, Saturn, Jupiter, Mars.
  const int ringPos[7] = {/*Saturn*/ 4, /*Jupiter*/ 5, /*Mars*/ 6, /*Sun*/ 0,
                          /*Venus*/ 1,  /*Mercury*/ 2, /*Moon*/ 3};
  for (int d = 0; d < 7; ++d) {
    const int64_t day = daysFromCivil(2026, 11, 15) + d;
    SunTimes t = sunTimes(day, kPortland.latDeg, kPortland.lonDeg);
    ClockState s = computeClock(t.sunrise + 1, kPST, kPortland);
    const double wedge = 360.0 / 7.0;
    assertAngle(ringPos[(int)s.dayRuler] * wedge - wedge / 2, s.goldHandDeg, 0.5);
  }
}

void test_hour_hand_at_top_at_solar_noon() {
  const int64_t day = daysFromCivil(2026, 6, 21);
  SunTimes t = sunTimes(day, kPortland.latDeg, kPortland.lonDeg);
  ClockState s = computeClock((t.sunrise + t.sunset) / 2, kPDT, kPortland);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 6.0, s.h);
  assertAngle(0.0, s.hourHandDeg, 0.2);
  assertAngle(0.0, s.screenDayHandDeg, 0.2);
}

// --- Day ring LEDs ---------------------------------------------------------

void test_day_ring_led_pattern() {
  // Sunday is the top slice (LEDs 0..11): 3 night, 6 day, 3 night.
  const int64_t sun = daysFromCivil(2026, 11, 15);
  SunTimes t = sunTimes(sun, kPortland.latDeg, kPortland.lonDeg);
  TEST_ASSERT_EQUAL(3, computeClock(t.sunrise + 60, kPST, kPortland).dayRingLed);
  TEST_ASSERT_EQUAL(8, computeClock(t.sunset - 60, kPST, kPortland).dayRingLed);
  TEST_ASSERT_EQUAL(9, computeClock(t.sunset + 60, kPST, kPortland).dayRingLed);
  // Monday sits three slices on: LEDs 36..47.
  SunTimes m = sunTimes(sun + 1, kPortland.latDeg, kPortland.lonDeg);
  TEST_ASSERT_EQUAL(36 + 3, computeClock(m.sunrise + 60, kPST, kPortland).dayRingLed);
  TEST_ASSERT_EQUAL(36 + 2, computeClock(m.sunrise - 60, kPST, kPortland).dayRingLed);
}

// --- Screen ----------------------------------------------------------------

void test_screen_arc_matches_hour_length() {
  const int64_t day = daysFromCivil(2026, 6, 21);
  SunTimes t = sunTimes(day, kPortland.latDeg, kPortland.lonDeg);
  ClockState s = computeClock(t.sunrise + 3600, kPDT, kPortland);
  const double L = (t.sunset - t.sunrise) / 3600.0;
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 15.0 * L, s.arcSpanDeg);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, L * 60.0 / 12.0, s.hourLengthMin);
  assertAngle(-15.0 * L / 2.0, s.arcStartDeg, 0.01);
  // The screen hour hand is at the arc start at the beginning of each hour.
  ClockState h0 = computeClock(s.hourStart, kPDT, kPortland);
  assertAngle(h0.arcStartDeg, h0.screenHourHandDeg, 0.1);
  // At night the arc is centred at the bottom.
  ClockState n = computeClock(t.sunset + 3600, kPDT, kPortland);
  assertAngle(180.0, n.arcStartDeg + n.arcSpanDeg / 2.0, 0.01);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_weekdays);
  RUN_TEST(test_portland_summer_solstice);
  RUN_TEST(test_portland_winter_solstice);
  RUN_TEST(test_greenwich_equinox);
  RUN_TEST(test_polar_night_is_finite);
  RUN_TEST(test_saturday_first_hour_is_saturn);
  RUN_TEST(test_monday_night_ruled_by_venus);
  RUN_TEST(test_before_sunrise_belongs_to_previous_planetary_day);
  RUN_TEST(test_rulers_advance_in_chaldean_order_through_a_day);
  RUN_TEST(test_gold_hand_starts_each_day_at_its_rulers_wedge);
  RUN_TEST(test_hour_hand_at_top_at_solar_noon);
  RUN_TEST(test_day_ring_led_pattern);
  RUN_TEST(test_screen_arc_matches_hour_length);
  return UNITY_END();
}
