#include "i2cbus.h"
#include <stdio.h>
#include "board.h"
#include "driver/i2c_master.h"

#define SPEED_HZ 400000
#define TIMEOUT_MS 50
#define MAX_DEVS 8

static i2c_master_bus_handle_t s_bus;
static struct {
    uint8_t addr;
    i2c_master_dev_handle_t dev;
} s_devs[MAX_DEVS];
static int s_ndevs;

esp_err_t i2cbus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_bus);
}

static i2c_master_dev_handle_t dev_for(uint8_t addr)
{
    for (int i = 0; i < s_ndevs; i++) {
        if (s_devs[i].addr == addr) {
            return s_devs[i].dev;
        }
    }
    if (!s_bus || s_ndevs == MAX_DEVS) {
        return NULL;
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = SPEED_HZ,
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(s_bus, &dc, &dev) != ESP_OK) {
        return NULL;
    }
    s_devs[s_ndevs].addr = addr;
    s_devs[s_ndevs].dev = dev;
    s_ndevs++;
    return dev;
}

bool i2cbus_probe(uint8_t addr)
{
    return s_bus && i2c_master_probe(s_bus, addr, TIMEOUT_MS) == ESP_OK;
}

void i2cbus_scan(void)
{
    char devs[160];
    int pos = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2cbus_probe(a) && pos < (int)sizeof(devs) - 6) {
            pos += snprintf(devs + pos, sizeof(devs) - pos, " 0x%02x", a);
        }
    }
    devs[pos] = '\0';
    printf("DIAG i2c bus sda=%d scl=%d devices:%s\n", I2C_SDA, I2C_SCL, pos ? devs : " none");
}

bool i2cbus_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev = dev_for(addr);
    uint8_t b[2] = {reg, val};
    return dev && i2c_master_transmit(dev, b, 2, TIMEOUT_MS) == ESP_OK;
}

bool i2cbus_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    i2c_master_dev_handle_t dev = dev_for(addr);
    return dev && i2c_master_transmit_receive(dev, &reg, 1, buf, n, TIMEOUT_MS) == ESP_OK;
}

bool i2cbus_read(uint8_t addr, uint8_t *buf, size_t n)
{
    i2c_master_dev_handle_t dev = dev_for(addr);
    return dev && i2c_master_receive(dev, buf, n, TIMEOUT_MS) == ESP_OK;
}
