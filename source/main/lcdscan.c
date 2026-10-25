#include "lcdscan.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RDDID 0x04
#define MAXC 40
#define PARTITIONS 24 /* random control-line settings; each has CS/DC low and RST high with p >= 1/8 */

static bool id_ok(uint32_t id) { return id != 0 && id != 0xFFFFFF; }

static void out_pin(int pin, int level)
{
    gpio_set_level(pin, level);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    /* weakest drive: harmless if the line turns out to be another chip's output */
    gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_0);
}

static void prep_pin(int pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

/* One RDDID transfer. pulse[] pins go high for a moment first: a CS rising edge
 * resets the panel's serial interface. dc_after >= 0 is raised once the command
 * byte is out (DC only matters for the command, CS for the whole transfer). */
static uint32_t bb_rddid(int sclk, int sda, const int *pulse, int npulse, int dc_after)
{
    for (int i = 0; i < npulse; i++) {
        gpio_set_level(pulse[i], 1);
    }
    esp_rom_delay_us(1);
    for (int i = 0; i < npulse; i++) {
        gpio_set_level(pulse[i], 0);
    }
    gpio_set_level(sclk, 0);
    gpio_set_level(sda, 0);
    gpio_set_direction(sda, GPIO_MODE_OUTPUT);
    for (int b = 7; b >= 0; b--) {
        gpio_set_level(sda, (RDDID >> b) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(sclk, 1);
        esp_rom_delay_us(1);
        gpio_set_level(sclk, 0);
    }
    if (dc_after >= 0) {
        gpio_set_level(dc_after, 1);
    }
    gpio_set_direction(sda, GPIO_MODE_INPUT); /* keeps the pull-up: silence reads as ones */
    esp_rom_delay_us(1);
    uint32_t raw = 0;
    for (int b = 0; b < 25; b++) { /* one dummy clock, then the 24 ID bits */
        gpio_set_level(sclk, 1);
        esp_rom_delay_us(1);
        raw = (raw << 1) | (uint32_t)gpio_get_level(sda);
        gpio_set_level(sclk, 0);
        esp_rom_delay_us(1);
    }
    if (dc_after >= 0) {
        gpio_set_level(dc_after, 0);
    }
    return raw & 0xFFFFFF;
}

uint32_t lcdscan_read_id(const board_profile_t *p)
{
    if (p->lcd_sclk < 0 || p->lcd_mosi < 0) {
        return 0;
    }
    const int pins[] = {p->lcd_sclk, p->lcd_mosi, p->lcd_cs, p->lcd_dc, p->lcd_rst};
    for (int i = 0; i < 5; i++) {
        if (pins[i] >= 0) {
            prep_pin(pins[i]);
        }
    }
    for (int i = 0; i < p->n_hold_lo; i++) {
        prep_pin(p->hold_lo[i]);
        out_pin(p->hold_lo[i], 0);
    }
    for (int i = 0; i < p->n_hold_hi; i++) {
        prep_pin(p->hold_hi[i]);
        out_pin(p->hold_hi[i], 1);
    }
    out_pin(p->lcd_sclk, 0);
    if (p->lcd_cs >= 0) {
        out_pin(p->lcd_cs, 1);
    }
    if (p->lcd_dc >= 0) {
        out_pin(p->lcd_dc, 0);
    }
    if (p->lcd_rst >= 0) {
        out_pin(p->lcd_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(p->lcd_rst, 1);
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    int cs = p->lcd_cs;
    uint32_t id = bb_rddid(p->lcd_sclk, p->lcd_mosi, &cs, cs >= 0, -1);
    if (cs >= 0 && bb_rddid(p->lcd_sclk, p->lcd_mosi, &cs, 1, -1) != id) {
        id = 0; /* unstable: not a real answer */
    }

    for (int i = 0; i < 5; i++) {
        if (pins[i] >= 0) {
            gpio_reset_pin(pins[i]);
        }
    }
    for (int i = 0; i < p->n_hold_lo; i++) {
        gpio_reset_pin(p->hold_lo[i]);
    }
    for (int i = 0; i < p->n_hold_hi; i++) {
        gpio_reset_pin(p->hold_hi[i]);
    }
    return id;
}

/* ---- search ---- */

typedef struct {
    int pin;
    bool drive; /* may be driven as a control line */
    int level;  /* its level in the current setting */
} cpin_t;

static cpin_t s_c[MAXC];
static int s_n;
static uint32_t s_rng = 0x2545F491;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void to_idle(int i)
{
    if (s_c[i].drive) {
        out_pin(s_c[i].pin, s_c[i].level);
    } else {
        gpio_set_direction(s_c[i].pin, GPIO_MODE_INPUT);
    }
}

/* RDDID with candidate ic as SCLK and id as SDA, the others at their levels */
static uint32_t try_pair(int ic, int id, int dc_after)
{
    int pulse[MAXC];
    int np = 0;
    for (int i = 0; i < s_n; i++) {
        if (i != ic && i != id && s_c[i].drive && !s_c[i].level) {
            pulse[np++] = s_c[i].pin;
        }
    }
    out_pin(s_c[ic].pin, 0);
    uint32_t v = bb_rddid(s_c[ic].pin, s_c[id].pin, pulse, np, dc_after);
    to_idle(ic);
    to_idle(id);
    return v;
}

static int add_hold(int *arr, int n, int pin)
{
    if (n < MAX_HOLD) {
        arr[n++] = pin;
    }
    return n;
}

static void release_all(void)
{
    for (int i = 0; i < s_n; i++) {
        gpio_reset_pin(s_c[i].pin);
    }
}

uint32_t lcdscan_search(const int *cand, const bool *idle_high, int n, board_profile_t *p)
{
    s_n = n > MAXC ? MAXC : n;
    for (int i = 0; i < s_n; i++) {
        s_c[i].pin = cand[i];
        s_c[i].drive = idle_high[i];
        s_c[i].level = 1;
        prep_pin(cand[i]);
    }

    TickType_t t0 = xTaskGetTickCount();
    int fc = -1, fd = -1, fpart = -1;
    uint32_t id = 0;
    for (int part = 0; part < PARTITIONS && fc < 0; part++) {
        for (int i = 0; i < s_n; i++) {
            s_c[i].level = rnd() & 1;
            to_idle(i);
        }
        vTaskDelay(pdMS_TO_TICKS(20)); /* a reset line that just went high needs 5 ms */
        /* both orders: a pair that toggled the reset line spoils the tests right after it */
        for (int pass = 0; pass < 2 && fc < 0; pass++) {
            for (int a = 0; a < s_n && fc < 0; a++) {
                int ic = pass ? s_n - 1 - a : a;
                for (int b = 0; b < s_n; b++) {
                    int idd = pass ? s_n - 1 - b : b;
                    if (idd == ic) {
                        continue;
                    }
                    uint32_t v = try_pair(ic, idd, -1);
                    if (id_ok(v) && try_pair(ic, idd, -1) == v) {
                        fc = ic;
                        fd = idd;
                        fpart = part;
                        id = v;
                        break;
                    }
                }
                vTaskDelay(1); /* let the idle task feed the watchdog */
            }
        }
    }
    unsigned long ms = (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);
    if (fc < 0) {
        printf("DIAG lcdscan none: no panel answered on %d pins in %lu ms\n", s_n, ms);
        release_all();
        return 0;
    }
    printf("DIAG lcdscan found sclk=%d sda=%d id=0x%06lx setting=%d in %lu ms\n", s_c[fc].pin,
           s_c[fd].pin, (unsigned long)id, fpart, ms);

    /* which control lines matter: flip each one alone and see if the answer stops */
    int need_lo[MAXC], nlo = 0, need_hi[MAXC], nhi = 0;
    for (int i = 0; i < s_n; i++) {
        if (i == fc || i == fd || !s_c[i].drive) {
            continue;
        }
        int keep = s_c[i].level;
        bool needed = true;
        for (int attempt = 0; attempt < 2 && needed; attempt++) { /* one flaky read is not proof */
            s_c[i].level = !keep;
            to_idle(i);
            esp_rom_delay_us(5);
            needed = try_pair(fc, fd, -1) != id;
            s_c[i].level = keep;
            to_idle(i);
            vTaskDelay(pdMS_TO_TICKS(10)); /* recovery if that was the reset line */
        }
        if (needed) {
            if (keep) {
                need_hi[nhi++] = s_c[i].pin;
            } else {
                need_lo[nlo++] = s_c[i].pin;
            }
        }
    }

    p->lcd_sclk = s_c[fc].pin;
    p->lcd_mosi = s_c[fd].pin;
    p->lcd_cs = p->lcd_dc = p->lcd_rst = NO_PIN;
    p->n_hold_lo = p->n_hold_hi = 0;

    /* low lines: DC only matters during the command byte, CS during the whole read */
    int cs_like[MAXC], ncs = 0;
    for (int k = 0; k < nlo; k++) {
        int pin = need_lo[k];
        uint32_t v = try_pair(fc, fd, pin);
        if (v == id && p->lcd_dc < 0) {
            p->lcd_dc = pin;
        } else if (v == id) {
            p->n_hold_lo = add_hold(p->hold_lo, p->n_hold_lo, pin);
        } else {
            cs_like[ncs++] = pin;
        }
    }
    if (ncs == 1) {
        p->lcd_cs = cs_like[0];
    } else {
        for (int k = 0; k < ncs; k++) {
            p->n_hold_lo = add_hold(p->hold_lo, p->n_hold_lo, cs_like[k]);
        }
    }
    if (nhi == 1) {
        p->lcd_rst = need_hi[0];
    } else {
        for (int k = 0; k < nhi; k++) {
            p->n_hold_hi = add_hold(p->hold_hi, p->n_hold_hi, need_hi[k]);
        }
    }
    printf("DIAG lcdscan roles cs=%d dc=%d rst=%d holds_low=%d holds_high=%d\n", p->lcd_cs, p->lcd_dc,
           p->lcd_rst, p->n_hold_lo, p->n_hold_hi);
    release_all();

    if (p->lcd_dc < 0) {
        printf("DIAG lcdscan no DC line found\n");
        return 0;
    }
    /* final check with only the found lines driven */
    uint32_t v = lcdscan_read_id(p);
    printf("DIAG lcdscan verify id=0x%06lx %s\n", (unsigned long)v, v == id ? "ok" : "mismatch");
    return v == id ? id : 0;
}
