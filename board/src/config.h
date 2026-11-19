// Board unit settings: pins, rates, filter, and sensor axis mapping.
// Everything tunable lives here, not inside functions.
#ifndef SKISIM_BOARD_CONFIG_H
#define SKISIM_BOARD_CONFIG_H

#include <math.h>
#include "hardware/i2c.h"

// ---------------------------------------------------------------------------
// I2C to the MPU6050
// ---------------------------------------------------------------------------
#define SKI_I2C_PORT        i2c0
#define SKI_I2C_SDA_PIN     8          // physical pin 11
#define SKI_I2C_SCL_PIN     9          // physical pin 12
// 400 kHz keeps a 14-byte read near 0.4 ms, well inside the 2 ms sample
// period. If long wires under the board cause errors, drop to 100000 and
// SKI_SAMPLE_HZ to 250 (a read then takes about 1.6 ms).
#define SKI_I2C_BAUD        400000

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define SKI_SAMPLE_HZ       500        // IMU reads + filter updates per second
#define SKI_SEND_PERIOD_US  10000      // one UDP packet every 10 ms

// ---------------------------------------------------------------------------
// Complementary filter
// ---------------------------------------------------------------------------
#define SKI_FILTER_ALPHA      0.98f    // weight on the gyro path
#define SKI_GYRO_CAL_SECONDS  2.0f     // hold still this long after boot

// ---------------------------------------------------------------------------
// Sensor axis mapping
// From the tilt test on 2026-11-19 (sensor flat, chip side up): rocking the
// board left/right rotates about the sensor's Y axis.
//   tilt angle = atan2(ax, az), right side down = positive
//   tilt rate  = -gy (the gyro's sign is opposite to that angle)
// Re-run the test once the sensor is mounted under the board; if it hangs
// upside down, az flips sign and this mapping must change.
// ---------------------------------------------------------------------------
#define SKI_ACCEL_ANGLE_DEG(ax, ay, az) \
    (atan2f((float)(ax), (float)(az)) * 57.29578f)
#define SKI_GYRO_RATE_RAW(gx, gy, gz)   (-(int32_t)(gy))

// After an IMU read fails this many times in a row, re-initialise the sensor.
#define SKI_IMU_MAX_ERRORS  5

// Print a "tx unit=.. seq=.. angle=.." line for every Nth packet sent, for
// tools/imu_viewer and for comparing with the server's "rx" lines.
// 2 = 50 lines/s. 0 = off.
#define SKI_LOG_ANGLE_EVERY 2

#endif // SKISIM_BOARD_CONFIG_H
