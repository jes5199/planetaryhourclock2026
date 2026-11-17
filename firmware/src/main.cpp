// Planetary hour clock: centre-screen firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.43.
//
// Time comes from, in order of preference: NTP over Wi-Fi (if secrets.h provides
// credentials), the on-board PCF85063 RTC, or the firmware's build time.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "amoled.h"
#include "planetary.h"
#include "rtc.h"
#include "settings.h"
#include "sun.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"  // defines WIFI_SSID and WIFI_PASSWORD; not committed
#endif

namespace {

constexpr int kPinI2cSda = 47;
constexpr int kPinI2cScl = 48;

Settings settings = defaultSettings();
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
  setenv("TZ", settings.timezone, 1);
  tzset();

  // An RTC reading older than this firmware must be stale (e.g. the factory demo's clock).
  const time_t built = buildTime();
  time_t t;
  const bool haveRtc = rtc::begin();
  if (haveRtc && rtc::read(&t) && t >= built) {
    setSystemTime(t);
    Serial.println("time: from RTC");
  } else {
    setSystemTime(built);
    if (haveRtc) rtc::write(built);
    Serial.println("time: RTC unset or stale, using build time");
  }

#if defined(WIFI_SSID)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  sntp_set_time_sync_notification_cb([](timeval*) { ntpSynced = true; });
  configTzTime(settings.timezone, "pool.ntp.org", "time.nist.gov");
  Serial.println("time: NTP requested");
#endif
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

  initTime();

  amoled_init();
  ui::begin();
}

void loop() {
  static time_t lastShown = 0;
  static int lastLoggedMinute = -1;
  static int lastBrightness = -1;

  if (ntpSynced) {
    ntpSynced = false;
    rtc::write(time(nullptr));
    Serial.println("time: NTP synced, RTC updated");
  }

  const time_t now = time(nullptr);
  if (now != lastShown) {
    lastShown = now;
    const planetary::ClockState s = planetary::computeClock(now, utcOffset(now), settings.location);
    ui::update(s, now);

    const int brightness = s.isDay ? settings.brightnessDay : settings.brightnessNight;
    if (brightness != lastBrightness) {
      amoled_set_brightness(static_cast<uint8_t>(brightness));
      lastBrightness = brightness;
    }
    if (now / 60 != lastLoggedMinute) {
      lastLoggedMinute = now / 60;
      logState(s, now);
    }
  }

  lv_timer_handler();
  delay(5);
}
