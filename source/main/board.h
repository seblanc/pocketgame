#pragma once

/* Pinout of the ESP32-S3-LCD-1.54 board, from the vendor schematic
 * (docs/ESP32-S3-LCD-1.54-Schematic.pdf). */

/* 240x240 ST7789 panel, write-only SPI (the panel has no read-back line) */
#define LCD_SCLK 38
#define LCD_MOSI 39
#define LCD_CS 21
#define LCD_DC 45
#define LCD_RST 40
#define LCD_BL 46 /* drives the LED_K low-side MOSFET: high = on */

/* one I2C bus for the touch panel, the ES8311 codec, the ES7210 mic ADC and the IMU */
#define I2C_SDA 42
#define I2C_SCL 41
#define TP_RST 47
#define TP_INT 48

/* ES8311 codec -> NS4150B speaker amplifier */
#define I2S_MCLK 8
#define I2S_BCLK 9
#define I2S_WS 10
#define I2S_DOUT 12 /* ESP -> codec (DSDIN) */
#define PA_CTRL 7
#define ES8311_ADDR 0x18

/* buttons, all active low with pull-ups */
#define KEY_MINUS 0 /* also BOOT */
#define KEY_PLUS 4
#define KEY_PWR 5

/* battery power latch: high keeps the board on when running from the battery */
#define BAT_EN 2
