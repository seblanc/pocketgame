# PocketGame

Two-player tic-tac-toe for an ESP32-S3 1.54" 240x240 touch LCD board (ESP-IDF, `source/`).

- Tap an empty cell to place X or O; after a win or a draw, tap anywhere for a new game.
- The BOOT button starts a new game. Tones play on the speaker when its pins are known.
- The seller publishes no pinout, so the firmware finds the hardware itself and prints `DIAG ...` lines:
  - Display: it reads the ST7789 ID (RDDID, bit-banged over the bidirectional SDA line) on the known
    1.54" pin maps; if none answers, it searches the free GPIOs for SCLK/SDA and then tells CS, DC and RST
    apart by flipping each line. Pins that answered are saved in NVS and checked again on every boot.
    `LCD OK ... verified` is printed only when the ID read succeeded, `LCD UNVERIFIED` otherwise.
  - Backlight: it can't be detected electrically. Until one is confirmed, the screen shows `TAP ME` and
    the firmware switches to the next candidate pin/level every 2.5 s; the first tap (or BOOT) on the lit
    screen saves it. Holding a finger on the screen for 3 s starts the search again.
  - I2C: after boot, a background scan reports every bus it finds (e.g. audio codec, IMU).

Serial commands (USB-C console, 115200): `new`, `play <0-8>`, `board`, `bl`, `bl next`, `bl ok`,
`bl search`, `bl <gpio> <0|1>`, `forget` (drops the saved display and backlight pins, restarts).
Replies: `BOARD X.O......`, `TURN O`, `WIN X line=0,1,2`, `DRAW`, `ERR occupied|game over|bad cell`.

Host test of the rules: `gcc -I source/main tests/test_game.c source/main/game.c -o test_game && ./test_game`
