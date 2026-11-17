// PCF85063 real-time clock on the board's I2C bus. Stores UTC.
#pragma once

#include <ctime>

namespace rtc {

bool begin();                   // call after Wire.begin()
bool read(time_t* utcOut);      // false if the chip is missing or its time is invalid
bool write(time_t utc);

}  // namespace rtc
