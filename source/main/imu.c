#include "imu.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2cbus.h"

/* QMI8658 registers */
#define REG_WHO_AM_I 0x00 /* reads 0x05 */
#define REG_REVISION 0x01
#define REG_CTRL1 0x02
#define REG_CTRL2 0x03 /* accelerometer full scale and output rate */
#define REG_CTRL7 0x08 /* sensor enables */
#define REG_AX_L 0x35  /* AX_L, AX_H, AY_L, AY_H, AZ_L, AZ_H */
#define REG_RESET 0x60

#define CHIP_ID 0x05
#define CTRL1_ADDR_AI 0x40  /* register address auto-increment, little endian */
#define CTRL2_8G_125HZ 0x26 /* aFS = +-8 g, aODR = 125 Hz */
#define CTRL7_ACC_EN 0x01
#define LSB_PER_G 4096      /* at +-8 g */

static bool s_ok;
static uint8_t s_rev;

bool imu_init(void)
{
    uint8_t id = 0;
    if (!i2cbus_read_reg(IMU_ADDR, REG_WHO_AM_I, &id, 1) || id != CHIP_ID) {
        return false;
    }
    i2cbus_write_reg(IMU_ADDR, REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2cbus_read_reg(IMU_ADDR, REG_REVISION, &s_rev, 1);
    s_ok = i2cbus_write_reg(IMU_ADDR, REG_CTRL1, CTRL1_ADDR_AI) &&
           i2cbus_write_reg(IMU_ADDR, REG_CTRL2, CTRL2_8G_125HZ) &&
           i2cbus_write_reg(IMU_ADDR, REG_CTRL7, CTRL7_ACC_EN);
    vTaskDelay(pdMS_TO_TICKS(30)); /* first samples */
    return s_ok;
}

bool imu_ok(void) { return s_ok; }

uint8_t imu_rev(void) { return s_rev; }

bool imu_read_raw_mg(int *ax, int *ay, int *az)
{
    uint8_t b[6];
    if (!s_ok || !i2cbus_read_reg(IMU_ADDR, REG_AX_L, b, sizeof(b))) {
        return false;
    }
    int v[3];
    for (int i = 0; i < 3; i++) {
        v[i] = (int16_t)(b[2 * i] | b[2 * i + 1] << 8) * 1000 / LSB_PER_G;
    }
    *ax = v[0];
    *ay = v[1];
    *az = v[2];
    return true;
}

bool imu_read_mg(int *ax, int *ay, int *az)
{
    if (!imu_read_raw_mg(ax, ay, az)) {
        return false;
    }
#if IMU_INVERT_X
    *ax = -*ax;
#endif
    return true;
}

void imu_screen_tilt(int ax, int ay, int *tx, int *ty)
{
    *tx = IMU_TILT_X_SIGN * ax;
    *ty = IMU_TILT_Y_SIGN * ay;
#if IMU_SWAP_XY
    int t = *tx;
    *tx = IMU_TILT_X_SIGN * ay;
    *ty = IMU_TILT_Y_SIGN * t;
#endif
}
