#include "setup_mode.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <sys/time.h>

#include <cmath>
#include <cstring>

#include "cities.h"
#include "rtc.h"
#include "settings.h"
#include "setup_page.h"
#include "time_status.h"
#include "tz.h"
#include "setup_screens.h"
#include "ui.h"

namespace setup_mode {

namespace {

constexpr uint32_t kTimeoutMs = 10 * 60 * 1000;
const IPAddress kIp(192, 168, 4, 1);

char ssid[24];
char password[12];
char wifiQr[96];
bool running = false;
uint32_t lastActivity = 0;
bool stopRequested = false;

DNSServer dns;
WebServer server(80);

// Letters and digits that can't be confused with each other when read off the screen.
void makePassword(char* out, size_t len) {
  static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (size_t i = 0; i < len; ++i) out[i] = kAlphabet[esp_random() % (sizeof kAlphabet - 1)];
  out[len] = '\0';
}

void jsonEscape(String& out, const char* s) {
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') out += '\\';
    if (static_cast<unsigned char>(*s) >= 0x20) out += *s;
  }
}

void handleState() {
  const Settings& c = settings::current();
  String j = "{\"time\":";
  j += String(static_cast<long>(time(nullptr)));
  j += ",\"lat\":" + String(c.location.latDeg, 5) + ",\"lon\":" + String(c.location.lonDeg, 5);
  j += ",\"tzName\":\"";
  jsonEscape(j, c.tzName);
  j += "\",\"place\":\"";
  jsonEscape(j, c.place);
  j += "\"}";
  server.send(200, "application/json", j);
}

void appendCity(String& j, const cities::Result& c) {
  j += "{\"n\":\"";
  jsonEscape(j, c.name);
  j += "\",\"r\":\"";
  jsonEscape(j, c.region);
  j += "\",\"c\":\"";
  jsonEscape(j, c.country);
  j += "\",\"lat\":" + String(c.lat, 4) + ",\"lon\":" + String(c.lon, 4) + ",\"tz\":\"";
  if (c.tzName) jsonEscape(j, c.tzName);
  j += "\"}";
}

// /api/cities?q=<lowercase ascii>&tz=<phone zone>: up to 8 matches.
// /api/cities?tz=<zone> with no q: the largest city in that zone, as a first guess.
void handleCities() {
  cities::Result r[8];
  size_t n;
  const String q = server.arg("q"), zone = server.arg("tz");
  if (q.length()) n = cities::search(q.c_str(), zone.c_str(), r, 8);
  else n = cities::largestIn(zone.c_str(), r) ? 1 : 0;
  String j = "[";
  for (size_t i = 0; i < n; ++i) {
    if (i) j += ',';
    appendCity(j, r[i]);
  }
  j += "]";
  server.send(200, "application/json", j);
}

// Copy UTF-8 text, cutting at a character boundary if it doesn't fit.
void copyUtf8(char* dst, size_t n, const char* src) {
  size_t len = strlen(src);
  if (len >= n) {
    len = n - 1;
    while (len > 0 && (static_cast<unsigned char>(src[len]) & 0xC0) == 0x80) --len;
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

void handleSet() {
  if (server.hasArg("tz")) {
    const String name = server.arg("tz");
    if (!applyZone(name.c_str())) {
      if (!server.hasArg("off")) {
        server.send(400, "text/plain", "unknown time zone");
        return;
      }
      // Unknown zone: keep the phone's current offset, without daylight saving.
      Settings& c = settings::current();
      tz::fixedOffset(server.arg("off").toInt(), c.tz, sizeof c.tz);
      snprintf(c.tzName, sizeof c.tzName, "UTC%+.2g (fixed)", server.arg("off").toInt() / 60.0);
      settings::applyTimezone();
      settings::save();
    }
  }
  if (server.hasArg("lat") && server.hasArg("lon")) {
    const double lat = server.arg("lat").toDouble(), lon = server.arg("lon").toDouble();
    if (!(std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0)) {
      server.send(400, "text/plain", "latitude/longitude out of range");
      return;
    }
    applyLocation(lat, lon, server.hasArg("place") ? server.arg("place").c_str() : "");
  }
  if (server.hasArg("time")) {
    const double ms = server.arg("time").toDouble();
    if (ms > 1.7e12) {  // after 2023: guards against garbage
      applyTime(static_cast<long>(ms / 1000), static_cast<long>(std::fmod(ms, 1000.0) * 1000));
    }
  }
  Serial.printf("setup: web set%s%s%s\n", server.hasArg("time") ? " time" : "",
                server.hasArg("tz") ? " tz" : "", server.hasArg("lat") ? " location" : "");

  if (server.hasArg("back")) {
    server.sendHeader("Location", "/?saved=1");
    server.send(302, "text/plain", "");
  } else {
    server.send(200, "text/plain", "ok");
  }
}

// Captive portal: every unknown URL (including the phones' connectivity checks)
// redirects to the setup page, which makes iOS and Android pop it up automatically.
void handleNotFound() {
  server.sendHeader("Location", String("http://") + kIp.toString() + "/");
  server.send(302, "text/plain", "");
}

}  // namespace

void begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(ssid, sizeof ssid, "Planetary %02X%02X", mac[4], mac[5]);

  Preferences p;
  p.begin("setup", false);
  if (p.getString("appass", password, sizeof password) != 8) {
    makePassword(password, 8);
    p.putString("appass", password);
  }
  p.end();
  snprintf(wifiQr, sizeof wifiQr, "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);

  server.on("/", [] {
    lastActivity = millis();
    server.send_P(200, "text/html", kSetupPage);
  });
  server.on("/api/state", [] { lastActivity = millis(); handleState(); });
  server.on("/set", [] { lastActivity = millis(); handleSet(); });
  server.on("/api/cities", [] { lastActivity = millis(); handleCities(); });
  server.on("/done", [] {
    server.send(200, "text/plain", "ok");
    stopRequested = true;
  });
  server.onNotFound(handleNotFound);
}

void start() {
  if (running) return;
  WiFi.mode(WiFi.getMode() == WIFI_STA ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(kIp, kIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", kIp);
  server.begin();
  running = true;
  stopRequested = false;
  lastActivity = millis();
  setup_screens::showHome(ssid, password, wifiQr);
  Serial.printf("setup: hotspot \"%s\" up\n", ssid);
}

void stop() {
  if (!running) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WiFi.getMode() == WIFI_AP_STA ? WIFI_STA : WIFI_OFF);
  running = false;
  ui::showClock();
  Serial.println("setup: hotspot down");
}

bool active() { return running; }

void touched() { lastActivity = millis(); }

void applyTime(long unixSeconds, long micros) {
  timeval tv{static_cast<time_t>(unixSeconds), static_cast<suseconds_t>(micros)};
  settimeofday(&tv, nullptr);
  rtc::write(tv.tv_sec);
  time_status::setValid(true);
  time_status::saveLastGood(tv.tv_sec);
}

bool applyZone(const char* ianaName) {
  const char* posix = tz::posixFor(ianaName);
  if (!posix) return false;
  Settings& c = settings::current();
  strlcpy(c.tz, posix, sizeof c.tz);
  strlcpy(c.tzName, ianaName, sizeof c.tzName);
  settings::applyTimezone();
  settings::save();
  return true;
}

void applyLocation(double lat, double lon, const char* place) {
  Settings& c = settings::current();
  c.location = {lat, lon};
  copyUtf8(c.place, sizeof c.place, place ? place : "");
  settings::save();
}

void loop() {
  if (!running) return;
  dns.processNextRequest();
  server.handleClient();
  if (stopRequested || millis() - lastActivity > kTimeoutMs) stop();
}

}  // namespace setup_mode
