# PocketGame

Two-player tic-tac-toe for the ESP32-S3-LCD-1.54 touch board (ESP-IDF, `source/`).

- Tap an empty cell to place X or O; after a win or a draw, tap anywhere for a new game.
- Any of the three buttons starts a new game; holding PWR for 2 s switches the board off on battery.
- Tones play on the speaker through the ES8311 codec and the NS4150B amplifier.

Pins come from the vendor schematic (`docs/ESP32-S3-LCD-1.54-Schematic.pdf`), see `source/main/board.h`:
ST7789 240x240 on SPI (SCLK 38, SDA 39, CS 21, DC 45, RST 40, backlight 46); one I2C bus
(SDA 42, SCL 41) for the touch panel (RST 47, INT 48), the ES8311 codec, the ES7210 mic ADC and the
QMI8658 IMU; I2S to the codec (MCLK 8, BCLK 9, WS 10, DOUT 12), amplifier enable 7; buttons
BOOT/- 0, + 4, PWR 5; battery power latch 2. The touch controller is identified by its I2C address
at boot (`TOUCH OK chip=...`), and `DIAG i2c bus ...` lists every device on the bus.

Serial commands (USB-C console, 115200): `new`, `play <0-8>`, `board`.
Replies: `BOARD X.O......`, `TURN O`, `WIN X line=0,1,2`, `DRAW`, `ERR occupied|game over|bad cell`.

Host test of the rules: `gcc -I source/main tests/test_game.c source/main/game.c -o test_game && ./test_game`
