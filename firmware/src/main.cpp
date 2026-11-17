// Planetary hour clock: centre-screen firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.43.
//
// Time comes from, in order of preference: the phone during setup (long-press the screen),
// NTP over Wi-Fi (if secrets.h provides credentials), the on-board PCF85063 RTC, or the
// firmware's build time.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "amoled.h"
#include "planetary.h"
#include "rtc.h"
#include "settings.h"
#include "setup_mode.h"
#include "setup_screens.h"
#include "sun.h"
#include "time_status.h"
#include "touch.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"  // defines WIFI_SSID and WIFI_PASSWORD; not committed
#endif

namespace {

constexpr int kPinI2cSda = 47;
constexpr int kPinI2cScl = 48;

volatile bool ntpSynced = false;

void setSystemTime(time_t utc) {
  timeval tv{utc, 0};
  settimeofday(&tv, nullptr);
}

// Firmware build time, interpreted as local wall time in the configured zone.
time_t buildTime() {
  static const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {0};
  struct tm t {};
  sscanf(__DATE__, "%3s %d %d", mon, &t.tm_mday, &t.tm_year);
  sscanf(__TIME__, "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec);
  t.tm_mon = (strstr(kMonths, mon) - kMonths) / 3;
  t.tm_year -= 1900;
  t.tm_isdst = -1;
  return mktime(&t);
}

// Local offset from UTC at `utc`, in seconds (newlib has no tm_gmtoff).
int32_t utcOffset(time_t utc) {
  struct tm lt;
  localtime_r(&utc, &lt);
  const int64_t local = planetary::daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
                        lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
  return static_cast<int32_t>(local - utc);
}

void initTime() {
  settings::applyTimezone();

  // An RTC reading older than this firmware must be stale (e.g. the factory demo's clock).
  const time_t built = buildTime();
  time_t t;
  const bool haveRtc = rtc::begin();
  const long lastGood = time_status::lastGood();
  const bool rtcOk = haveRtc && rtc::read(&t);
  Serial.printf("time: rtc present=%d readable=%d rtc=%ld build=%ld lastGood=%ld\n", haveRtc, rtcOk,
                rtcOk ? static_cast<long>(t) : 0L, static_cast<long>(built), lastGood);
  if (rtcOk && t >= built && t >= lastGood) {
    setSystemTime(t);
    time_status::setValid(true);
    Serial.println("time: from RTC");
  } else {
    // Keep the clock running from the build time, but flag it: the face shows
    // "SET TIME" and setup mode opens until a phone or NTP sets the real time.
    setSystemTime(built);
    time_status::setValid(false);
    Serial.println("time: RTC lost power or is stale; time not set");
  }

#if defined(WIFI_SSID)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  sntp_set_time_sync_notification_cb([](timeval*) { ntpSynced = true; });
  configTzTime(settings::current().tz, "pool.ntp.org", "time.nist.gov");
  Serial.println("time: NTP requested");
#endif
}

// LVGL input from the touch panel, in screen (rotated) coordinates.
void touchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  uint16_t px, py;
  if (touch::read(&px, &py)) {
    int x, y;
    amoled_panel_to_screen(px, py, &x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    setup_mode::touched();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void logState(const planetary::ClockState& s, time_t now) {
  struct tm lt;
  localtime_r(&now, &lt);
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d  h=%.3f %s  ruler=%s  day=%s night=%s  "
                "hour=%.1f min  gold=%.1f hand=%.1f led=%d\n",
                lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, s.h,
                s.isDay ? "day" : "night", planetary::planetName(s.ruler),
                planetary::planetName(s.dayRuler), planetary::planetName(s.nightRuler),
                s.hourLengthMin, s.goldHandDeg, s.hourHandDeg, s.dayRingLed);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Wire.begin(kPinI2cSda, kPinI2cScl);

  settings::load();
  initTime();

  amoled_init(settings::current().screenRotation);
  ui::begin();

  touch::begin();
  static lv_indev_drv_t indev;
  lv_indev_drv_init(&indev);
  indev.type = LV_INDEV_TYPE_POINTER;
  indev.read_cb = touchRead;
  lv_indev_drv_register(&indev);

  setup_mode::begin();
  // A new clock, or one whose RTC lost the time, opens setup by itself.
  if (!settings::saved() || !time_status::valid()) setup_mode::start();
}

void loop() {
  static time_t lastShown = 0;
  static int lastLoggedMinute = -1;
  static int lastBrightness = -1;

  if (ntpSynced) {
    ntpSynced = false;
    rtc::write(time(nullptr));
    time_status::setValid(true);
    time_status::saveLastGood(time(nullptr));
    Serial.println("time: NTP synced, RTC updated");
  }

  const time_t now = time(nullptr);
  if (now != lastShown) {
    lastShown = now;
    // Every 10 minutes, remember a known-good time (see time_status::lastGood).
    static time_t lastSaved = 0;
    if (time_status::valid() && now - lastSaved >= 600) {
      time_status::saveLastGood(now);
      lastSaved = now;
    }
    planetary::ClockState s = planetary::computeClock(now, utcOffset(now), settings::current().location);
#ifdef PREVIEW_RULER
    // Design preview: show a fixed ruling planet (Chaldean index, Saturn = 0 ... Moon = 6).
    s.ruler = static_cast<planetary::Planet>(PREVIEW_RULER);
#endif
    if (time_status::valid()) ui::update(s, now);
    else ui::showTimeNotSet();

    const Settings& cfg = settings::current();
    const int brightness = s.isDay ? cfg.brightnessDay : cfg.brightnessNight;
    if (brightness != lastBrightness) {
      amoled_set_brightness(static_cast<uint8_t>(brightness));
      lastBrightness = brightness;
    }
    if (now / 60 != lastLoggedMinute) {
      lastLoggedMinute = now / 60;
      logState(s, now);
    }
  }

  // Long-press on the clock face opens setup; on the setup home screen it closes it.
  // (Elsewhere in setup, long presses are left alone so scrolling isn't mistaken for one.)
  if (touch::longPress(1500)) {
    if (setup_mode::active() && setup_screens::onHome()) setup_mode::stop();
    else if (!setup_mode::active() && ui::onClock()) setup_mode::start();
  }
  setup_mode::loop();

  lv_timer_handler();
  delay(5);
}
