// Line-based commands over the USB serial port (115200 baud), for setting the clock from a
// computer: time, time zone, location, setup mode, Bluetooth. Replies start with "ok:" or
// "error:"; `status` prints JSON. The help text is printed at boot, when a USB host connects,
// on `help`, and a one-line hint every 10 minutes, so whoever (or whatever) is reading the
// port can discover how to use it.
#pragma once

namespace serial_cmd {

void begin();
void loop();  // call every loop() iteration

}  // namespace serial_cmd
