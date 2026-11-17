// The centre screen: civil time, the hour's ruler, the hour's start/length/end,
// and the hour arc with its two virtual hands.
#pragma once

#include <ctime>

#include "planetary.h"

namespace ui {

void begin();
void update(const planetary::ClockState& s, time_t now);

// Setup mode: a Wi-Fi QR code (joins the clock's hotspot) with the network name and password.
void showSetup(const char* ssid, const char* password, const char* wifiQr);
void showClock();

}  // namespace ui
