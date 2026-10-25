#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Small bit-banged I2C master: lets the firmware probe any pin pair while it
 * looks for the touch controller, and then talk to it on whatever pins it found. */

typedef struct {
    int sda;
    int scl;
    int half_us; /* half clock period */
} bbi2c_t;

void bbi2c_setup_pin(int pin);
void bbi2c_init(bbi2c_t *b, int sda, int scl, int half_us);
void bbi2c_recover(const bbi2c_t *b);
bool bbi2c_probe(const bbi2c_t *b, uint8_t addr);
bool bbi2c_read_reg8(const bbi2c_t *b, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
bool bbi2c_read_reg16(const bbi2c_t *b, uint8_t addr, uint16_t reg, uint8_t *buf, size_t n);
bool bbi2c_write_reg8(const bbi2c_t *b, uint8_t addr, uint8_t reg, uint8_t val);
bool bbi2c_write_reg16(const bbi2c_t *b, uint8_t addr, uint16_t reg, uint8_t val);
