// Gyro bias calibration and complementary filter for one tilt axis, in fix15.
// The caller maps raw sensor axes to "accel angle" and "gyro rate" (that
// mapping depends on how the sensor is mounted, so it lives in config.h).
//
//   angle = alpha * (angle + (rate - bias) * dt) + (1 - alpha) * accel_angle
#ifndef SKISIM_TILT_FILTER_H
#define SKISIM_TILT_FILTER_H

#include <stdbool.h>
#include <stdint.h>
#include "fix15.h"

typedef struct {
    // settings
    fix15    alpha;          // weight on the gyro path, e.g. 0.98
    int32_t  sample_hz;      // update rate; dt = 1 / sample_hz
    int32_t  calib_samples;  // samples averaged for the gyro bias

    // state
    bool     calibrated;     // gyro bias measured
    bool     seeded;         // angle initialised from the accelerometer
    int64_t  calib_sum;
    int32_t  calib_count;
    fix15    bias;           // deg/s
    fix15    angle;          // deg, filtered output
    fix15    rate;           // deg/s, bias-corrected gyro rate
} tilt_filter_t;

void tilt_filter_init(tilt_filter_t *f, float alpha, int sample_hz,
                      float calib_seconds);

// Feed one sample. accel_angle in deg, gyro_rate in deg/s (both fix15).
// While the bias is being measured (hold still), the output follows the
// accelerometer alone.
void tilt_filter_update(tilt_filter_t *f, fix15 accel_angle, fix15 gyro_rate);

// Re-seed the angle from the next accelerometer sample, keeping the bias.
// Use after a sensor dropout.
void tilt_filter_reseed(tilt_filter_t *f);

// Use a gyro bias measured earlier (e.g. before a reboot) and skip the
// hold-still calibration.
void tilt_filter_set_bias(tilt_filter_t *f, fix15 bias);

#endif // SKISIM_TILT_FILTER_H
