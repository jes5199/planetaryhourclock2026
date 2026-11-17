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

#include "rtc.h"
#include "settings.h"
#include "setup_page.h"
#include "tz.h"
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

void handleSet() {
  Settings& c = settings::current();
  bool changed = false;

  if (server.hasArg("tz")) {
    const String name = server.arg("tz");
    if (const char* posix = tz::posixFor(name.c_str())) {
      strlcpy(c.tz, posix, sizeof c.tz);
      strlcpy(c.tzName, name.c_str(), sizeof c.tzName);
    } else if (server.hasArg("off")) {
      // Unknown zone: keep the phone's current offset, without daylight saving.
      tz::fixedOffset(server.arg("off").toInt(), c.tz, sizeof c.tz);
      snprintf(c.tzName, sizeof c.tzName, "UTC%+.2g (fixed)", server.arg("off").toInt() / 60.0);
    } else {
      server.send(400, "text/plain", "unknown time zone");
      return;
    }
    settings::applyTimezone();
    changed = true;
  }
  if (server.hasArg("lat") && server.hasArg("lon")) {
    const double lat = server.arg("lat").toDouble(), lon = server.arg("lon").toDouble();
    if (!(std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0)) {
      server.send(400, "text/plain", "latitude/longitude out of range");
      return;
    }
    c.location = {lat, lon};
    strlcpy(c.place, server.hasArg("place") ? server.arg("place").c_str() : "", sizeof c.place);
    changed = true;
  }
  if (server.hasArg("time")) {
    const double ms = server.arg("time").toDouble();
    if (ms > 1.7e12) {  // after 2023: guards against garbage
      timeval tv{static_cast<time_t>(ms / 1000), static_cast<suseconds_t>(std::fmod(ms, 1000.0) * 1000)};
      settimeofday(&tv, nullptr);
      rtc::write(tv.tv_sec);
    }
  }
  if (changed) settings::save();
  Serial.printf("setup: set%s%s%s\n", server.hasArg("time") ? " time" : "",
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
  ui::showSetup(ssid, password, wifiQr);
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

void loop() {
  if (!running) return;
  dns.processNextRequest();
  server.handleClient();
  if (stopRequested || millis() - lastActivity > kTimeoutMs) stop();
}

}  // namespace setup_mode
