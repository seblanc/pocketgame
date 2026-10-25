#include "bbi2c.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define DLY(b) esp_rom_delay_us((b)->half_us)

void bbi2c_setup_pin(int pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    gpio_set_level(pin, 1);
}

void bbi2c_init(bbi2c_t *b, int sda, int scl, int half_us)
{
    b->sda = sda;
    b->scl = scl;
    b->half_us = half_us;
    bbi2c_setup_pin(sda);
    bbi2c_setup_pin(scl);
}

static inline void sda_set(const bbi2c_t *b, int v) { gpio_set_level(b->sda, v); }
static inline int sda_get(const bbi2c_t *b) { return gpio_get_level(b->sda); }

static void scl_high(const bbi2c_t *b)
{
    gpio_set_level(b->scl, 1);
    /* honour clock stretching, but never hang on a wrong pin */
    for (int i = 0; i < 200 && !gpio_get_level(b->scl); i++) {
        esp_rom_delay_us(5);
    }
}

static inline void scl_low(const bbi2c_t *b) { gpio_set_level(b->scl, 0); }

static void start(const bbi2c_t *b)
{
    sda_set(b, 1);
    DLY(b);
    scl_high(b);
    DLY(b);
    sda_set(b, 0);
    DLY(b);
    scl_low(b);
    DLY(b);
}

static void stop(const bbi2c_t *b)
{
    sda_set(b, 0);
    DLY(b);
    scl_high(b);
    DLY(b);
    sda_set(b, 1);
    DLY(b);
}

static bool write_byte(const bbi2c_t *b, uint8_t v)
{
    for (int i = 7; i >= 0; i--) {
        sda_set(b, (v >> i) & 1);
        DLY(b);
        scl_high(b);
        DLY(b);
        scl_low(b);
    }
    sda_set(b, 1);
    DLY(b);
    scl_high(b);
    DLY(b);
    bool ack = sda_get(b) == 0;
    scl_low(b);
    DLY(b);
    return ack;
}

static uint8_t read_byte(const bbi2c_t *b, bool ack)
{
    uint8_t v = 0;
    sda_set(b, 1);
    for (int i = 0; i < 8; i++) {
        DLY(b);
        scl_high(b);
        DLY(b);
        v = (v << 1) | (sda_get(b) ? 1 : 0);
        scl_low(b);
    }
    sda_set(b, ack ? 0 : 1);
    DLY(b);
    scl_high(b);
    DLY(b);
    scl_low(b);
    sda_set(b, 1);
    DLY(b);
    return v;
}

void bbi2c_recover(const bbi2c_t *b)
{
    sda_set(b, 1);
    for (int i = 0; i < 9; i++) {
        scl_low(b);
        DLY(b);
        scl_high(b);
        DLY(b);
    }
    start(b);
    stop(b);
}

bool bbi2c_probe(const bbi2c_t *b, uint8_t addr)
{
    start(b);
    bool ack = write_byte(b, addr << 1);
    stop(b);
    return ack;
}

static bool read_after_reg(const bbi2c_t *b, uint8_t addr, uint8_t *buf, size_t n)
{
    start(b);
    if (!write_byte(b, (addr << 1) | 1)) {
        stop(b);
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = read_byte(b, i + 1 < n);
    }
    stop(b);
    return true;
}

bool bbi2c_read_reg8(const bbi2c_t *b, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    start(b);
    if (!write_byte(b, addr << 1) || !write_byte(b, reg)) {
        stop(b);
        return false;
    }
    return read_after_reg(b, addr, buf, n);
}

bool bbi2c_read_reg16(const bbi2c_t *b, uint8_t addr, uint16_t reg, uint8_t *buf, size_t n)
{
    start(b);
    if (!write_byte(b, addr << 1) || !write_byte(b, reg >> 8) || !write_byte(b, reg & 0xFF)) {
        stop(b);
        return false;
    }
    return read_after_reg(b, addr, buf, n);
}

bool bbi2c_write_reg8(const bbi2c_t *b, uint8_t addr, uint8_t reg, uint8_t val)
{
    start(b);
    bool ok = write_byte(b, addr << 1) && write_byte(b, reg) && write_byte(b, val);
    stop(b);
    return ok;
}

bool bbi2c_write_reg16(const bbi2c_t *b, uint8_t addr, uint16_t reg, uint8_t val)
{
    start(b);
    bool ok = write_byte(b, addr << 1) && write_byte(b, reg >> 8) && write_byte(b, reg & 0xFF) &&
              write_byte(b, val);
    stop(b);
    return ok;
}
