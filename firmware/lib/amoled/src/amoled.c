// Adapted from the Waveshare ESP32-S3-Touch-AMOLED-1.43 Arduino LVGL example (lcd_bsp.c).
#include "amoled.h"

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "read_lcd_id_bsp.h"

#define PIN_LCD_CS 9
#define PIN_LCD_PCLK 10
#define PIN_LCD_DATA0 11
#define PIN_LCD_DATA1 12
#define PIN_LCD_DATA2 13
#define PIN_LCD_DATA3 14
#define PIN_LCD_RST 21

#define LCD_HOST SPI2_HOST
#define LCD_BITS_PER_PIXEL 16
#define DRAW_BUF_LINES (AMOLED_HEIGHT / 8)

#define SH8601_ID 0x86  // anything else is treated as CO5300, which needs a 6 px column offset

static const char* TAG = "amoled";

static const sh8601_lcd_init_cmd_t sh8601_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static const sh8601_lcd_init_cmd_t co5300_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static int s_x_offset = 0;

static bool on_flush_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* edata,
                          void* user_ctx) {
  lv_disp_flush_ready((lv_disp_drv_t*)user_ctx);
  return false;
}

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
  esp_lcd_panel_draw_bitmap(s_panel, area->x1 + s_x_offset, area->y1, area->x2 + 1 + s_x_offset,
                            area->y2 + 1, color_map);
}

// The controller needs even start and odd end coordinates.
static void rounder_cb(lv_disp_drv_t* drv, lv_area_t* area) {
  area->x1 = (area->x1 >> 1) << 1;
  area->y1 = (area->y1 >> 1) << 1;
  area->x2 = ((area->x2 >> 1) << 1) + 1;
  area->y2 = ((area->y2 >> 1) << 1) + 1;
}

lv_disp_t* amoled_init(int rotation_cw) {
  static lv_disp_draw_buf_t draw_buf;
  static lv_disp_drv_t disp_drv;

  const uint8_t id = read_lcd_id();
  const bool is_sh8601 = id == SH8601_ID;
  s_x_offset = is_sh8601 ? 0 : 6;
  ESP_LOGI(TAG, "panel id 0x%02x (%s)", id, is_sh8601 ? "SH8601" : "CO5300");

  const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
      PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
      AMOLED_WIDTH * DRAW_BUF_LINES * LCD_BITS_PER_PIXEL / 8);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  const esp_lcd_panel_io_spi_config_t io_config =
      SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_flush_done, &disp_drv);
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io));

  sh8601_vendor_config_t vendor_config = {
      .init_cmds = is_sh8601 ? sh8601_init_cmds : co5300_init_cmds,
      .init_cmds_size = is_sh8601 ? sizeof(sh8601_init_cmds) / sizeof(sh8601_init_cmds[0])
                                  : sizeof(co5300_init_cmds) / sizeof(co5300_init_cmds[0]),
      .flags = {.use_qspi_interface = 1},
  };
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = PIN_LCD_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = LCD_BITS_PER_PIXEL,
      .vendor_config = &vendor_config,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_io, &panel_config, &s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

  lv_init();
  const size_t buf_px = AMOLED_WIDTH * DRAW_BUF_LINES;
  lv_color_t* buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_color_t* buf2 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf1 && buf2);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_px);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = AMOLED_WIDTH;
  disp_drv.ver_res = AMOLED_HEIGHT;
  disp_drv.flush_cb = flush_cb;
  disp_drv.rounder_cb = rounder_cb;
  disp_drv.draw_buf = &draw_buf;
  // LVGL's ROT_270 turns the content clockwise by 90 degrees, ROT_90 counter-clockwise.
  switch (((rotation_cw % 360) + 360) % 360) {
    case 90: disp_drv.sw_rotate = 1; disp_drv.rotated = LV_DISP_ROT_270; break;
    case 180: disp_drv.sw_rotate = 1; disp_drv.rotated = LV_DISP_ROT_180; break;
    case 270: disp_drv.sw_rotate = 1; disp_drv.rotated = LV_DISP_ROT_90; break;
    default: break;
  }
  return lv_disp_drv_register(&disp_drv);
}

void amoled_set_brightness(uint8_t level) {
  if (!s_io) return;
  esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), &level, 1);
}
