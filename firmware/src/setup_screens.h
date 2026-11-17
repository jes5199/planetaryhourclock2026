// Setup screens on the clock itself: the home screen (Wi-Fi QR code plus Time and Place
// buttons), a date/time picker, and a region -> time zone -> city picker. Everything can be
// set with the touchscreen alone; the QR code is the phone route.
#pragma once

namespace setup_screens {

void showHome(const char* ssid, const char* password, const char* wifiQr);
bool onHome();  // true while the home (QR) screen is showing

}  // namespace setup_screens
