// Setup mode: the clock opens its own Wi-Fi hotspot (WPA2, password unique to each clock)
// with a captive portal, shows a QR code to join it, and serves a page that sets the
// time, time zone and location from the phone.
//
// HTTP API (also the landing point for the online GPS page):
//   GET /set?time=<unix ms>&tz=<IANA>&off=<minutes east>&lat=<deg>&lon=<deg>&place=<text>
//       Any subset. With &back=1 it redirects to the setup page instead of replying "ok".
//   GET /api/state   current settings and clock time as JSON
//   GET /done        leave setup mode
#pragma once

namespace setup_mode {

void begin();          // after settings::load(); creates this clock's hotspot name/password
void start();
void stop();
bool active();
const char* deviceName();  // "Planetary 234C": the hotspot and Bluetooth name
void loop();           // call every loop() iteration

// Shared by the web page and the touchscreen screens. Each saves to flash.
void applyTime(long unixSeconds, long micros = 0);   // also sets the RTC
bool applyZone(const char* ianaName);                 // false if the zone is unknown
void applyLocation(double lat, double lon, const char* place);
void touched();        // the touchscreen was used: postpone the timeout

}  // namespace setup_mode
