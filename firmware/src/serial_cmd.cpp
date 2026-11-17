#include "serial_cmd.h"

#include <Arduino.h>

#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "ble_time.h"
#include "cities.h"
#include "demo.h"
#include "planetary.h"
#include "settings.h"
#include "setup_mode.h"
#include "sun.h"
#include "time_status.h"
#include "tz.h"

namespace serial_cmd {

namespace {

constexpr const char* kHelp = R"(
== Planetary hour clock: serial commands (115200 baud, one command per line) ==
Replies start with "ok:" or "error:". Times are stored in UTC; the time zone decides local time.

  help                          this text
  status                        current time, zone, place, planetary hour, as one JSON line

  time <unix seconds>           set the clock, e.g.  time 1794886714   (on a Mac/Linux host: date +%s)
  time <ISO 8601>               e.g.  time 2026-11-16T19:40:00Z   or   time 2026-11-16T11:40:00-08:00
  time <YYYY-MM-DD HH:MM[:SS]>  local wall-clock time in the clock's current time zone

  tz <IANA zone>                e.g.  tz America/Los_Angeles
  loc <lat> <lon> [place name]  degrees, north and east positive, e.g.  loc 45.5152 -122.6784 Portland, Oregon
  city <search text>            list up to 8 matching cities, numbered (largest first)
  city <n>                      use match n from the last list: sets location, place name and time zone

  setup open | setup close      show or hide the setup screen (Wi-Fi hotspot + QR code)
  demo                          start or stop a demo: one week in 3 minutes (also: triple-tap the clock)
  ble pair                      open Bluetooth pairing for 2 minutes (iPhone: Settings > Bluetooth)
  ble forget                    forget all paired phones
  reboot                        restart the clock

Example, from a computer whose clock and zone are right:
  tz America/Los_Angeles
  time 1794886714
  city portland        ->  1: Portland, Oregon, United States ...
  city 1
)";

char line[160];
size_t lineLen = 0;
cities::Result lastCities[8];
size_t lastCityCount = 0;
volatile bool hostConnected = false;

void reply(bool ok, const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.printf("%s %s\n", ok ? "ok:" : "error:", buf);
}

void jsonString(const char* s) {
  Serial.print('"');
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') Serial.print('\\');
    if (static_cast<unsigned char>(*s) >= 0x20) Serial.print(*s);
  }
  Serial.print('"');
}

void printStatus() {
  const Settings& c = settings::current();
  const time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  char local[32];
  strftime(local, sizeof local, "%Y-%m-%dT%H:%M:%S", &lt);
  const int64_t localSecs = planetary::daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 +
                            lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
  const int32_t offset = static_cast<int32_t>(localSecs - now);
  const planetary::ClockState s = planetary::computeClock(now, offset, c.location);
  Serial.printf("{\"unix\":%ld,\"local\":\"%s\",\"utc_offset_s\":%ld,\"time_valid\":%s,\"tz\":", static_cast<long>(now),
                local, static_cast<long>(offset), time_status::valid() ? "true" : "false");
  jsonString(c.tzName);
  Serial.print(",\"place\":");
  jsonString(c.place);
  Serial.printf(",\"lat\":%.4f,\"lon\":%.4f,\"is_day\":%s,\"ruler\":\"%s\",\"hour_index\":%d,"
                "\"hour_length_min\":%.1f,\"hour_ends_unix\":%ld,\"setup_open\":%s,\"ble_paired\":%s}\n",
                c.location.latDeg, c.location.lonDeg, s.isDay ? "true" : "false", planetary::planetName(s.ruler),
                s.hourIndex, s.hourLengthMin, static_cast<long>(s.hourEnd), setup_mode::active() ? "true" : "false",
                ble_time::paired() ? "true" : "false");
}

// Parses "2026-11-16T19:40:00Z", "...+05:30", or "2026-11-16 11:40[:00]" (local).
bool parseDateTime(const char* s, long* utc) {
  int y, mo, d, h, mi, sec = 0;
  char sep;
  int n = 0;
  if (sscanf(s, "%d-%d-%d%c%d:%d%n", &y, &mo, &d, &sep, &h, &mi, &n) < 6) return false;
  const char* rest = s + n;
  if (*rest == ':') {
    int used = 0;
    if (sscanf(rest, ":%d%n", &sec, &used) != 1) return false;
    rest += used;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return false;
  const long naive = static_cast<long>(planetary::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec);
  if (*rest == 'Z' || *rest == 'z') {
    *utc = naive;
  } else if (*rest == '+' || *rest == '-') {
    int oh = 0, om = 0;
    if (sscanf(rest + 1, "%d:%d", &oh, &om) < 1) return false;
    const long off = (oh * 3600L + om * 60L) * (*rest == '-' ? -1 : 1);
    *utc = naive - off;
  } else {
    struct tm t {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = sec;
    t.tm_isdst = -1;
    *utc = static_cast<long>(mktime(&t));  // local time in the clock's zone
  }
  return true;
}

void cmdTime(const char* arg) {
  long utc;
  char* end;
  const long n = strtol(arg, &end, 10);
  if (*arg && *end == '\0') {
    utc = n > 100000000000L ? n / 1000 : n;  // also accept milliseconds
  } else if (!parseDateTime(arg, &utc)) {
    reply(false, "time: expected unix seconds, ISO 8601, or 'YYYY-MM-DD HH:MM[:SS]'");
    return;
  }
  if (utc < 1700000000L) {
    reply(false, "time: %ld is before 2023, refusing", utc);
    return;
  }
  setup_mode::applyTime(utc);
  const time_t t = utc;
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[32];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &lt);
  reply(true, "time set to %ld (local %s, %s)", utc, buf, settings::current().tzName);
}

void cmdTz(const char* arg) {
  if (!setup_mode::applyZone(arg)) {
    reply(false, "tz: unknown zone '%s' (use an IANA name like Europe/London)", arg);
    return;
  }
  reply(true, "time zone %s (%s)", settings::current().tzName, settings::current().tz);
}

void cmdLoc(const char* arg) {
  char* end;
  const double lat = strtod(arg, &end);
  if (end == arg) return reply(false, "loc: expected <lat> <lon> [place name]");
  const char* p = end;
  const double lon = strtod(p, &end);
  if (end == p) return reply(false, "loc: expected <lat> <lon> [place name]");
  if (std::fabs(lat) > 90 || std::fabs(lon) > 180) return reply(false, "loc: out of range");
  while (*end == ' ' || *end == ',') ++end;
  setup_mode::applyLocation(lat, lon, end);
  reply(true, "location %.4f, %.4f \"%s\"", lat, lon, settings::current().place);
}

void cmdCity(const char* arg) {
  char* end;
  const long pick = strtol(arg, &end, 10);
  if (*arg && *end == '\0') {
    if (pick < 1 || pick > static_cast<long>(lastCityCount)) return reply(false, "city: no match %ld; search first", pick);
    const cities::Result& c = lastCities[pick - 1];
    if (c.tzName) setup_mode::applyZone(c.tzName);
    char place[64];
    snprintf(place, sizeof place, "%s, %s", c.name, *c.region ? c.region : c.country);
    setup_mode::applyLocation(c.lat, c.lon, place);
    return reply(true, "place %s (%.4f, %.4f), time zone %s", place, c.lat, c.lon, settings::current().tzName);
  }
  // Search: lowercase ASCII, like the phone page.
  char q[64];
  size_t n = 0;
  for (const char* p = arg; *p && n < sizeof q - 1; ++p) q[n++] = static_cast<char>(tolower(*p));
  q[n] = '\0';
  lastCityCount = cities::search(q, settings::current().tzName, lastCities, 8);
  if (lastCityCount == 0) return reply(false, "city: nothing matches '%s'", arg);
  for (size_t i = 0; i < lastCityCount; ++i) {
    const cities::Result& c = lastCities[i];
    Serial.printf("  %u: %s, %s, %s  (%.4f, %.4f) %s\n", static_cast<unsigned>(i + 1), c.name, c.region, c.country,
                  c.lat, c.lon, c.tzName ? c.tzName : "");
  }
  reply(true, "%u matches; choose with: city <n>", static_cast<unsigned>(lastCityCount));
}

void handle(char* cmd) {
  // Trim.
  while (*cmd == ' ' || *cmd == '\t') ++cmd;
  for (char* e = cmd + strlen(cmd); e > cmd && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\t'); --e) e[-1] = '\0';
  if (!*cmd) return;
  char* arg = strchr(cmd, ' ');
  if (arg) {
    *arg++ = '\0';
    while (*arg == ' ') ++arg;
  } else {
    arg = cmd + strlen(cmd);
  }

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) Serial.print(kHelp);
  else if (!strcmp(cmd, "status")) printStatus();
  else if (!strcmp(cmd, "time")) cmdTime(arg);
  else if (!strcmp(cmd, "tz")) cmdTz(arg);
  else if (!strcmp(cmd, "loc")) cmdLoc(arg);
  else if (!strcmp(cmd, "city")) cmdCity(arg);
  else if (!strcmp(cmd, "setup")) {
    if (!strcmp(arg, "open")) { setup_mode::start(); reply(true, "setup open"); }
    else if (!strcmp(arg, "close")) { setup_mode::stop(); reply(true, "setup closed"); }
    else reply(false, "setup: open or close");
  } else if (!strcmp(cmd, "ble")) {
    if (!strcmp(arg, "pair")) { ble_time::startPairing(); reply(true, "pairing open for 2 minutes"); }
    else if (!strcmp(arg, "forget")) { ble_time::forget(); reply(true, "forgot all paired phones"); }
    else reply(false, "ble: pair or forget");
  } else if (!strcmp(cmd, "demo")) {
    if (demo::active()) { demo::stop(); reply(true, "demo stopped"); }
    else { demo::start(); reply(true, "demo started: one week in %u s", demo::kWeekRealSeconds); }
  } else if (!strcmp(cmd, "reboot")) {
    reply(true, "rebooting");
    Serial.flush();
    delay(100);
    ESP.restart();
  } else {
    reply(false, "unknown command '%s'; send 'help'", cmd);
  }
}

}  // namespace

void begin() {
  Serial.onEvent(ARDUINO_HW_CDC_CONNECTED_EVENT, [](void*, esp_event_base_t, int32_t, void*) { hostConnected = true; });
  Serial.print(kHelp);
}

void loop() {
  if (hostConnected) {
    hostConnected = false;
    Serial.print(kHelp);
  }
  static uint32_t lastHint = 0;
  if (millis() - lastHint > 10UL * 60 * 1000) {
    lastHint = millis();
    Serial.println("(serial commands available: send 'help')");
  }
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      line[lineLen] = '\0';
      if (lineLen) handle(line);
      lineLen = 0;
    } else if (lineLen < sizeof line - 1) {
      line[lineLen++] = ch;
    }
  }
}

}  // namespace serial_cmd
