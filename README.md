# PocketGame

A small multi-game handheld for the ESP32-S3-LCD-1.54 touch board (ESP-IDF, `source/`).
Minimal UI: dark background, thin lines, one accent colour.

- **Home screen:** one touch tile per game (tic-tac-toe for now) and a thin top bar with the battery %
  (IO1 / VBAT_ADC, 200k/100k divider) and the Wi-Fi state. The bar only shows outside games.
- **Tic-tac-toe:** tap a cell to place X or O; after a win or a draw, tap anywhere for a new game.
  X is blue, O is red: a banner in the player's colour shows who starts ("X STARTS") and whose turn it
  is. The starting player alternates after every finished round (an abandoned round keeps its starter).
- **Touch sound:** a short tick plays on every scroll step while the finger moves (every 12 px) and every
  5 volume steps while dragging the slider, not only on release. Menu tiles open on a tap (release
  without moving).
- **Buttons:** a short press on LEFT goes back (asks to confirm while a game is in progress);
  holding LEFT + RIGHT for 3 s opens Settings. The middle button is only the power key:
  holding it 3 s shows "Powering off" and releases the battery latch (on USB power the board sleeps
  instead, and the next press wakes it).
- **Settings:** volume slider (saved in NVS; 0 = silent, there is no separate mute) and Wi-Fi setup:
  the board scans for networks, opens the hotspot `PocketGame-XXXX`; join it from a phone, the setup page
  opens (or visit `192.168.4.1`) and lists nearby networks (signal bars, dBm, lock for secured ones).
  Tap one to fill in its name, or type a name by hand; "Scan again" rescans. Enter the password and the
  board joins the network.
- **HTTP (port 80, on the joined network):** `GET /status` returns JSON (`{"app":"pocketgame",...}`).
- The partition table has two OTA app slots and rollback is enabled, ready for over-the-air updates
  (for example from GitHub releases).

Pins come from the vendor schematic (`docs/ESP32-S3-LCD-1.54-Schematic.pdf`), see `source/main/board.h`.
LEFT = GPIO0 (BOOT), RIGHT = GPIO4, power key GPIO5 (active low), battery latch BAT_EN = GPIO2.

Serial commands (USB-C console, 115200):

| command | reply |
| --- | --- |
| `new`, `play <0-8>`, `board` | `READY turn=X\|O` (starter of the new round), `BOARD X.O......`, `TURN O`, `WIN X line=0,1,2`, `DRAW`, `ERR occupied\|game over\|bad cell` |
| `status` | `STATUS screen=menu game=ttt bat=87 mv=4012 wifi=connected ip=... starter=X vol=70` |
| `open ttt`, `back`, `yes`, `no`, `settings` | `SCREEN menu\|game=ttt\|confirm\|settings\|wifi`, `MENU games=1` |
| `vol <0-100>` | `VOL n` (0 = silent) |
| `wifi scan` | `WIFI scan n=<count>`, then `WIFI ap <i> rssi=.. ch=.. secure=0\|1` per network (names stay off the console) |
| `wifi ssid` / `wifi pass` | `SSID?` / `PASS?`, then the next line is the value; `WIFI saved`, `WIFI connected ip=...` |
| `wifi`, `wifi setup`, `wifi stop`, `wifi forget` | Wi-Fi state, setup hotspot on/off, forget the network |

Host test of the rules: `gcc -I source/main tests/test_game.c source/main/game.c -o test_game && ./test_game`
