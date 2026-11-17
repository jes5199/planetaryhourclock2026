// Display bring-up for the Waveshare ESP32-S3-Touch-AMOLED-1.43:
// 466x466 round AMOLED on QSPI (SH8601 or CO5300 controller, detected at boot), driven through LVGL 8.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AMOLED_WIDTH 466
#define AMOLED_HEIGHT 466

// Initializes the panel and registers it as the default LVGL display.
// `rotation_cw` turns the picture clockwise: 0, 90, 180 or 270 degrees.
// Call lv_timer_handler() regularly afterwards (from one task only).
lv_disp_t* amoled_init(int rotation_cw);

// Converts a touch point in panel coordinates to LVGL's (rotated) coordinates.
void amoled_panel_to_screen(int px, int py, int* x, int* y);

// Panel brightness, 0 (off) to 255.
void amoled_set_brightness(uint8_t level);

#ifdef __cplusplus
}
#endif
