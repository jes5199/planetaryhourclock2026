#include "demo.h"

#include <Arduino.h>

#include <cstdio>

namespace demo {

namespace {

constexpr int64_t kWeek = 7LL * 24 * 3600;
constexpr int64_t kSpeed = kWeek / kWeekRealSeconds;  // simulated seconds per real second

bool running = false;
time_t startTime = 0;
uint32_t startMs = 0;
char bannerText[16];

}  // namespace

void start() {
  startTime = time(nullptr);
  startMs = millis();
  running = true;
  Serial.printf("demo: running a week in %u s (%lldx)\n", kWeekRealSeconds, static_cast<long long>(kSpeed));
}

void stop() {
  if (!running) return;
  running = false;
  Serial.println("demo: stopped");
}

bool active() {
  if (running && static_cast<int64_t>(millis() - startMs) * kSpeed / 1000 >= kWeek) stop();
  return running;
}

time_t now() {
  return startTime + static_cast<time_t>(static_cast<int64_t>(millis() - startMs) * kSpeed / 1000);
}

const char* banner() {
  static const char* const kDays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  const time_t t = now();
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(bannerText, sizeof bannerText, "DEMO %s", kDays[lt.tm_wday]);
  return bannerText;
}

}  // namespace demo
