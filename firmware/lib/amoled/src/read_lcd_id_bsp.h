// From the Waveshare ESP32-S3-Touch-AMOLED-1.43 examples (Apache-2.0):
// https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.43
// Reads the panel ID by bit-banging, to tell SH8601 from CO5300 panels.
#ifndef READ_LCD_ID_H
#define READ_LCD_ID_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif 
void lcd_gpio_init(void);
void SPI_ReadComm(uint8_t regval);
uint8_t SPI_ReadData(void);
uint8_t read_lcd_id(void);
#ifdef __cplusplus
}
#endif
#endif
