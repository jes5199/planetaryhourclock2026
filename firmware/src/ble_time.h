// Set the clock from an iPhone over Bluetooth LE, the way many bedside clocks do.
//
// iOS offers the standard Current Time Service (0x1805) to Bluetooth accessories it has
// paired with. After a one-time pairing (pairing mode, started from the setup screen),
// the phone reconnects when it sees the clock advertising; the clock reads the phone's
// local time and UTC offset, sets itself, and disconnects. Outside setup mode the clock
// advertises all the time, resting 5 minutes after each sync. Android has no built-in Current
// Time Service, so Android phones use the QR/Wi-Fi setup instead.
#pragma once

#include <cstdint>

namespace ble_time {

void begin(const char* deviceName);
void loop();                   // call every loop() iteration (applies results on the main task)

void startPairing(uint32_t seconds = 120);
bool pairing();                // pairing mode is open
bool paired();                 // at least one phone is bonded
// Short status for the setup screen while pairing ("Pair in iPhone Bluetooth") or just after
// a sync ("Time set by phone", for 10 s); "" otherwise.
const char* status();

}  // namespace ble_time
