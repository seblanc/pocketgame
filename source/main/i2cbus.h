#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* The board's shared I2C bus (touch, codec, IMU). Transfers are serialized by the
 * driver, so tasks may use it concurrently. */
esp_err_t i2cbus_init(void);
bool i2cbus_probe(uint8_t addr);
/* Prints "DIAG i2c bus ... devices: ..." with every address that answers. */
void i2cbus_scan(void);
bool i2cbus_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
bool i2cbus_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
/* plain read without a register address */
bool i2cbus_read(uint8_t addr, uint8_t *buf, size_t n);
