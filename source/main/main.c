/* PocketGame: two-player tic-tac-toe on an ESP32-S3 1.54" touch LCD.
 *
 * Serial protocol (USB Serial/JTAG console, one command per line):
 *   new          -> BOARD .........  TURN X
 *   play <0-8>   -> MOVE X 4, BOARD ..., then TURN O | WIN X line=a,b,c | DRAW
 *                   or ERR occupied | ERR game over | ERR bad cell
 *   board        -> BOARD ... and the current state
 *   bl           -> BL gpio=<n> on=<0|1> option=<i>/<count> searching=<0|1>
 *   bl next | bl ok | bl search | bl <gpio> <0|1>   backlight search by hand
 *   forget       -> drops the saved display and backlight pins and restarts
 * Touch moves and button presses print the same lines.
 *
 * Until a backlight option is confirmed, the firmware switches to the next
 * candidate pin every few seconds; the first tap (or the BOOT button) on the lit
 * screen saves it. Holding a finger on the screen for 3 s starts the search again. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backlight.h"
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "game.h"
#include "hal/usb_serial_jtag_ll.h"
#include "sound.h"
#include "touch.h"

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_display_task;
static TaskHandle_t s_bl_task;
static view_t s_view;
static board_t s_board;

static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

static void request_redraw(void)
{
    if (s_display_task) {
        xTaskNotifyGive(s_display_task);
    }
}

static void print_board_locked(void)
{
    char b[10];
    game_board_str(&s_view.game, b);
    out("BOARD %s", b);
}

static void print_state_locked(void)
{
    const game_t *g = &s_view.game;
    if (g->status == GAME_WIN) {
        out("WIN %c line=%d,%d,%d", game_mark_char(g->winner), g->win_line[0], g->win_line[1], g->win_line[2]);
    } else if (g->status == GAME_DRAW) {
        out("DRAW");
    } else {
        out("TURN %c", game_mark_char(g->turn));
    }
}

static void app_new_game(const char *src)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    game_new(&s_view.game);
    out("NEW src=%s", src);
    print_board_locked();
    print_state_locked();
    xSemaphoreGive(s_lock);
    sound_play(SND_NEW);
    request_redraw();
}

static void app_play(int cell, const char *src)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    game_t *g = &s_view.game;
    uint8_t mover = g->turn;
    move_result_t r = game_play(g, cell);
    sound_t snd = SND_BAD;
    switch (r) {
    case MOVE_ERR_RANGE:
        out("ERR bad cell");
        break;
    case MOVE_ERR_OCCUPIED:
        out("ERR occupied");
        break;
    case MOVE_ERR_OVER:
        out("ERR game over");
        break;
    case MOVE_OK:
        out("MOVE %c %d src=%s", game_mark_char(mover), cell, src);
        print_board_locked();
        print_state_locked();
        if (g->status == GAME_WIN) {
            if (g->winner == CELL_X) {
                s_view.wins_x++;
            } else {
                s_view.wins_o++;
            }
            snd = SND_WIN;
        } else if (g->status == GAME_DRAW) {
            snd = SND_DRAW;
        } else {
            snd = mover == CELL_X ? SND_X : SND_O;
        }
        break;
    }
    xSemaphoreGive(s_lock);
    sound_play(snd);
    if (r == MOVE_OK) {
        request_redraw();
    }
}

static bool calibrating(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool c = s_view.calibrating;
    xSemaphoreGive(s_lock);
    return c;
}

static void print_bl_locked(void)
{
    int pin, on, idx, n;
    backlight_current(&pin, &on, &idx, &n);
    out("BL gpio=%d on=%d option=%d/%d searching=%d", pin, on, idx + 1, n, s_view.calibrating);
}

/* the lit screen was seen: keep this backlight option */
static void bl_confirm(const char *src)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_view.calibrating = false;
    backlight_confirm();
    int pin, on, idx, n;
    backlight_current(&pin, &on, &idx, &n);
    out("BL confirmed gpio=%d on=%d src=%s", pin, on, src);
    xSemaphoreGive(s_lock);
    sound_play(SND_NEW);
    request_redraw();
}

static void bl_search(const char *src)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_view.calibrating = true;
    backlight_forget();
    out("BL search started src=%s: tap the screen once it lights up", src);
    xSemaphoreGive(s_lock);
    if (s_bl_task) {
        xTaskNotifyGive(s_bl_task);
    }
    request_redraw();
}

static void handle_bl(const char *arg)
{
    int pin, on;
    if (*arg == '\0') {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        print_bl_locked();
        xSemaphoreGive(s_lock);
    } else if (strcmp(arg, "next") == 0) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        backlight_next();
        print_bl_locked();
        xSemaphoreGive(s_lock);
    } else if (strcmp(arg, "ok") == 0) {
        bl_confirm("uart");
    } else if (strcmp(arg, "search") == 0) {
        bl_search("uart");
    } else if (sscanf(arg, "%d %d", &pin, &on) == 2 && pin >= 0 && pin <= 48) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        backlight_set(pin, on);
        print_bl_locked();
        xSemaphoreGive(s_lock);
    } else {
        out("ERR bl (bl | bl next | bl ok | bl search | bl <gpio> <0|1>)");
    }
}

static void handle_line(char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }
    size_t n = strlen(line);
    while (n && isspace((unsigned char)line[n - 1])) {
        line[--n] = '\0';
    }
    if (n == 0) {
        return;
    }
    if (strcmp(line, "new") == 0) {
        app_new_game("uart");
    } else if (strncmp(line, "play", 4) == 0) {
        char *end;
        long cell = strtol(line + 4, &end, 10);
        if (end == line + 4) {
            out("ERR bad cell");
        } else {
            app_play((int)cell, "uart");
        }
    } else if (strcmp(line, "board") == 0) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        print_board_locked();
        print_state_locked();
        out("SCORE X=%d O=%d", s_view.wins_x, s_view.wins_o);
        xSemaphoreGive(s_lock);
    } else if (strcmp(line, "bl") == 0 || strncmp(line, "bl ", 3) == 0) {
        const char *arg = line + 2;
        while (*arg == ' ') {
            arg++;
        }
        handle_bl(arg);
    } else if (strcmp(line, "forget") == 0) {
        board_forget_lcd();
        backlight_forget();
        out("FORGET restarting");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        out("ERR unknown command (new | play <0-8> | board | bl | forget)");
    }
}

/* Reads the USB Serial/JTAG RX FIFO directly: the console keeps its non-blocking
 * output, so the game never stalls when no computer is listening. */
static void console_task(void *arg)
{
    char line[64];
    size_t len = 0;
    for (;;) {
        uint8_t buf[64];
        int n = 0;
        if (usb_serial_jtag_ll_rxfifo_data_available()) {
            n = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
        }
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                handle_line(line);
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            }
        }
    }
}

static void display_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        view_t snap;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snap = s_view;
        xSemaphoreGive(s_lock);
        display_render(&snap);
    }
}

static void touch_task(void *arg)
{
    TickType_t down_at = 0;
    bool held = false;
    for (;;) {
        int x, y;
        if (touch_poll_press(&x, &y)) {
            down_at = xTaskGetTickCount();
            held = false;
            int cell = display_cell_at(x, y);
            out("TAP x=%d y=%d cell=%d", x, y, cell);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool cal = s_view.calibrating;
            bool over = s_view.game.status != GAME_PLAYING;
            xSemaphoreGive(s_lock);
            if (cal) {
                bl_confirm("touch");
            } else if (over) {
                app_new_game("touch");
            } else if (cell >= 0) {
                app_play(cell, "touch");
            }
        } else if (touch_is_down() && !held && xTaskGetTickCount() - down_at >= pdMS_TO_TICKS(3000)) {
            held = true;
            bl_search("hold");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* Runs the slow I2C scan off the boot path, then the backlight search whenever
 * it is active: one candidate every few seconds until a tap confirms it. */
static void bl_task(void *arg)
{
    int pin, on, idx, n;
    backlight_current(&pin, &on, &idx, &n);
    board_scan_i2c(&s_board, PIN_BIT(pin));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    backlight_exclude(s_board.i2c_pins); /* bus lines are never the backlight */
    xSemaphoreGive(s_lock);
    for (;;) {
        if (!calibrating()) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        backlight_current(&pin, &on, &idx, &n);
        out("DIAG backlight try gpio=%d on=%d option=%d/%d", pin, on, idx + 1, n);
        xSemaphoreGive(s_lock);
        for (int t = 0; t < 50 && calibrating(); t++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_view.calibrating) {
            backlight_next();
        }
        xSemaphoreGive(s_lock);
    }
}

/* Any of the board's buttons starts a new game. */
static void button_task(void *arg)
{
    int pins[3];
    int last[3];
    for (int i = 0; i < 3; i++) {
        pins[i] = s_board.profile->buttons[i];
        const touch_info_t *t = &s_board.touch;
        if (t->kind != TOUCH_NONE && (pins[i] == t->sda || pins[i] == t->scl || pins[i] == t->rst)) {
            pins[i] = NO_PIN; /* the touch bus owns this pin */
        }
        last[i] = 1;
        if (pins[i] >= 0) {
            gpio_reset_pin(pins[i]);
            gpio_set_direction(pins[i], GPIO_MODE_INPUT);
            gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    for (int i = 0; i < 3; i++) {
        if (pins[i] >= 0) {
            last[i] = gpio_get_level(pins[i]);
        }
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(25));
        for (int i = 0; i < 3; i++) {
            if (pins[i] < 0) {
                continue;
            }
            int v = gpio_get_level(pins[i]);
            if (v != last[i]) {
                vTaskDelay(pdMS_TO_TICKS(15));
                v = gpio_get_level(pins[i]);
            }
            if (v == 0 && last[i] == 1) {
                out("BTN gpio=%d", pins[i]);
                if (calibrating()) {
                    bl_confirm("button");
                } else {
                    app_new_game("button");
                }
            }
            last[i] = v;
        }
    }
}

void app_main(void)
{
    s_lock = xSemaphoreCreateMutex();
    game_new(&s_view.game);
    esp_log_level_set("gpio", ESP_LOG_WARN); /* pin probing reconfigures hundreds of pins */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* give the host time to reopen the USB serial port after a reset */
    vTaskDelay(pdMS_TO_TICKS(1500));
    out("POCKETGAME BOOT");

    board_detect(&s_board);
    const board_profile_t *p = s_board.profile;

    err = display_init(p);
    if (err != ESP_OK) {
        out("LCD FAIL profile=%s err=%s", p->name, esp_err_to_name(err));
    } else if (s_board.lcd_detected) {
        out("LCD OK profile=%s src=%s id=0x%06lx verified sclk=%d sda=%d cs=%d dc=%d rst=%d", p->name,
            s_board.lcd_source, (unsigned long)s_board.lcd_id, p->lcd_sclk, p->lcd_mosi, p->lcd_cs,
            p->lcd_dc, p->lcd_rst);
    } else {
        out("LCD UNVERIFIED profile=%s: no display answered its ID read, using a guess", p->name);
    }

    uint64_t busy = board_lcd_mask(p);
    for (int i = 0; i < 3; i++) {
        busy |= PIN_BIT(p->buttons[i]);
    }
    if (s_board.touch.kind != TOUCH_NONE) {
        busy |= PIN_BIT(s_board.touch.sda) | PIN_BIT(s_board.touch.scl) | PIN_BIT(s_board.touch.rst);
    }
    bool bl_saved = backlight_init(&s_board, busy);
    s_view.calibrating = !bl_saved;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    print_bl_locked();
    xSemaphoreGive(s_lock);
    if (!bl_saved) {
        out("BL search: tap the screen or press BOOT once it lights up");
    }

    if (touch_init(&s_board.touch)) {
        out("TOUCH OK chip=%s addr=0x%02x sda=%d scl=%d rst=%d", touch_kind_name(s_board.touch.kind),
            s_board.touch.addr, s_board.touch.sda, s_board.touch.scl, s_board.touch.rst);
        xTaskCreate(touch_task, "touch", 3072, NULL, 5, NULL);
    } else {
        out("TOUCH NONE: no touch controller found on I2C");
    }

    out("AUDIO %s", sound_init(p));
    xTaskCreate(display_task, "display", 4096, NULL, 4, &s_display_task);
    xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL);
    request_redraw();

    out("READY turn=X");
    xTaskCreate(console_task, "console", 4096, NULL, 6, NULL);
    xTaskCreate(bl_task, "backlight", 4096, NULL, 2, &s_bl_task);
}
