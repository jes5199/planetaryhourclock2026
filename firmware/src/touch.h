// FT3168 capacitive touch on the board's I2C bus (address 0x38), polled.
#pragma once

#include <cstdint>

namespace touch {

bool begin();                       // call after Wire.begin()
bool read(uint16_t* x, uint16_t* y);  // true while a finger is down (panel coordinates)

// Long-press detector: call often; returns true once each time a press reaches `ms`.
bool longPress(uint32_t ms);

}  // namespace touch
