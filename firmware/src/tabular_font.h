// Fixed-width ("tabular") digits for any LVGL font: every digit 0-9 advances by the width
// of the widest digit and is centred in that cell, so changing numbers don't shift the text.
// Kerning is turned off so numbers line up exactly.
#pragma once

#include "lvgl.h"

// Returns a font that draws like `base` but with tabular digits. The returned font lives
// for the rest of the program; call once per base font.
const lv_font_t* makeTabularFont(const lv_font_t* base);
