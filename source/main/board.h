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

/* QMI8658 IMU (0x6b on the same bus): how its axes lie under the screen. Assumed, not
 * checked on the board: sensor x to the screen's right, y to its top. At rest the
 * accelerometer reads +1 g upwards, so a lowered right edge gives a negative x and a
 * lowered bottom edge a positive y. Fix the signs (or swap) here if the ball rolls
 * the wrong way. */
#define IMU_TILT_X_SIGN (-1)
#define IMU_TILT_Y_SIGN 1
#define IMU_SWAP_XY 0
/* Tried on the board: left/right came out mirrored, so the games read the sensor's X
 * axis inverted (all motion games: Tilt Duel, Hot Potato, Steady Hand). */
#define IMU_INVERT_X 1

/* ES8311 codec -> NS4150B speaker amplifier */
#define I2S_MCLK 8
#define I2S_BCLK 9
#define I2S_WS 10
#define I2S_DOUT 12 /* ESP -> codec (DSDIN) */
#define PA_CTRL 7
#define ES8311_ADDR 0x18

/* buttons on the top edge, active low with pull-ups. LEFT and RIGHT are the app keys;
 * the middle one is the power key: pressing it switches the board on, holding it 3 s
 * switches it off. */
#define KEY_LEFT 0  /* KEY_MINUS, also BOOT */
#define KEY_RIGHT 4 /* KEY_PLUS */
#define KEY_PWR 5

/* battery power latch: high keeps the board on when running from the battery */
#define BAT_EN 2

/* VBAT through R27 200k / R32 100k to IO1 (ADC1 channel 0) */
#define BAT_ADC 1
#define BAT_DIVIDER 3
