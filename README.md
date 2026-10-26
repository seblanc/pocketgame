# PocketGame

A small multi-game handheld for the ESP32-S3-LCD-1.54 touch board (ESP-IDF, `source/`).
Minimal UI: dark background, thin lines, one accent colour.

- **Home screen:** no title, one touch tile per game right below the bar (Tic-tac-toe, Connect 4; all games are for 2 players) and a thin top bar with the battery %
  (IO1 / VBAT_ADC, 200k/100k divider) and the Wi-Fi state. The bar only shows outside games.
- **Games** are self-contained modules in `source/main/game/` (one `.c/.h` per game) behind the common
  interface in `game/game.h` (rules, board drawing, touch mapping, UART command); `GAMES[]` in `main.c`
  lists them as menu tiles. The app frame (scores, turn banner, leave confirmation, sounds, alternating
  starter) is shared.
- **Connect 4:** 7 columns x 6 rows; press-drag-release: touching the board shows a preview disc in the
  mover's colour above the column under the finger (in place of the banner and scores), it follows the
  finger, and releasing drops the disc there to the lowest free cell. Releasing off the board
  (`AIM cancelled`) or on a full column (`ERR full column n`) leaves the turn unchanged.
  Four in a row (horizontal, vertical, diagonal) wins and is highlighted; a full board is a draw.
  Blue X / red O discs and banner, same sounds and rules for starting and leaving as tic-tac-toe.
- **Tic-tac-toe:** tap a cell to place X or O; after a win or a draw, tap anywhere for a new game.
  X is blue, O is red: a banner in the player's colour shows who starts ("X STARTS") and whose turn it
  is. The starting player alternates after every finished round (an abandoned round keeps its starter).
- **Scrolling lists:** the home menu game list and the Settings rows scroll by finger drag, keep gliding
  after a flick and slow down, and stretch with resistance past the top or bottom, springing back on
  release (iOS-style). Menu tiles and the Wi-Fi SET UP button act on a tap (release without scrolling);
  touching a gliding list only stops it.
- **Touch sound:** a soft, decaying tick. The volume slider ticks only while the finger is on the volume
  bar and the value changes, at least 90 ms apart. Both lists (home menu, Settings) scroll silently.
- **Buttons:** a short press on LEFT goes back (asks to confirm while a game is in progress);
  holding LEFT + RIGHT for 3 s opens Settings. The middle button is only the power key:
  holding it 3 s shows "Powering off" and releases the battery latch (on USB power the board sleeps
  instead, and the next press wakes it).
- **Settings:** a scrolling list of rows: volume slider (saved in NVS; 0 shows OFF and is silent, there
  is no separate mute), Wi-Fi, a button reference and the firmware version. Wi-Fi setup:
  the board scans for networks, opens the hotspot `PocketGame-XXXX`; join it from a phone, the setup page
  opens (or visit `192.168.4.1`) and lists nearby networks (signal bars, dBm, lock for secured ones).
  Tap one to fill in its name, or type a name by hand; "Scan again" rescans. Enter the password and tap
  the filled, rounded CONNECT button; the board joins the network.
- **HTTP (port 80, on the joined network):** `GET /status` returns JSON (`{"app":"pocketgame",...}`).
- The partition table has two OTA app slots and rollback is enabled, ready for over-the-air updates
  (for example from GitHub releases).

Pins come from the vendor schematic (`docs/ESP32-S3-LCD-1.54-Schematic.pdf`), see `source/main/board.h`.
LEFT = GPIO0 (BOOT), RIGHT = GPIO4, power key GPIO5 (active low), battery latch BAT_EN = GPIO2.

Serial commands (USB-C console, 115200):

| command | reply |
| --- | --- |
| `new`, `board` | new round / board of the game on screen (tic-tac-toe from other screens): `READY turn=X\|O` (starter of the new round), `BOARD ...`, `TURN O` |
| `play <0-8>` | tic-tac-toe: `BOARD X.O......`, `TURN O`, `WIN X line=0,1,2`, `DRAW`, `ERR occupied\|game over\|bad cell` |
| `drop <0-6> [<0-6> ...]` | Connect 4, one drop per number: `DROP X col=3 row=0`, `BOARD` + 42 cells (top row first), `TURN O`, `WIN X cells=a,b,c,d`, `DRAW`, `ERR range\|full column n\|game over` |
| `status` | `STATUS screen=menu game=ttt bat=87 mv=4012 wifi=connected ip=... starter=X vol=70` |
| `open ttt\|c4`, `back`, `yes`, `no`, `settings` | `SCREEN menu\|game=ttt\|game=c4\|confirm\|settings\|wifi`, `MENU games=2` |
| `vol <0-100>` | `VOL n` (0 = silent) |
| `wifi scan` | `WIFI scan n=<count>`, then `WIFI ap <i> rssi=.. ch=.. secure=0\|1` per network (names stay off the console) |
| `wifi ssid` / `wifi pass` | `SSID?` / `PASS?`, then the next line is the value; `WIFI saved`, `WIFI connected ip=...` |
| `wifi`, `wifi setup`, `wifi stop`, `wifi forget` | Wi-Fi state, setup hotspot on/off, forget the network |

Host tests of the rules:
`gcc -DGAME_RULES_ONLY -I source/main tests/test_game.c source/main/game/tictactoe.c -o test_game && ./test_game`,
`gcc -DGAME_RULES_ONLY -I source/main tests/test_connect4.c source/main/game/connect4.c -o test_c4 && ./test_c4`

Host test of the list scrolling: `gcc -I source/main tests/test_scroll.c source/main/scroll.c -lm -o test_scroll && ./test_scroll`
