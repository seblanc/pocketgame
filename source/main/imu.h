#pragma once
#include <stdbool.h>
#include <stdint.h>

/* QMI8658 6-axis IMU on the shared I2C bus (accelerometer only, +-8 g). */
#define IMU_ADDR 0x6b

/* Checks WHO_AM_I and starts the accelerometer; needs the I2C bus. */
bool imu_init(void);
bool imu_ok(void);
/* chip revision byte, valid after imu_init() */
uint8_t imu_rev(void);
/* raw accelerometer in mg, sensor axes */
bool imu_read_mg(int *ax, int *ay, int *az);
/* the same reading as the tilt of the screen, in mg: tx > 0 when the right edge is
 * lower, ty > 0 when the bottom edge is lower (where a ball on the screen rolls) */
void imu_screen_tilt(int ax, int ay, int *tx, int *ty);
