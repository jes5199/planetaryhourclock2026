// FT3168 capacitive touch on the board's I2C bus (address 0x38), polled.
//
// update() reads the panel (at most every 10 ms) and tracks presses; everything else reads
// that cached state, so the I2C bus is touched once per poll however many consumers there are.
#pragma once

#include <cstdint>

namespace touch {

bool begin();    // call after Wire.begin()
void update();   // call every loop() iteration

bool read(uint16_t* x, uint16_t* y);  // true while a finger is down (panel coordinates)

// Gestures, each reported once:
bool longPress(uint32_t ms);  // a press held for `ms`
bool tripleTap();             // three quick taps

}  // namespace touch
