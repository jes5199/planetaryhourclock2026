#include "tabular_font.h"

#include <new>

namespace {

struct Tabular {
  lv_font_t font;  // must stay first: LVGL hands us &font
  const lv_font_t* base;
  uint16_t digitWidth;
};

bool getGlyphDsc(const lv_font_t* font, lv_font_glyph_dsc_t* dsc, uint32_t letter, uint32_t) {
  const Tabular* t = reinterpret_cast<const Tabular*>(font);
  if (!t->base->get_glyph_dsc(t->base, dsc, letter, 0)) return false;
  if (letter >= '0' && letter <= '9') {
    dsc->ofs_x += (t->digitWidth - dsc->adv_w) / 2;
    dsc->adv_w = t->digitWidth;
  }
  return true;
}

const uint8_t* getGlyphBitmap(const lv_font_t* font, uint32_t letter) {
  const Tabular* t = reinterpret_cast<const Tabular*>(font);
  return t->base->get_glyph_bitmap(t->base, letter);
}

}  // namespace

const lv_font_t* makeTabularFont(const lv_font_t* base) {
  Tabular* t = new Tabular{*base, base, 0};
  lv_font_glyph_dsc_t dsc;
  for (uint32_t c = '0'; c <= '9'; ++c) {
    if (base->get_glyph_dsc(base, &dsc, c, 0) && dsc.adv_w > t->digitWidth) t->digitWidth = dsc.adv_w;
  }
  t->font.get_glyph_dsc = getGlyphDsc;
  t->font.get_glyph_bitmap = getGlyphBitmap;
  return &t->font;
}
