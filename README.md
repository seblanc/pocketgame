# PocketGame

Two-player tic-tac-toe for an ESP32-S3 1.54" 240x240 touch LCD board (ESP-IDF, `source/`).

- Tap an empty cell to place X or O; after a win or a draw, tap anywhere for a new game.
- Any of the board buttons starts a new game. Tones play on the speaker when its pins are known.
- The seller publishes no pinout. At boot the firmware tries the known 1.54" board pin maps, reads the
  display ID on each, scans every free GPIO pair for I2C devices and prints `DIAG ...` lines with what it found.

Serial commands (USB-C console, 115200): `new`, `play <0-8>`, `board`.
Replies: `BOARD X.O......`, `TURN O`, `WIN X line=0,1,2`, `DRAW`, `ERR occupied|game over|bad cell`.

Host test of the rules: `gcc -I source/main tests/test_game.c source/main/game.c -o test_game && ./test_game`
