/* PocketGame: a small multi-game handheld on the ESP32-S3-LCD-1.54 touch board.
 *
 * Screens: home menu (one tile per game, top bar with battery % and Wi-Fi), the
 * games (tic-tac-toe, Connect 4, the IMU games Tilt Duel, Hot Potato and Steady Hand,
 * Draw & Guess, Coin Platformer and Defi Secousse, modules in game/), a leave-game confirmation,
 * Settings (volume, where 0 is silent, Wi-Fi, updates), the Wi-Fi setup hotspot screen,
 * the update screen (GitHub check, download progress) and a game's rules screen.
 *
 * Buttons: a short press on LEFT goes back (asking first while a game is running);
 * holding LEFT + RIGHT for 3 s opens Settings. The middle button is only the power
 * key: holding it 3 s powers the board off (or sleeps it on USB). The home menu's
 * "hold L + R" banner slides down off the screen 3 s after the menu opens, and the
 * game list grows into the space it leaves.
 *
 * Idle power-off, on battery only: 60 s without a touch or a button press (a running
 * motion game's tilt counts too; the console does not) shows the off screen and
 * releases the battery latch. On USB power the board stays on with the screen lit: a
 * computer is recognised by its start-of-frame packets, a plain charger by the board
 * still running once the latch is released (the off screen shows once, then it comes
 * back and later timeouts only check the latch quietly).
 *
 * Serial protocol (USB Serial/JTAG console, one command per line):
 *   new | board                a new round / the board of the game on screen (tic-tac-toe
 *                              from the other screens): READY turn=X (who starts:
 *                              alternates after each finished round), BOARD ..., TURN O
 *   play <0-8>                 tic-tac-toe (switches to it): WIN X line=a,b,c | DRAW,
 *                              ERR occupied | ERR game over | ERR bad cell
 *   drop <0-6> [<0-6> ...]     Connect 4 (switches to it): WIN X cells=a,b,c,d | DRAW,
 *                              ERR range | ERR full column n | ERR game over
 *   maze [go] | pass | hold [go]   Tilt Duel / Hot Potato / Steady Hand (switch to it):
 *                              the state, or a tap (start the run / pass the potato)
 *   sim tilt <ax> <ay> <ms>    motion game on screen: <ms> of a made-up screen tilt in mg
 *   sim shake | sim wait <ms>  a shake / time passing on a level board; sim <key> <val>
 *                              sets a game value (sim fuse <ms>, sim seed <n>: the next
 *                              Tilt Duel maze); sim solve rolls the Tilt Duel ball out of
 *                              the maze. Replies with the game's
 *                              events and one state line (BALL | BUBBLE | POTATO ...);
 *                              from then on the round ignores the sensor and the clock
 *   done | reveal | guess <0-2> | undo | clear | start   Draw & Guess (switch to it):
 *                              SCREEN handover, CHOICES a,b,c correct=n word=w, RESULT
 *                              correct|wrong, SCORE X=n O=n, TURN drawer=O; sim word <idx>,
 *                              sim correct <0-2>, sim stroke (WORD | SIM | STROKE ...)
 *   level [jump]               Coin Platformer (switches to it): LEVEL plats=n coins=n id=hex,
 *                              PLAYER ..., or a tap: JUMP X | ERR airborne; sim jump,
 *                              sim move L|R <ms>, sim autoplay (collects every coin); events
 *                              LAND ground|plat=n, COIN X idx=n n=n + SCORE X=n O=n,
 *                              FINISH X coins=n left=ns points=n, TURN O + RUN O id=hex
 *                              same=yes | WIN | DRAW (by points: 10 a coin + 1 a second left).
 *                              On the board tilting walks, RIGHT jumps (or starts the next round)
 *   dare get <idx> | dare count | pass   Defi Secousse (switches to it): DARE idx=n text=..,
 *                              DARES n=..; pass after a dare: PASS X, TURN O | ERR no dare;
 *                              sim shake <ms> (steady shaking): SHAKE X ms=.. need=.. |
 *                              DARE X idx=n left=n text=.. | ERR dare shown; sim cycle draws a
 *                              whole deck: CYCLE draws=n unique=n total=n. On the board a tap
 *                              or RIGHT hands it on. "pass" goes to the game on screen first
 *   text                       TEXT screen=<name> <every string the screen shows, " | " between>
 *                              (the UI is French, upper case without accents; the protocol
 *                              stays English; STATUS ... lang=fr)
 *   sim idle <ms> | sim touch  any screen: the idle timer jumps ahead (SIM idle=ms) / a
 *                              touch (INPUT touch); on USB a timeout replies
 *                              POWER usb: auto-off disabled and the board stays on
 *   imu                        IMU ax=.. ay=.. az=.. raw_ax=.. (mg; ax is the games' X axis,
 *                              the raw one inverted) | ERR imu not found
 *   rules                      the help screen of the game on screen (Hot Potato):
 *                              SCREEN rules game=potato, RULES fuse=8-20s hold=2s ...
 *   ota <http(s)://url>        flashes that image into the other app slot and restarts:
 *                              OTA start .., OTA progress pct=n, OTA done | OTA FAIL reason=..
 *   ota status                 OTA status state=.. running=ota_N next=ota_N fw=..
 *   update check | install     the latest GitHub release of seblanc/pocketgame:
 *                              UPDATE result=newer|same|older latest=.. current=.. | UPDATE ERR ..;
 *                              install flashes its pocketgame.bin
 *   status                     STATUS screen=menu bat=.. mv=.. wifi=.. ip=.. starter=X vol=.. fw=v..
 *                              idle=s/60s power=on|off autooff=on|off (off: on USB)
 *                              hint=shown|sliding|hidden
 *   open <id> | back | yes | no | settings      navigation, replies SCREEN ...
 *   vol <0-100>                                 VOL n (saved in NVS, 0 = silent)
 *   wifi [ssid | pass | scan | setup | stop | forget]  ssid/pass ask SSID? / PASS? and
 *                                               read the next line as the value; scan
 *                                               replies WIFI scan n=<count>, one line per AP
 */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "battery.h"
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_app_desc.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "game/connect4.h"
#include "game/dare.h"
#include "game/drawguess.h"
#include "game/potato.h"
#include "game/steady.h"
#include "game/tictactoe.h"
#include "game/plat.h"
#include "game/tilt.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/usb_serial_jtag_struct.h"
#include "i2cbus.h"
#include "imu.h"
#include "net.h"
#include "ota.h"
#include "scroll.h"
#include "settings.h"
#include "sound.h"
#include "touch.h"
#include "ui.h"

#define COMBO_MS 3000
#define SHORT_PRESS_MS 1000
#define POWER_HOLD_MS 3000
#define HEARTBEAT_S 10
#define BATTERY_S 20
#define IDLE_OFF_MS 60000
#define HINT_SHOW_MS 3000
#define HINT_SLIDE_MS 400
#define TIMER_MS 30 /* the hint animation and the idle check */

/* the home menu tiles, in order; the first one is the default game */
const game_def_t *const GAMES[] = {&TICTACTOE, &CONNECT4, &TILT_DUEL, &HOT_POTATO, &STEADY_HAND, &DRAW_GUESS,
                                     &COIN_PLATFORMER, &DEFI_SECOUSSE};
const int GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);
_Static_assert(sizeof(GAMES) / sizeof(GAMES[0]) <= GAME_MAX, "raise GAME_MAX");

static const char *const SCREEN_NAMES[] = {"menu", "game", "confirm", "settings", "wifi", "off", "rules", "update"};

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_display_task;
static app_view_t s_v;                    /* under s_lock */
static screen_t s_settings_from = SCR_MENU; /* where Settings returns to */
static bool s_sim[GAME_MAX];              /* under s_lock: the round is driven over UART */

/* power: on; off = the idle power-off (or its latch check) is under way */
typedef enum { PWR_ON, PWR_OFF } power_t;
static const char *const POWER_NAMES[] = {"on", "off"};
static power_t s_power;     /* under s_lock */
static uint32_t s_input_ms; /* under s_lock: the last touch or key, the idle timer's start */
/* USB power: a computer sent a start-of-frame within USB_SOF_MS, or the board kept
 * running with its battery latch released (a charger) */
#define USB_SOF_MS 3000
static bool s_sof_seen;      /* under s_lock */
static uint32_t s_sof_ms;    /* under s_lock: the last start-of-frame seen */
static bool s_usb_charger;   /* under s_lock */
static bool usb_power_locked(uint32_t now);

/* the menu hint: shown when the menu opens, slides off after HINT_SHOW_MS */
typedef enum { HINT_SHOWN, HINT_SLIDING, HINT_HIDDEN } hint_t;
static const char *const HINT_NAMES[] = {"shown", "sliding", "hidden"};
static hint_t s_hint = HINT_HIDDEN; /* under s_lock */
static uint32_t s_hint_at;  /* under s_lock */

uint32_t game_random(void) { return esp_random(); }

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

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

/* "text": the next frame also lists every string it draws, printed as one TEXT line */
static volatile bool s_text_req;

static void request_text(void)
{
    s_text_req = true;
    request_redraw();
}

void app_net_changed(void) { request_redraw(); }
void app_ota_changed(void) { request_redraw(); }

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* ---- screens ---- */

static void show_locked(screen_t s)
{
    s_v.screen = s;
    s_v.preview = -1;
    if (s == SCR_GAME) {
        out("SCREEN game=%s", GAMES[s_v.game_idx]->id);
    } else if (s == SCR_RULES) {
        out("SCREEN rules game=%s", GAMES[s_v.game_idx]->id);
        out("RULES %s", GAMES[s_v.game_idx]->rules_uart);
    } else {
        out("SCREEN %s", SCREEN_NAMES[s]);
    }
    if (s == SCR_MENU) {
        out("MENU games=%d", GAME_COUNT);
        s_hint = HINT_SHOWN;
        s_hint_at = now_ms();
        s_v.hint_dy = 0;
        out("HINT settings show");
    }
    request_redraw();
}

/* the game shown (or last shown) and its state */
static game_slot_t *slot_locked(void) { return &s_v.games[s_v.game_idx]; }
static const game_round_t *round_locked(void) { return game_round(slot_locked()->st.bytes); }

static bool game_running_locked(void) { return round_locked()->status == GAME_PLAYING && round_locked()->moves > 0; }

/* a game's commands work from any screen and bring that game up */
static void ensure_game_locked(int idx)
{
    if (s_v.screen != SCR_GAME || s_v.game_idx != idx) {
        s_v.game_idx = idx;
        show_locked(SCR_GAME);
    }
}

static void print_board_locked(void) { GAMES[s_v.game_idx]->print(slot_locked()->st.bytes); }

/* a fresh board; the starter alternates after every finished round (an abandoned
 * round is replayed by the same starter) */
static void new_round_locked(void)
{
    GAMES[s_v.game_idx]->new_round(slot_locked()->st.bytes, slot_locked()->next_starter);
    s_sim[s_v.game_idx] = false; /* the sensor and the clock drive the new round */
}

static void print_ready_locked(void)
{
    const char *key = GAMES[s_v.game_idx]->starter_key;
    out("READY %s=%c", key ? key : "turn", game_mark_char(round_locked()->starter));
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
    bool fresh = round_locked()->status != GAME_PLAYING;
    if (fresh) {
        new_round_locked();
    }
    show_locked(SCR_GAME);
    if (fresh) {
        print_ready_locked();
    }
    print_board_locked();
    unlock();
    sound_play(SND_TICK);
}

/* new: the game on screen (or behind the leave confirmation), else the first game */
static void app_new_game(const char *src)
{
    lock();
    bool on_game = s_v.screen == SCR_GAME || s_v.screen == SCR_CONFIRM || s_v.screen == SCR_RULES;
    ensure_game_locked(on_game ? s_v.game_idx : 0);
    new_round_locked();
    out("NEW src=%s", src);
    print_ready_locked();
    print_board_locked();
    unlock();
    sound_play(SND_NEW);
    request_redraw();
}

/* after a move: a finished round is scored and the other player starts the next one */
static void moved_locked(game_event_t ev)
{
    game_slot_t *s = slot_locked();
    const game_round_t *r = round_locked();
    if (ev == GEV_WIN || ev == GEV_DRAW || ev == GEV_BOOM) {
        s->next_starter = game_other(r->starter);
    }
    if (ev == GEV_WIN || ev == GEV_BOOM) {
        if (r->winner == CELL_X) {
            s->wins_x++;
        } else {
            s->wins_o++;
        }
    }
    if ((ev == GEV_WIN || ev == GEV_DRAW || ev == GEV_BOOM) && GAMES[s_v.game_idx]->finished) {
        GAMES[s_v.game_idx]->finished(s->st.bytes, s->wins_x, s->wins_o, s->next_starter);
    }
}

static void play_event(game_event_t ev)
{
    static const sound_t SOUNDS[] = {
        [GEV_MOVE_X] = SND_X, [GEV_MOVE_O] = SND_O, [GEV_BAD] = SND_BAD,
        [GEV_WIN] = SND_WIN,  [GEV_DRAW] = SND_DRAW, [GEV_BOOM] = SND_BOOM,
        [GEV_RUN_END] = SND_END, [GEV_DARE] = SND_DARE,
    };
    if (ev != GEV_NONE) {
        sound_play(SOUNDS[ev]);
    }
    if (ev != GEV_NONE && ev != GEV_BAD) {
        request_redraw();
    }
}

/* one move (a tap) in game idx */
static void app_move(int idx, int m, const char *src)
{
    lock();
    ensure_game_locked(idx);
    game_event_t ev = GAMES[idx]->move(slot_locked()->st.bytes, m, src);
    moved_locked(ev);
    unlock();
    play_event(ev);
}

/* the console command of game idx (its verb) with its arguments */
static void app_game_command(int idx, const char *arg)
{
    lock();
    ensure_game_locked(idx);
    game_event_t ev = GAMES[idx]->command(slot_locked()->st.bytes, arg);
    moved_locked(ev);
    unlock();
    play_event(ev);
    request_redraw(); /* a list of moves may end in a rejected one */
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
            new_round_locked();
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
    case SCR_RULES:
        show_locked(SCR_GAME);
        break;
    case SCR_UPDATE:
        show_locked(SCR_SETTINGS); /* a download goes on in the background */
        break;
    case SCR_MENU:
        show_locked(SCR_MENU);
        break;
    case SCR_OFF:
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
        new_round_locked();
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
    if (s_v.screen != SCR_SETTINGS && s_v.screen != SCR_WIFI && s_v.screen != SCR_UPDATE) {
        s_settings_from = s_v.screen;
    }
    show_locked(SCR_SETTINGS);
    unlock();
    sound_play(SND_TICK);
}

/* save: store in NVS and report VOL n; tick: play the tick at the new level */
static void app_set_volume(int vol, bool save, bool tick)
{
    vol = vol < 0 ? 0 : vol > 100 ? 100 : vol;
    lock();
    s_v.vol = vol;
    unlock();
    sound_set_level(vol); /* 0 = silent: there is no separate mute */
    if (save) {
        settings_save_volume(vol);
        out("VOL %d", vol);
    }
    if (tick) {
        sound_play(SND_TICK);
    }
    request_redraw();
}

/* the help screen of the game on screen (Hot Potato) */
static void app_rules(const char *src)
{
    lock();
    const game_def_t *g = GAMES[s_v.game_idx];
    if ((s_v.screen != SCR_GAME && s_v.screen != SCR_RULES) || !g->rules) {
        unlock();
        out("ERR no rules here (open potato or dare first)");
        return;
    }
    out("RULES src=%s", src);
    show_locked(SCR_RULES); /* the round waits: motion only runs on the game screen */
    unlock();
    sound_play(SND_TICK);
}

/* Settings > CHECK: the update screen, and the GitHub check */
static void app_update_check(const char *src)
{
    lock();
    if (s_v.screen != SCR_SETTINGS && s_v.screen != SCR_WIFI && s_v.screen != SCR_UPDATE) {
        s_settings_from = s_v.screen;
    }
    if (s_v.screen != SCR_UPDATE) {
        show_locked(SCR_UPDATE);
    }
    unlock();
    sound_play(SND_TICK);
    ota_check(src);
}

/* the update screen's right-hand button: install a newer release, else check again */
static void app_update_go(const char *src)
{
    ota_view_t o;
    ota_get_view(&o);
    if (o.state == OTA_IDLE && o.check == UPD_NEWER && o.has_asset) {
        sound_play(SND_TICK);
        ota_install_latest(src);
    } else {
        app_update_check(src);
    }
}

static void app_wifi_setup(void)
{
    lock();
    if (s_v.screen != SCR_SETTINGS && s_v.screen != SCR_WIFI && s_v.screen != SCR_UPDATE) {
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
    ota_get_view(&v->ota);
}

/* seconds without a touch or key */
static unsigned idle_s(void)
{
    lock();
    uint32_t ms = now_ms() - s_input_ms;
    unlock();
    return ms / 1000;
}

static void print_status(void)
{
    app_view_t v;
    snapshot(&v);
    lock();
    power_t pwr = s_power;
    hint_t hint = s_hint;
    bool usb = usb_power_locked(now_ms());
    unlock();
    out("STATUS screen=%s game=%s bat=%d mv=%d wifi=%s ip=%s starter=%c vol=%d fw=%s idle=%u/%ds power=%s autooff=%s "
        "hint=%s lang=fr",
        SCREEN_NAMES[v.screen], GAMES[v.game_idx]->id, v.bat_pct, v.bat_mv, net_state_name(v.wifi),
        v.ip[0] ? v.ip : "-", game_mark_char(game_round(v.games[v.game_idx].st.bytes)->starter), v.vol, FW_VERSION,
        idle_s(), IDLE_OFF_MS / 1000, POWER_NAMES[pwr], usb ? "off" : "on", HINT_NAMES[hint]);
}

void app_status_json(char *buf, size_t n)
{
    app_view_t v;
    snapshot(&v);
    lock();
    power_t pwr = s_power;
    bool usb = usb_power_locked(now_ms());
    unlock();
    snprintf(buf, n,
             "{\"app\":\"pocketgame\",\"fw\":\"%s\",\"version\":\"%s\",\"screen\":\"%s\",\"games\":%d,"
             "\"battery\":{\"mv\":%d,\"pct\":%d},\"wifi\":\"%s\",\"ip\":\"%s\",\"vol\":%d,\"idle_s\":%u,\"power\":\"%s\","
             "\"autooff\":%s}",
             FW_VERSION, v.version, SCREEN_NAMES[v.screen], GAME_COUNT, v.bat_mv, v.bat_pct, net_state_name(v.wifi), v.ip,
             v.vol, idle_s(), POWER_NAMES[pwr], usb ? "false" : "true");
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

/* ---- power: idle power-off ---- */

/* Every timer tick, under the lock: a computer's host sends a start-of-frame every
 * millisecond, whose 11-bit number the USB Serial/JTAG block keeps; a charger sends none. */
static void usb_sof_watch_locked(uint32_t now)
{
    static bool started;
    static uint32_t last;
    uint32_t f = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    if (started && f != last) {
        s_sof_seen = true;
        s_sof_ms = now;
    }
    started = true;
    last = f;
}

/* on USB power: no idle power-off, no dark display */
static bool usb_power_locked(uint32_t now)
{
    return s_usb_charger || (s_sof_seen && now - s_sof_ms < USB_SOF_MS);
}

/* With the latch released but USB power on: sleep until the power key is pressed.
 * Does not return (the wake-up is a fresh boot). */
static void sleep_until_power_key(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_hold_en(LCD_BL);
    gpio_hold_en(BAT_EN);
    gpio_deep_sleep_hold_en();
    rtc_gpio_pullup_en(KEY_PWR);
    rtc_gpio_pulldown_dis(KEY_PWR);
    esp_sleep_enable_ext0_wakeup(KEY_PWR, 0);
    esp_deep_sleep_start();
}

/* A touch or key press: restarts the idle timer. Returns true when it was used up by
 * the power state (the board is switching off), so it must not act on the screen. */
static bool app_input(const char *src)
{
    (void)src;
    lock();
    power_t was = s_power;
    if (was == PWR_ON) {
        s_input_ms = now_ms();
    }
    unlock();
    return was != PWR_ON;
}

/* activity that keeps the board on but never wakes it (motion games, downloads) */
static void keep_awake(void)
{
    lock();
    if (s_power == PWR_ON) {
        s_input_ms = now_ms();
    }
    unlock();
}

/* 60 s without input and no computer seen on USB: the off screen, then the latch is
 * released, so on battery the power goes. Still running afterwards means a charger:
 * latched again, the screen comes back and auto-off stays disabled. A known charger
 * is only checked quietly (screen lit), in case it was pulled since. */
static void idle_power_off(void)
{
    lock();
    bool quiet = s_usb_charger;
    screen_t was = s_v.screen;
    if (!quiet) {
        s_v.off_idle = true;
        show_locked(SCR_OFF);
    }
    unlock();
    if (!quiet) {
        out("POWER off reason=idle after=%ds", IDLE_OFF_MS / 1000);
        sound_play(SND_TICK);
        vTaskDelay(pdMS_TO_TICKS(1500)); /* let the off screen be seen */
        gpio_set_level(LCD_BL, 0);
    }
    gpio_set_level(BAT_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(800));
    /* still running: powered over USB */
    gpio_set_level(BAT_EN, 1);
    gpio_set_level(LCD_BL, 1);
    lock();
    s_usb_charger = true;
    if (!quiet) {
        s_v.screen = was; /* quietly: the menu keeps its hint state */
        s_v.off_idle = false;
    }
    s_input_ms = now_ms();
    s_power = PWR_ON;
    unlock();
    out("POWER usb: auto-off disabled");
    request_redraw();
}

/* the menu hint's slide (ease-in) and the idle timer */
static void timer_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TIMER_MS));
        ota_view_t o;
        ota_get_view(&o);
        if (o.state == OTA_CHECKING || o.state == OTA_DOWNLOADING || o.state == OTA_REBOOTING ||
            net_state() == WIFI_SETUP) {
            keep_awake(); /* a download or the phone setup takes as long as it takes */
        }

        bool redraw = false;
        lock();
        uint32_t now = now_ms(); /* under the lock: never before a time another task just set */
        usb_sof_watch_locked(now);
        if (s_v.screen == SCR_MENU && s_hint != HINT_HIDDEN) {
            uint32_t t = now - s_hint_at;
            int dy = 0;
            if (t > HINT_SHOW_MS) {
                s_hint = HINT_SLIDING;
                float p = (float)(t - HINT_SHOW_MS) / HINT_SLIDE_MS;
                dy = p >= 1 ? HINT_SLIDE_PX : (int)(p * p * HINT_SLIDE_PX);
            }
            if (dy >= HINT_SLIDE_PX) {
                dy = HINT_SLIDE_PX;
                s_hint = HINT_HIDDEN;
                out("HINT settings hide ms=%lu", (unsigned long)t);
                int from, h, content_h;
                ui_list_size(SCR_MENU, 0, &from, &content_h);
                ui_list_size(SCR_MENU, HINT_SLIDE_PX, &h, &content_h);
                out("MENU list grow h=%d from=%d", h, from);
            }
            redraw = dy != s_v.hint_dy;
            s_v.hint_dy = dy;
        }
        bool idle = s_power == PWR_ON && now - s_input_ms >= IDLE_OFF_MS;
        bool usb = idle && usb_power_locked(now) && !s_usb_charger; /* a charger is checked below */
        if (usb) {
            idle = false;
            s_input_ms = now; /* the timer starts over; the board stays on, screen lit */
        } else if (idle) {
            s_power = PWR_OFF;
        }
        unlock();
        if (redraw) {
            request_redraw();
        }
        if (usb) {
            out("POWER usb: auto-off disabled");
        }
        if (idle) {
            idle_power_off();
        }
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
    } else if (strcmp(arg, "scan") == 0) {
        net_scan();
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
        out("ERR wifi (ssid | pass | scan | setup | stop | forget)");
    }
}

#define SIM_STEP_MS 20

/* sim ...: drives the motion game on screen with made-up input on a simulated clock */
static void handle_sim(const char *arg)
{
    char key[12] = "";
    long a = 0, b = 0, c = 0;
    int n = sscanf(arg, "%11s %ld %ld %ld", key, &a, &b, &c);
    /* the idle timer, on any screen */
    if (strcmp(key, "idle") == 0 && n == 2 && a >= 0 && a <= 600000) {
        lock();
        s_input_ms -= (uint32_t)a;
        unlock();
        out("SIM idle=%ld", a);
        return;
    }
    if (strcmp(key, "touch") == 0 && n == 1) {
        out("INPUT touch");
        app_input("touch");
        return;
    }
    lock();
    const game_def_t *g = GAMES[s_v.game_idx];
    if (s_v.screen != SCR_GAME || !g->step) {
        unlock();
        out("ERR sim needs a motion game on screen (open tilt | potato | steady | draw | plat | dare)");
        return;
    }
    motion_t m = {.sim = true};
    long ms = 0;
    bool solve = false;
    char side = 0;
    if (strcmp(key, "jump") == 0 && n == 1 && g->steer) {
        /* a tap of the steering game (Coin Platformer): JUMP X | ERR airborne */
        s_sim[s_v.game_idx] = true;
        game_event_t ev = g->move(slot_locked()->st.bytes, 0, "sim");
        moved_locked(ev);
        unlock();
        play_event(ev);
        request_redraw();
        return;
    }
    if (strcmp(key, "move") == 0 && g->steer && sscanf(arg, "%*s %c %ld", &side, &a) == 2 &&
        (side == 'L' || side == 'R' || side == 'l' || side == 'r') && a >= 0 && a <= 600000) {
        /* LEFT or RIGHT held for a while */
        m.dir = side == 'L' || side == 'l' ? -1 : 1;
        ms = a;
    } else if ((strcmp(key, "solve") == 0 || strcmp(key, "autoplay") == 0) && n == 1 && g->autopilot) {
        solve = true;
        ms = 65000; /* past the 60 s limit of a run */
    } else if (strcmp(key, "tilt") == 0 && n == 4 && c >= 0 && c <= 600000) {
        m.tx = (int)a;
        m.ty = (int)b;
        ms = c;
    } else if (strcmp(key, "wait") == 0 && n == 2 && a >= 0 && a <= 600000) {
        ms = a;
    } else if (strcmp(key, "shake") == 0 && n == 1) {
        m.shake = true;
    } else if (strcmp(key, "shake") == 0 && n == 2 && a > 0 && a <= 600000) {
        /* <ms> of steady shaking (Defi Secousse), starting with a jolt */
        m.shake = true;
        m.shaking = true;
        ms = a;
    } else if ((n == 1 || n == 2) && g->sim_set && g->sim_set(slot_locked()->st.bytes, key, n == 2 ? a : -1)) {
        /* a key without a value gets -1 (Draw & Guess: sim stroke) */
        s_sim[s_v.game_idx] = true;
        unlock();
        request_redraw();
        return;
    } else {
        unlock();
        out("ERR sim (tilt <ax> <ay> <ms> | shake [<ms>] | wait <ms>%s | idle <ms> | touch)",
            g->steer       ? " | jump | move L|R <ms> | autoplay"
            : g->autopilot ? " | seed <n> | solve"
            : g->timed      ? " | word <idx> | correct <0-2> | stroke"
            : g->right_taps ? " | cycle"
            : g->sim_set    ? " | fuse <ms>"
                            : "");
        return;
    }
    s_sim[s_v.game_idx] = true;
    void *st = slot_locked()->st.bytes;
    game_event_t last = GEV_NONE;
    /* in steps like the live sensor's; a run that ends (or a shake) stops the rest */
    do {
        m.dt_ms = ms < SIM_STEP_MS ? (uint32_t)ms : SIM_STEP_MS;
        ms -= m.dt_ms;
        if (solve) {
            g->autopilot(st, &m);
        }
        game_event_t ev = g->step(st, &m);
        m.shake = false;
        if (ev != GEV_NONE) {
            moved_locked(ev);
            last = ev;
            break;
        }
    } while (ms > 0);
    g->report(st);
    unlock();
    play_event(last);
    request_redraw();
}

static void handle_imu(void)
{
    int raw_ax, ay, az;
    if (!imu_read_raw_mg(&raw_ax, &ay, &az)) {
        out("ERR imu not found");
        return;
    }
    /* the games' X axis is the raw one inverted (IMU_INVERT_X); one reading for both */
    int ax = IMU_INVERT_X ? -raw_ax : raw_ax;
    int tx, ty;
    imu_screen_tilt(ax, ay, &tx, &ty);
    out("IMU ax=%d ay=%d az=%d raw_ax=%d tilt=%d,%d", ax, ay, az, raw_ax, tx, ty);
}

/* ota <url> | ota status */
static void handle_ota(const char *arg)
{
    if (*arg == '\0') {
        out("ERR usage (ota <http(s)://host/fw.bin> | ota status)");
    } else if (strcmp(arg, "status") == 0) {
        ota_print_status();
    } else {
        ota_start_url(arg, "uart");
    }
}

/* update [check | install] */
static void handle_update(const char *arg)
{
    if (strcmp(arg, "check") == 0) {
        ota_check("uart");
    } else if (strcmp(arg, "install") == 0) {
        ota_install_latest("uart");
    } else if (*arg == '\0') {
        ota_print_status();
    } else {
        out("ERR update (check | install)");
    }
}

/* line (with its arguments arg) as a console verb of game i; false if it is not one */
static bool game_verb(int i, const char *line, const char *arg)
{
    if (strcmp(line, GAMES[i]->verb) == 0) {
        app_game_command(i, arg);
        return true;
    }
    for (const char *const *v = GAMES[i]->more_verbs; v && *v; v++) {
        if (strcmp(line, *v) == 0) {
            char whole[64];
            snprintf(whole, sizeof(whole), "%s %s", line, arg);
            app_game_command(i, whole);
            return true;
        }
    }
    return false;
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
    } else if (strcmp(line, "board") == 0) {
        lock();
        if (s_v.screen != SCR_GAME && s_v.screen != SCR_CONFIRM) {
            s_v.game_idx = 0;
        }
        print_board_locked();
        out("SCORE X=%d O=%d", slot_locked()->wins_x, slot_locked()->wins_o);
        unlock();
    } else if (strcmp(line, "status") == 0) {
        print_status();
    } else if (strcmp(line, "open") == 0) {
        int idx = -1;
        for (int i = 0; i < GAME_COUNT; i++) {
            if (strcmp(arg, GAMES[i]->id) == 0) {
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
            app_set_volume((int)v, true, true);
        }
    } else if (strcmp(line, "wifi") == 0) {
        handle_wifi(arg);
    } else if (strcmp(line, "sim") == 0) {
        handle_sim(arg);
    } else if (strcmp(line, "imu") == 0) {
        handle_imu();
    } else if (strcmp(line, "rules") == 0) {
        app_rules("uart");
    } else if (strcmp(line, "ota") == 0) {
        handle_ota(arg);
    } else if (strcmp(line, "update") == 0) {
        handle_update(arg);
    } else if (strcmp(line, "text") == 0) {
        request_text();
    } else {
        /* a verb two games share ("pass") goes to the game on screen first */
        lock();
        int shown = s_v.screen == SCR_GAME ? s_v.game_idx : -1;
        unlock();
        if (shown >= 0 && game_verb(shown, line, arg)) {
            return;
        }
        for (int i = 0; i < GAME_COUNT; i++) {
            if (game_verb(i, line, arg)) {
                return;
            }
        }
        out("ERR unknown command (new | play <0-8> | drop <0-6> ... | maze | pass | hold | done | reveal | guess <0-2> | "
            "level | dare get <n>|count | sim ... | imu | rules | board | status | text | "
            "open ttt|c4|tilt|potato|steady|draw|plat|dare | back | yes | no | "
            "settings | vol <0-100> | wifi ... | ota <url>|status | update check|install)");
    }
}

/* Reads the USB Serial/JTAG RX FIFO directly: the console keeps its non-blocking
 * output, so the game never stalls when no computer is listening. */
static void console_task(void *arg)
{
    char line[300]; /* room for an "ota <url>" */
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
    static char text[1200];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        snapshot(&v);
        bool capture = s_text_req;
        if (capture) {
            s_text_req = false;
            gfx_text_capture(text, sizeof(text));
        }
        ui_render(&v);
        if (capture) {
            gfx_text_capture(NULL, 0);
            /* longer than app_out() takes: written here in one piece */
            static char line[sizeof(text) + 40];
            int n = snprintf(line, sizeof(line), "TEXT screen=%s %s\n", SCREEN_NAMES[v.screen], text);
            if (n > (int)sizeof(line) - 1) {
                n = sizeof(line) - 1;
                line[n - 1] = '\n';
            }
            fwrite(line, 1, n, stdout);
            fflush(stdout);
        }
    }
}

/* the move aimed in a drag game (shown as its preview), -1 for none */
static void set_preview(int m)
{
    lock();
    bool changed = s_v.preview != m;
    s_v.preview = m;
    unlock();
    if (changed) {
        request_redraw();
    }
}

/* the finger on a drawing game's canvas: down, move or up at x,y */
static void app_pen(int phase, int x, int y)
{
    lock();
    const game_def_t *g = GAMES[s_v.game_idx];
    bool changed = s_v.screen == SCR_GAME && g->pen && g->pen(slot_locked()->st.bytes, phase, x, y);
    unlock();
    if (changed) {
        request_redraw();
    }
}

/* Runs what a press does at once. Returns the hit for the touch task to finish: the
 * slider (a drag), aiming in a drag game (moves on release) and list taps (tiles,
 * SET UP), which act on release unless the finger scrolled the list. */
static hit_t on_press(int x, int y, int *arg)
{
    app_view_t v;
    snapshot(&v);
    *arg = -1;
    hit_t h = ui_hit(&v, x, y, arg);
    out("TAP x=%d y=%d screen=%s hit=%d arg=%d", x, y, SCREEN_NAMES[v.screen], h, *arg);
    switch (h) {
    case HIT_TILE:
    case HIT_WIFI_SETUP:
    case HIT_UPDATE_CHECK:
    case HIT_SLIDER:
        return h;
    case HIT_RULES:
        app_rules("touch");
        break;
    case HIT_RULES_DONE:
    case HIT_UPDATE_BACK:
        app_back("touch");
        break;
    case HIT_UPDATE_GO:
        app_update_go("touch");
        break;
    case HIT_AIM:
        set_preview(*arg);
        return h;
    case HIT_PEN:
        app_pen(GAME_PEN_DOWN, x, y);
        return h;
    case HIT_MOVE:
        app_move(v.game_idx, *arg, "touch");
        break;
    case HIT_NEW_GAME:
        app_new_game("touch");
        break;
    case HIT_YES:
    case HIT_NO:
        app_confirm(h == HIT_YES);
        break;
    case HIT_WIFI_DONE:
        app_back("touch");
        break;
    case HIT_NONE:
        break;
    }
    return HIT_NONE;
}

/* Slider ticks follow the finger but never closer than TICK_MIN_MS, so a fast drag
 * gives a steady ~11/s instead of a buzz, and only when the value changes. The lists
 * (home menu, Settings) scroll silently. */
#define TICK_MIN_MS 90
#define TAP_SLOP_PX 8   /* more finger travel than this is a scroll, not a tap */
#define SLIDER_DEAD_PX 2 /* touch jitter on a resting finger does not move the volume */
#define TOUCH_MS 20

static void play_tick(uint32_t *last)
{
    uint32_t now = now_ms();
    if (now - *last >= TICK_MIN_MS) {
        *last = now;
        sound_play(SND_TICK);
    }
}

static scroller_t s_lists[2]; /* menu, settings: touch task only */

static scroller_t *list_of(screen_t s)
{
    return s == SCR_MENU ? &s_lists[0] : s == SCR_SETTINGS ? &s_lists[1] : NULL;
}

/* hands the list offset to the screens; redraws when it changed */
static void publish_scroll(screen_t s, const scroller_t *sc)
{
    int off = scroll_offset(sc);
    lock();
    int *dst = s == SCR_MENU ? &s_v.menu_scroll : &s_v.settings_scroll;
    bool changed = *dst != off;
    *dst = off;
    unlock();
    if (changed) {
        request_redraw();
    }
}

static void touch_task(void *arg)
{
    bool was = false, slider = false, moved = false, waking = false;
    bool aim = false; /* aiming in a drag game: the move is made on release */
    bool pen = false; /* drawing on a canvas (Draw & Guess) */
    hit_t pending = HIT_NONE; /* list tap waiting for the release */
    int pending_arg = -1, last_vol = -1, slider_x = 0;
    int press_x = 0, press_y = 0;
    uint32_t last_tick = 0, last_ms = now_ms();
    screen_t shown = SCR_OFF;
    int shown_dy = -1;
    scroller_t *sc = NULL;
    for (;;) {
        lock();
        screen_t scr = s_v.screen;
        int hint_dy = s_v.hint_dy;
        unlock();
        if (scr != shown) {
            /* a list starts at its top each time its screen opens */
            shown = scr;
            shown_dy = hint_dy;
            sc = list_of(scr);
            if (sc) {
                int view_h, content_h;
                ui_list_size(scr, hint_dy, &view_h, &content_h);
                scroll_reset(sc, view_h, content_h);
                publish_scroll(scr, sc);
            }
            slider = false;
            aim = false; /* the preview went with the screen change */
            pen = false;
            pending = HIT_NONE;
        } else if (sc && scr == SCR_MENU && hint_dy != shown_dy) {
            /* the hint banner slides away: the game list grows into its space */
            shown_dy = hint_dy;
            int view_h, content_h;
            ui_list_size(scr, hint_dy, &view_h, &content_h);
            scroll_resize(sc, view_h, content_h);
        }

        int x = 0, y = 0;
        bool down = touch_read(&x, &y);
        uint32_t now = now_ms();
        if (down && !was && app_input("touch")) {
            waking = true;
        }
        if (waking) {
            /* the touch that woke the display does nothing else, up to its release */
            waking = down;
            was = down;
            last_ms = now;
            vTaskDelay(pdMS_TO_TICKS(TOUCH_MS));
            continue;
        }
        if (down && !was) {
            bool gliding = sc && scroll_moving(sc); /* a touch on a moving list only stops it */
            moved = false;
            press_x = x;
            press_y = y;
            hit_t h = on_press(x, y, &pending_arg);
            slider = h == HIT_SLIDER;
            aim = h == HIT_AIM;
            pen = h == HIT_PEN;
            pending = (h == HIT_TILE || h == HIT_WIFI_SETUP || h == HIT_UPDATE_CHECK) && !gliding ? h : HIT_NONE;
            if (slider) {
                slider_x = x;
                lock();
                int vol = s_v.vol;
                unlock();
                if (pending_arg != vol) {
                    app_set_volume(pending_arg, false, false);
                    play_tick(&last_tick);
                }
                last_vol = pending_arg;
            } else if (sc) {
                scroll_press(sc, y, now);
            }
        } else if (down && slider) {
            if (abs(x - slider_x) >= SLIDER_DEAD_PX) {
                slider_x = x;
                int v = ui_slider_value(x);
                if (v != last_vol) {
                    last_vol = v;
                    app_set_volume(v, false, false);
                    play_tick(&last_tick);
                }
            }
        } else if (down && pen) {
            app_pen(GAME_PEN_MOVE, x, y); /* the stroke follows the finger */
        } else if (down && aim) {
            lock();
            int idx = s_v.game_idx;
            unlock();
            set_preview(GAMES[idx]->hit(x, y)); /* follows the finger, -1 off the board */
        } else if (down && sc) {
            if (abs(x - press_x) > TAP_SLOP_PX || abs(y - press_y) > TAP_SLOP_PX) {
                moved = true;
            }
            scroll_drag(sc, y, now);
            publish_scroll(scr, sc);
        } else if (!down && was) {
            if (pen) {
                pen = false;
                app_pen(GAME_PEN_UP, x, y);
            } else if (aim) {
                /* the move under the preview, where the finger left the board */
                aim = false;
                lock();
                int idx = s_v.game_idx, m = s_v.screen == SCR_GAME ? s_v.preview : -1;
                s_v.preview = -1;
                unlock();
                if (m >= 0) {
                    app_move(idx, m, "touch"); /* a full column is rejected as before */
                } else {
                    out("AIM cancelled");
                }
                request_redraw(); /* the preview is gone, also after a rejected drop */
            } else if (slider) {
                slider = false;
                app_set_volume(last_vol, true, false); /* save once, on release */
            } else {
                if (sc) {
                    scroll_release(sc, now);
                }
                if (pending == HIT_TILE && !moved) {
                    app_open_game(pending_arg, "touch");
                } else if (pending == HIT_WIFI_SETUP && !moved) {
                    app_wifi_setup();
                } else if (pending == HIT_UPDATE_CHECK && !moved) {
                    app_update_check("touch");
                }
            }
            pending = HIT_NONE;
        }
        if (!down && sc) {
            float dt = (now - last_ms) / 1000.0f;
            scroll_step(sc, dt > 0.1f ? 0.1f : dt);
            publish_scroll(scr, sc);
        }
        last_ms = now;
        was = down;
        vTaskDelay(pdMS_TO_TICKS(TOUCH_MS));
    }
}

/* The motion games run on the live sensor and the real clock: every MOTION_MS the tilt
 * (lightly smoothed) and shakes go to the game on screen, unless its round is driven
 * over UART (sim). A shake is a jolt of the measured acceleration far from 1 g. */
#define MOTION_MS 20
#define SHAKE_MG 1300     /* | |a| - 1 g | above this is a shake */
#define SHAKE_QUIET_MS 700 /* one shake, not one per jolt of it */
#define REDRAW_EVERY 2     /* motion ticks per frame while a round runs */
#define REDRAW_TIMED 25    /* a clock-only game (Draw & Guess): its countdown, twice a second */
#define MOVED_MG 150       /* a tilt change this big in a motion game restarts the idle timer */
#define SHAKING_MG 450     /* | |a| - 1 g | above this is a jolt of continued shaking */
#define SHAKING_HOLD_MS 300 /* the board counts as shaken this long after each jolt */

static void motion_task(void *arg)
{
    float fx = 0, fy = 0, sx = 0, ref_x = 0, ref_y = 0;
    uint32_t last = now_ms(), shake_at = 0, jolt_at = 0;
    bool jolted = false;
    for (int tick = 0;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(MOTION_MS));
        uint32_t now = now_ms(), dt = now - last;
        last = now;
        int ax = 0, ay = 0, az = 1000, tx = 0, ty = 0;
        bool live = imu_read_mg(&ax, &ay, &az);
        if (live) {
            imu_screen_tilt(ax, ay, &tx, &ty);
        }
        fx += (tx - fx) * 0.35f;
        fy += (ty - fy) * 0.35f;
        /* the steering game (Coin Platformer) follows the tilt more quickly */
        sx += (tx - sx) * 0.7f;
        float mag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);
        bool shake = false;
        if (live && fabsf(mag - 1000) > SHAKE_MG && now - shake_at > SHAKE_QUIET_MS) {
            shake = true;
            shake_at = now;
        }
        /* shaking goes on while jolts keep coming (Defi Secousse adds up its time) */
        if (live && fabsf(mag - 1000) > SHAKING_MG) {
            jolt_at = now;
            jolted = true;
        }
        bool shaking = jolted && now - jolt_at < SHAKING_HOLD_MS;

        lock();
        const game_def_t *g = GAMES[s_v.game_idx];
        bool on = s_v.screen == SCR_GAME && g->step && !s_sim[s_v.game_idx];
        game_event_t ev = GEV_NONE;
        bool running = false, timed = g->timed;
        if (on) {
            motion_t m = {.tx = (int)(g->steer ? sx : fx), .ty = (int)fy, .shake = shake, .shaking = shaking,
                          .dt_ms = dt > 200 ? 200 : dt};
            if (shake) {
                out("SHAKE mg=%d", (int)mag);
            }
            ev = g->step(slot_locked()->st.bytes, &m);
            moved_locked(ev);
            running = round_locked()->status == GAME_PLAYING;
        }
        unlock();
        if (on && !timed && (shake || shaking || fabsf(fx - ref_x) + fabsf(fy - ref_y) > MOVED_MG)) {
            /* a motion game is played by tilting: that is input for the idle timer */
            ref_x = fx;
            ref_y = fy;
            keep_awake();
        }
        if (on) {
            play_event(ev);
            if (running && tick % (timed ? REDRAW_TIMED : REDRAW_EVERY) == 0) {
                request_redraw();
            }
        }
    }
}

/* Middle key held 3 s: release the battery latch. On battery the board loses power once
 * the key is let go (the key itself powers it while held); on USB it stays powered, so
 * it sleeps instead and the next press of the key wakes it. Does not return. */
static void power_off(void)
{
    lock();
    s_power = PWR_OFF;
    s_v.off_idle = false;
    show_locked(SCR_OFF);
    unlock();
    gpio_set_level(LCD_BL, 1);
    out("POWER off: release the power key");
    vTaskDelay(pdMS_TO_TICKS(300)); /* let the screen draw */
    while (gpio_get_level(KEY_PWR) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_BL, 0);
    gpio_set_level(BAT_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(800));

    /* still running: powered over USB */
    out("POWER usb: sleeping until the power key is pressed");
    sleep_until_power_key();
}

/* RIGHT in a steering game on screen (Coin Platformer, held upright and steered by tilt):
 * a jump while the round runs (the first one starts the run), the next round once it is
 * over, so no finger is needed on the screen. In Defi Secousse it hands the board on. */
static void key_jump(void)
{
    lock();
    const game_def_t *g = GAMES[s_v.game_idx];
    int idx = s_v.game_idx;
    bool steer = s_v.screen == SCR_GAME && (g->steer || g->right_taps);
    bool over = steer && round_locked()->status != GAME_PLAYING;
    unlock();
    if (over) {
        app_new_game("button");
    } else if (steer) {
        /* a jump, or a tap of the round (Defi Secousse: the next player) */
        app_move(idx, 0, "button");
        request_redraw();
    }
}

static void button_task(void *arg)
{
    static const int PINS[3] = {KEY_LEFT, KEY_RIGHT, KEY_PWR};
    static const char *const NAMES[3] = {"left", "right", "pwr"};
    bool down[3] = {false, false, false};
    TickType_t down_at[3] = {0, 0, 0};
    bool used[3] = {false, false, false}; /* part of a combo, or held at boot: no action */
    TickType_t combo_at = 0;
    bool combo_fired = false;

    for (int i = 0; i < 3; i++) {
        gpio_reset_pin(PINS[i]);
        gpio_set_direction(PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(PINS[i], GPIO_PULLUP_ONLY);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    for (int i = 0; i < 3; i++) {
        down[i] = gpio_get_level(PINS[i]) == 0;
        /* held at boot (the power-on press, too): ignore until released */
        used[i] = true;
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        TickType_t now = xTaskGetTickCount();
        if (down[2] && !used[2] && now - down_at[2] >= pdMS_TO_TICKS(POWER_HOLD_MS)) {
            used[2] = true;
            power_off();
        }
        for (int i = 0; i < 3; i++) {
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
                /* a press while the board is switching off does nothing else */
                used[i] = app_input("button");
                if (!used[i] && i == 1 && !down[0]) {
                    key_jump();
                }
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
    /* undo what power_off() set up when the board slept on USB */
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(BAT_EN);
    gpio_hold_dis(LCD_BL);
    rtc_gpio_deinit(KEY_PWR);

    s_lock = xSemaphoreCreateMutex();
    /* no other task runs yet: every game gets its first round, X starting */
    for (int i = 0; i < GAME_COUNT; i++) {
        s_v.game_idx = i;
        slot_locked()->next_starter = CELL_X;
        new_round_locked();
    }
    s_v.game_idx = 0;
    s_v.screen = SCR_MENU;
    s_v.preview = -1;
    strlcpy(s_v.version, esp_app_get_description()->version, sizeof(s_v.version));
    /* keep the console for the protocol (and Wi-Fi logs would print the network name) */
    esp_log_level_set("*", ESP_LOG_WARN);

    /* give the host time to reopen the USB serial port after a reset */
    vTaskDelay(pdMS_TO_TICKS(1500));
    out("POCKETGAME BOOT board=ESP32-S3-LCD-1.54 fw=%s build=%s", FW_VERSION, s_v.version);

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
    if (err == ESP_OK && imu_init()) {
        out("IMU OK chip=qmi8658 addr=0x%02x rev=0x%02x sda=%d scl=%d", IMU_ADDR, imu_rev(), I2C_SDA, I2C_SCL);
    } else {
        out("IMU NONE: no QMI8658 at 0x%02x (motion games run without the sensor)", IMU_ADDR);
    }

    out("KEYS left=%d right=%d pwr=%d (pwr: hold 3 s to power off; idle off after %d s on battery)", KEY_LEFT, KEY_RIGHT,
        KEY_PWR, IDLE_OFF_MS / 1000);

    settings_init();
    s_v.vol = settings_load_volume();
    sound_set_level(s_v.vol);
    out("AUDIO %s vol=%d", err == ESP_OK ? sound_init() : "none (no i2c)", s_v.vol);

    if (!battery_init()) {
        out("BAT adc unavailable");
    }
    s_v.bat_mv = battery_mv();
    s_v.bat_pct = battery_pct(s_v.bat_mv);
    out("BAT mv=%d pct=%d adc=io%d", s_v.bat_mv, s_v.bat_pct, BAT_ADC);

    /* the hardware came up: keep this build (cancels the rollback of a fresh OTA image) */
    ota_boot();

    if (!net_init()) {
        out("WIFI init failed");
    }

    xTaskCreate(display_task, "display", 4096, NULL, 4, &s_display_task);
    xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL);
    if (touch) {
        xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);
    }
    xTaskCreate(heartbeat_task, "heartbeat", 3072, NULL, 3, NULL);
    xTaskCreate(motion_task, "motion", 4096, NULL, 5, NULL);
    s_input_ms = now_ms(); /* the idle timer starts with the menu */
    xTaskCreate(timer_task, "timer", 4096, NULL, 4, NULL);

    lock();
    show_locked(SCR_MENU);
    unlock();

    lock();
    print_ready_locked();
    unlock();
    xTaskCreate(console_task, "console", 6144, NULL, 6, NULL);
}
