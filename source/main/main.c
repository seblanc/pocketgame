/* PocketGame: a small multi-game handheld on the ESP32-S3-LCD-1.54 touch board.
 *
 * Screens: home menu (one tile per game, top bar with battery % and Wi-Fi), the
 * games (tic-tac-toe for now), a leave-game confirmation, Settings (volume, mute,
 * Wi-Fi) and the Wi-Fi setup hotspot screen.
 *
 * Buttons: a short press on LEFT goes back (asking first while a game is running);
 * holding LEFT + RIGHT for 3 s opens Settings. The middle button is the power key:
 * the firmware never reads it.
 *
 * Serial protocol (USB Serial/JTAG console, one command per line):
 *   new | play <0-8> | board   tic-tac-toe, as before (switches to the game screen):
 *                              BOARD ........., TURN O | WIN X line=a,b,c | DRAW,
 *                              ERR occupied | ERR game over | ERR bad cell
 *   status                     STATUS screen=menu bat=.. mv=.. wifi=.. ip=.. vol=.. mute=..
 *   open <id> | back | yes | no | settings      navigation, replies SCREEN ...
 *   vol <0-100> | mute <0|1>                    VOL n, MUTE n (saved in NVS)
 *   wifi [ssid | pass | setup | stop | forget]  ssid/pass ask SSID? / PASS? and read
 *                                               the next line as the value
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "battery.h"
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "game.h"
#include "hal/usb_serial_jtag_ll.h"
#include "i2cbus.h"
#include "net.h"
#include "settings.h"
#include "sound.h"
#include "touch.h"
#include "ui.h"

#define COMBO_MS 3000
#define SHORT_PRESS_MS 1000
#define HEARTBEAT_S 10
#define BATTERY_S 20

const game_info_t GAMES[] = {
    {"ttt", "TIC-TAC-TOE", "2 PLAYERS"},
};
const int GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);

static const char *const SCREEN_NAMES[] = {"menu", "game", "confirm", "settings", "wifi"};

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_display_task;
static app_view_t s_v;                    /* under s_lock */
static screen_t s_settings_from = SCR_MENU; /* where Settings returns to */

void app_out(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof(buf) - 2) {
        n = sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    fwrite(buf, 1, n, stdout);
    fflush(stdout);
}
#define out app_out

static void request_redraw(void)
{
    if (s_display_task) {
        xTaskNotifyGive(s_display_task);
    }
}

void app_net_changed(void) { request_redraw(); }

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* ---- screens ---- */

static void show_locked(screen_t s)
{
    s_v.screen = s;
    if (s == SCR_GAME) {
        out("SCREEN game=%s", GAMES[s_v.game_idx].id);
    } else {
        out("SCREEN %s", SCREEN_NAMES[s]);
    }
    if (s == SCR_MENU) {
        out("MENU games=%d", GAME_COUNT);
    }
    request_redraw();
}

static bool game_running_locked(void) { return s_v.game.status == GAME_PLAYING && s_v.game.moves > 0; }

/* the tic-tac-toe commands work from any screen and bring the game up */
static void ensure_game_locked(void)
{
    if (s_v.screen != SCR_GAME) {
        s_v.game_idx = 0;
        show_locked(SCR_GAME);
    }
}

static void print_board_locked(void)
{
    char b[10];
    game_board_str(&s_v.game, b);
    out("BOARD %s", b);
}

static void print_state_locked(void)
{
    const game_t *g = &s_v.game;
    if (g->status == GAME_WIN) {
        out("WIN %c line=%d,%d,%d", game_mark_char(g->winner), g->win_line[0], g->win_line[1], g->win_line[2]);
    } else if (g->status == GAME_DRAW) {
        out("DRAW");
    } else {
        out("TURN %c", game_mark_char(g->turn));
    }
}

static void app_open_game(int idx, const char *src)
{
    lock();
    if (idx < 0 || idx >= GAME_COUNT) {
        out("ERR unknown game");
        unlock();
        return;
    }
    s_v.game_idx = idx;
    if (s_v.game.status != GAME_PLAYING) {
        game_new(&s_v.game);
    }
    show_locked(SCR_GAME);
    print_board_locked();
    print_state_locked();
    unlock();
    sound_play(SND_TICK);
}

static void app_new_game(const char *src)
{
    lock();
    ensure_game_locked();
    game_new(&s_v.game);
    out("NEW src=%s", src);
    print_board_locked();
    print_state_locked();
    unlock();
    sound_play(SND_NEW);
    request_redraw();
}

static void app_play(int cell, const char *src)
{
    lock();
    ensure_game_locked();
    game_t *g = &s_v.game;
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
                s_v.wins_x++;
            } else {
                s_v.wins_o++;
            }
            snd = SND_WIN;
        } else if (g->status == GAME_DRAW) {
            snd = SND_DRAW;
        } else {
            snd = mover == CELL_X ? SND_X : SND_O;
        }
        break;
    }
    unlock();
    sound_play(snd);
    if (r == MOVE_OK) {
        request_redraw();
    }
}

static void app_back(const char *src)
{
    bool stop_setup = false;
    lock();
    out("BACK src=%s", src);
    switch (s_v.screen) {
    case SCR_GAME:
        if (game_running_locked()) {
            show_locked(SCR_CONFIRM);
        } else {
            game_new(&s_v.game);
            show_locked(SCR_MENU);
        }
        break;
    case SCR_CONFIRM:
        show_locked(SCR_GAME);
        break;
    case SCR_SETTINGS:
        show_locked(s_settings_from);
        break;
    case SCR_WIFI:
        stop_setup = net_state() == WIFI_SETUP;
        show_locked(SCR_SETTINGS);
        break;
    case SCR_MENU:
        show_locked(SCR_MENU);
        break;
    }
    unlock();
    if (stop_setup) {
        net_setup_stop();
    }
}

static void app_confirm(bool yes)
{
    lock();
    if (s_v.screen != SCR_CONFIRM) {
        out("ERR nothing to confirm");
    } else if (yes) {
        game_new(&s_v.game);
        show_locked(SCR_MENU);
    } else {
        show_locked(SCR_GAME);
    }
    unlock();
}

static void app_settings(const char *src)
{
    lock();
    out("SETTINGS src=%s", src);
    if (s_v.screen != SCR_SETTINGS && s_v.screen != SCR_WIFI) {
        s_settings_from = s_v.screen;
    }
    show_locked(SCR_SETTINGS);
    unlock();
    sound_play(SND_TICK);
}

static void app_set_volume(int vol, bool save)
{
    vol = vol < 0 ? 0 : vol > 100 ? 100 : vol;
    lock();
    s_v.vol = vol;
    bool mute = s_v.mute;
    unlock();
    sound_set_level(vol, mute);
    if (save) {
        settings_save_audio(vol, mute);
        out("VOL %d", vol);
        sound_play(SND_TICK);
    }
    request_redraw();
}

static void app_set_mute(bool mute)
{
    lock();
    s_v.mute = mute;
    int vol = s_v.vol;
    unlock();
    sound_set_level(vol, mute);
    settings_save_audio(vol, mute);
    out("MUTE %d", mute ? 1 : 0);
    sound_play(SND_TICK);
    request_redraw();
}

static void app_wifi_setup(void)
{
    lock();
    if (s_v.screen != SCR_SETTINGS && s_v.screen != SCR_WIFI) {
        s_settings_from = s_v.screen;
    }
    show_locked(SCR_WIFI);
    unlock();
    net_setup_start();
}

/* ---- status ---- */

static void snapshot(app_view_t *v)
{
    lock();
    *v = s_v;
    unlock();
    v->wifi = net_state();
    net_ip(v->ip, sizeof(v->ip));
}

static void print_status(void)
{
    app_view_t v;
    snapshot(&v);
    out("STATUS screen=%s game=%s bat=%d mv=%d wifi=%s ip=%s vol=%d mute=%d", SCREEN_NAMES[v.screen],
        GAMES[v.game_idx].id, v.bat_pct, v.bat_mv, net_state_name(v.wifi), v.ip[0] ? v.ip : "-", v.vol,
        v.mute ? 1 : 0);
}

void app_status_json(char *buf, size_t n)
{
    app_view_t v;
    snapshot(&v);
    snprintf(buf, n,
             "{\"app\":\"pocketgame\",\"version\":\"%s\",\"screen\":\"%s\",\"games\":%d,"
             "\"battery\":{\"mv\":%d,\"pct\":%d},\"wifi\":\"%s\",\"ip\":\"%s\",\"vol\":%d,\"mute\":%s}",
             v.version, SCREEN_NAMES[v.screen], GAME_COUNT, v.bat_mv, v.bat_pct, net_state_name(v.wifi), v.ip,
             v.vol, v.mute ? "true" : "false");
}

static void read_battery(void)
{
    int mv = battery_mv(), pct = battery_pct(mv);
    lock();
    bool changed = pct != s_v.bat_pct;
    s_v.bat_mv = mv;
    s_v.bat_pct = pct;
    bool menu = s_v.screen == SCR_MENU;
    unlock();
    if (changed && menu) {
        request_redraw();
    }
}

/* ---- serial console ---- */

typedef enum { EXPECT_CMD, EXPECT_SSID, EXPECT_PASS } expect_t;
static expect_t s_expect;
static char s_pending_ssid[SSID_MAX + 1];

/* the line after "wifi ssid" / "wifi pass": a credential, never echoed */
static void handle_secret(const char *line)
{
    expect_t e = s_expect;
    s_expect = EXPECT_CMD;
    size_t n = strlen(line);
    if (e == EXPECT_SSID) {
        if (n == 0 || n > SSID_MAX) {
            out("ERR ssid length");
            return;
        }
        strlcpy(s_pending_ssid, line, sizeof(s_pending_ssid));
        out("SSID ok");
        return;
    }
    if (n > PASS_MAX) {
        out("ERR password length");
        return;
    }
    char ssid[SSID_MAX + 1], old_pass[PASS_MAX + 1];
    if (s_pending_ssid[0]) {
        strlcpy(ssid, s_pending_ssid, sizeof(ssid));
    } else if (!settings_get_wifi(ssid, old_pass)) {
        out("ERR no ssid (send wifi ssid first)");
        return;
    }
    settings_set_wifi(ssid, line);
    s_pending_ssid[0] = '\0';
    out("WIFI saved");
    net_connect();
}

static void handle_wifi(const char *arg)
{
    if (strcmp(arg, "ssid") == 0) {
        s_expect = EXPECT_SSID;
        out("SSID?");
    } else if (strcmp(arg, "pass") == 0) {
        s_expect = EXPECT_PASS;
        out("PASS?");
    } else if (strcmp(arg, "setup") == 0) {
        app_wifi_setup();
    } else if (strcmp(arg, "stop") == 0) {
        net_setup_stop();
    } else if (strcmp(arg, "forget") == 0) {
        settings_forget_wifi();
        out("WIFI forgotten");
        net_connect();
    } else if (*arg == '\0') {
        char ip[16];
        net_ip(ip, sizeof(ip));
        out("WIFI state=%s ip=%s ap=%s", net_state_name(net_state()), ip[0] ? ip : "-", net_ap_name());
    } else {
        out("ERR wifi (ssid | pass | setup | stop | forget)");
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
    char *arg = strchr(line, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ') {
            arg++;
        }
    } else {
        arg = line + n;
    }

    if (strcmp(line, "new") == 0) {
        app_new_game("uart");
    } else if (strcmp(line, "play") == 0) {
        char *end;
        long cell = strtol(arg, &end, 10);
        if (end == arg) {
            out("ERR bad cell");
        } else {
            app_play((int)cell, "uart");
        }
    } else if (strcmp(line, "board") == 0) {
        lock();
        print_board_locked();
        print_state_locked();
        out("SCORE X=%d O=%d", s_v.wins_x, s_v.wins_o);
        unlock();
    } else if (strcmp(line, "status") == 0) {
        print_status();
    } else if (strcmp(line, "open") == 0) {
        int idx = -1;
        for (int i = 0; i < GAME_COUNT; i++) {
            if (strcmp(arg, GAMES[i].id) == 0) {
                idx = i;
            }
        }
        app_open_game(idx, "uart");
    } else if (strcmp(line, "back") == 0) {
        app_back("uart");
    } else if (strcmp(line, "yes") == 0 || strcmp(line, "no") == 0) {
        app_confirm(line[0] == 'y');
    } else if (strcmp(line, "settings") == 0) {
        app_settings("uart");
    } else if (strcmp(line, "vol") == 0) {
        char *end;
        long v = strtol(arg, &end, 10);
        if (end == arg || v < 0 || v > 100) {
            out("ERR vol 0-100");
        } else {
            app_set_volume((int)v, true);
        }
    } else if (strcmp(line, "mute") == 0) {
        if (strcmp(arg, "0") != 0 && strcmp(arg, "1") != 0) {
            out("ERR mute 0|1");
        } else {
            app_set_mute(arg[0] == '1');
        }
    } else if (strcmp(line, "wifi") == 0) {
        handle_wifi(arg);
    } else {
        out("ERR unknown command (new | play <0-8> | board | status | open ttt | back | yes | no | settings | "
            "vol <0-100> | mute <0|1> | wifi ...)");
    }
}

/* Reads the USB Serial/JTAG RX FIFO directly: the console keeps its non-blocking
 * output, so the game never stalls when no computer is listening. */
static void console_task(void *arg)
{
    char line[128];
    size_t len = 0;
    char prev = 0;
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
            bool crlf = c == '\n' && prev == '\r';
            prev = c;
            if (crlf) {
                continue;
            }
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                if (s_expect != EXPECT_CMD) {
                    handle_secret(line);
                } else {
                    handle_line(line);
                }
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            }
        }
    }
}

/* ---- display, touch, buttons, heartbeat ---- */

static void display_task(void *arg)
{
    static app_view_t v; /* off the stack */
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        snapshot(&v);
        ui_render(&v);
    }
}

/* true when the press starts a slider drag */
static bool on_press(int x, int y)
{
    app_view_t v;
    snapshot(&v);
    int arg = -1;
    hit_t h = ui_hit(&v, x, y, &arg);
    out("TAP x=%d y=%d screen=%s hit=%d arg=%d", x, y, SCREEN_NAMES[v.screen], h, arg);
    switch (h) {
    case HIT_TILE:
        app_open_game(arg, "touch");
        break;
    case HIT_CELL:
        app_play(arg, "touch");
        break;
    case HIT_NEW_GAME:
        app_new_game("touch");
        break;
    case HIT_YES:
    case HIT_NO:
        app_confirm(h == HIT_YES);
        break;
    case HIT_SLIDER:
        app_set_volume(arg, false);
        return true;
    case HIT_MUTE:
        app_set_mute(!v.mute);
        break;
    case HIT_WIFI_SETUP:
        app_wifi_setup();
        break;
    case HIT_WIFI_DONE:
        app_back("touch");
        break;
    case HIT_NONE:
        break;
    }
    return false;
}

static void touch_task(void *arg)
{
    bool was = false, drag = false;
    int last_vol = -1;
    for (;;) {
        int x = 0, y = 0;
        bool down = touch_read(&x, &y);
        if (down && !was) {
            drag = on_press(x, y);
            last_vol = drag ? ui_slider_value(x) : -1;
        } else if (down && drag) {
            int v = ui_slider_value(x);
            if (v != last_vol) {
                last_vol = v;
                app_set_volume(v, false);
            }
        } else if (!down && was && drag) {
            drag = false;
            app_set_volume(last_vol, true); /* save once, on release */
        }
        was = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void button_task(void *arg)
{
    static const int PINS[2] = {KEY_LEFT, KEY_RIGHT};
    static const char *const NAMES[2] = {"left", "right"};
    bool down[2] = {false, false};
    TickType_t down_at[2] = {0, 0};
    bool used[2] = {false, false}; /* part of a combo: no short-press action */
    TickType_t combo_at = 0;
    bool combo_fired = false;

    for (int i = 0; i < 2; i++) {
        gpio_reset_pin(PINS[i]);
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(PINS[i], GPIO_PULLUP_ONLY);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    for (int i = 0; i < 2; i++) {
        down[i] = gpio_get_level(PINS[i]) == 0;
        used[i] = true; /* held at boot: ignore until released */
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < 2; i++) {
            bool d = gpio_get_level(PINS[i]) == 0;
            if (d != down[i]) {
                vTaskDelay(pdMS_TO_TICKS(15));
                d = gpio_get_level(PINS[i]) == 0;
            }
            if (d == down[i]) {
                continue;
            }
            down[i] = d;
            if (d) {
                out("BTN %s down gpio=%d", NAMES[i], PINS[i]);
                down_at[i] = now;
                used[i] = false;
            } else {
                bool short_press = !used[i] && now - down_at[i] < pdMS_TO_TICKS(SHORT_PRESS_MS);
                if (short_press && i == 0) {
                    app_back("button");
                }
            }
        }
        if (down[0] && down[1]) {
            used[0] = used[1] = true;
            if (!combo_at) {
                combo_at = now;
            } else if (!combo_fired && now - combo_at >= pdMS_TO_TICKS(COMBO_MS)) {
                combo_fired = true;
                app_settings("buttons");
            }
        } else {
            combo_at = 0;
            combo_fired = false;
        }
    }
}

static void heartbeat_task(void *arg)
{
    for (int s = 1;; s++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s % BATTERY_S == 0) {
            read_battery();
        }
        if (s % HEARTBEAT_S == 0) {
            lock();
            screen_t scr = s_v.screen;
            int pct = s_v.bat_pct;
            unlock();
            out("HEARTBEAT up=%llds screen=%s bat=%d wifi=%s", (long long)(esp_timer_get_time() / 1000000),
                SCREEN_NAMES[scr],
                pct, net_state_name(net_state()));
        }
    }
}

void app_main(void)
{
    /* hold the battery power latch first, before the power key is released */
    gpio_reset_pin(BAT_EN);
    gpio_set_direction(BAT_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(BAT_EN, 1);

    s_lock = xSemaphoreCreateMutex();
    game_new(&s_v.game);
    s_v.screen = SCR_MENU;
    strlcpy(s_v.version, esp_app_get_description()->version, sizeof(s_v.version));
    /* keep the console for the protocol (and Wi-Fi logs would print the network name) */
    esp_log_level_set("*", ESP_LOG_WARN);

    /* give the host time to reopen the USB serial port after a reset */
    vTaskDelay(pdMS_TO_TICKS(1500));
    out("POCKETGAME BOOT board=ESP32-S3-LCD-1.54 version=%s", s_v.version);

    esp_err_t err = display_init();
    if (err == ESP_OK) {
        out("LCD OK driver=ST7789 240x240 sclk=%d sda=%d cs=%d dc=%d rst=%d bl=%d", LCD_SCLK, LCD_MOSI, LCD_CS,
            LCD_DC, LCD_RST, LCD_BL);
    } else {
        out("LCD FAIL err=%s", esp_err_to_name(err));
    }

    err = i2cbus_init();
    if (err != ESP_OK) {
        out("I2C FAIL err=%s", esp_err_to_name(err));
    }
    bool touch = err == ESP_OK && touch_init();
    i2cbus_scan();
    if (touch) {
        out("TOUCH OK chip=%s addr=0x%02x sda=%d scl=%d rst=%d", touch_chip(), touch_addr(), I2C_SDA, I2C_SCL,
            TP_RST);
    } else {
        out("TOUCH NONE: no touch controller answered on I2C");
    }

    settings_init();
    settings_load_audio(&s_v.vol, &s_v.mute);
    sound_set_level(s_v.vol, s_v.mute);
    out("AUDIO %s vol=%d mute=%d", err == ESP_OK ? sound_init() : "none (no i2c)", s_v.vol, s_v.mute ? 1 : 0);

    if (!battery_init()) {
        out("BAT adc unavailable");
    }
    s_v.bat_mv = battery_mv();
    s_v.bat_pct = battery_pct(s_v.bat_mv);
    out("BAT mv=%d pct=%d adc=io%d", s_v.bat_mv, s_v.bat_pct, BAT_ADC);

    if (!net_init()) {
        out("WIFI init failed");
    }

    xTaskCreate(display_task, "display", 4096, NULL, 4, &s_display_task);
    xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL);
    if (touch) {
        xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);
    }
    xTaskCreate(heartbeat_task, "heartbeat", 3072, NULL, 3, NULL);

    lock();
    show_locked(SCR_MENU);
    unlock();

    out("READY turn=X");
    xTaskCreate(console_task, "console", 4096, NULL, 6, NULL);
    /* this build booted: keep it (matters once it arrives over the air) */
    esp_ota_mark_app_valid_cancel_rollback();
}
