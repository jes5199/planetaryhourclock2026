// FT3168 capacitive touch on the board's I2C bus (address 0x38), polled.
//
// A background task reads the panel every 10 ms and tracks presses, so taps are caught even
// while the screen is busy redrawing; everything else reads that cached state.
#pragma once

#include <cstdint>

namespace touch {

bool begin();    // call after Wire.begin(); starts the polling task

bool read(uint16_t* x, uint16_t* y);  // true while a finger is down (panel coordinates)

// Gestures, each reported once:
bool longPress(uint32_t ms);  // a press held for `ms`
bool tripleTap();             // three quick taps

}  // namespace touch
