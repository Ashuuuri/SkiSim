// MPU6050 driver shared by the board unit and the server's wired fallback.
// Pins and bus speed come from the caller, so each side keeps them in its
// own config header.
#ifndef SKISIM_MPU6050_H
#define SKISIM_MPU6050_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

#define MPU6050_ADDR            0x68
#define MPU6050_ACCEL_LSB_PER_G 16384   // +-2 g range
#define MPU6050_GYRO_LSB_PER_DPS 131    // +-250 deg/s range

typedef struct {
    i2c_inst_t *i2c;
    uint        sda;
    uint        scl;
    uint        baud;
} mpu6050_t;

typedef struct {
    int16_t ax, ay, az;     // accelerometer, raw counts
    int16_t gx, gy, gz;     // gyroscope, raw counts
} mpu6050_raw_t;

// Recover the bus, reset the sensor, check WHO_AM_I, wake it, and set
// +-2 g, +-250 deg/s, 1 kHz sampling with the ~44 Hz low-pass filter.
// Safe to call again at any time to recover from a dropout.
// Returns true when the sensor answered and accepted the configuration.
bool mpu6050_init(const mpu6050_t *m);

// Read one accelerometer + gyroscope sample (a single 14-byte burst).
// Returns false on an I2C error, or when every value is zero, which means
// the sensor lost power and restarted in sleep mode.
bool mpu6050_read(const mpu6050_t *m, mpu6050_raw_t *out);

#endif // SKISIM_MPU6050_H
