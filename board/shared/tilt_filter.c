#include "tilt_filter.h"

void tilt_filter_init(tilt_filter_t *f, float alpha, int sample_hz,
                      float calib_seconds) {
    f->alpha         = float2fix15(alpha);
    f->sample_hz     = sample_hz;
    f->calib_samples = (int32_t)(calib_seconds * sample_hz);
    f->calibrated    = false;
    f->seeded        = false;
    f->calib_sum     = 0;
    f->calib_count   = 0;
    f->bias          = 0;
    f->angle         = 0;
    f->rate          = 0;
}

void tilt_filter_reseed(tilt_filter_t *f) {
    f->seeded = false;
}

void tilt_filter_update(tilt_filter_t *f, fix15 accel_angle, fix15 gyro_rate) {
    if (!f->calibrated) {
        f->calib_sum += gyro_rate;
        if (++f->calib_count >= f->calib_samples) {
            f->bias = (fix15)(f->calib_sum / f->calib_count);
            f->calibrated = true;
        }
        f->angle  = accel_angle;      // accelerometer only until the bias is known
        f->rate   = 0;
        f->seeded = true;
        return;
    }

    f->rate = gyro_rate - f->bias;
    if (!f->seeded) {
        f->angle  = accel_angle;
        f->seeded = true;
        return;
    }

    // rate * dt as rate / sample_hz: exact, unlike rounding dt itself to fix15
    fix15 gyro_angle = f->angle + f->rate / f->sample_hz;
    f->angle = multfix15(f->alpha, gyro_angle)
             + multfix15(int2fix15(1) - f->alpha, accel_angle);
}
