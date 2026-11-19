// SkiSim client (board unit), Phase 1 steps 4-5.
//
// core 1: read the MPU6050 at SKI_SAMPLE_HZ, run the complementary filter,
//         publish the latest angle. Re-initialises the sensor after dropouts.
// core 0: WiFi (join, rejoin), send the latest angle every 10 ms, LED, log.
//
// LED: fast blink = looking for the server, slow blink = connected.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/cyw43_arch.h"

#include "config.h"
#include "fix15.h"
#include "mpu6050.h"
#include "net_client.h"
#include "protocol.h"
#include "tilt_filter.h"

#define BLINK_FAST_MS  100
#define BLINK_SLOW_MS  500

// ---------------------------------------------------------------------------
// State shared between the cores (guarded by imu_lock)
// ---------------------------------------------------------------------------
typedef struct {
    fix15    angle;       // deg, filtered
    fix15    rate;        // deg/s, bias-corrected
    uint16_t flags;       // SKI_FLAG_*
    uint32_t samples;     // successful reads since boot
    uint32_t errors;      // failed reads since boot
    uint32_t reinits;     // sensor re-initialisations since boot
} imu_state_t;

static imu_state_t imu_state = { .flags = SKI_FLAG_IMU_ERR };
static critical_section_t imu_lock;

static void imu_snapshot(imu_state_t *out) {
    critical_section_enter_blocking(&imu_lock);
    *out = imu_state;
    critical_section_exit(&imu_lock);
}

// ---------------------------------------------------------------------------
// core 1: sensor + filter
// ---------------------------------------------------------------------------
static void set_imu_error(bool err) {
    critical_section_enter_blocking(&imu_lock);
    if (err) imu_state.flags |= SKI_FLAG_IMU_ERR;
    else     imu_state.flags &= ~SKI_FLAG_IMU_ERR;
    critical_section_exit(&imu_lock);
}

static void imu_bring_up(const mpu6050_t *m) {
    set_imu_error(true);
    while (!mpu6050_init(m)) {
        critical_section_enter_blocking(&imu_lock);
        imu_state.reinits++;
        critical_section_exit(&imu_lock);
        sleep_ms(200);
    }
    set_imu_error(false);
}

static void core1_main(void) {
    const mpu6050_t imu = {
        .i2c = SKI_I2C_PORT, .sda = SKI_I2C_SDA_PIN, .scl = SKI_I2C_SCL_PIN,
        .baud = SKI_I2C_BAUD,
    };
    tilt_filter_t filter;
    tilt_filter_init(&filter, SKI_FILTER_ALPHA, SKI_SAMPLE_HZ, SKI_GYRO_CAL_SECONDS);

    imu_bring_up(&imu);

    const uint32_t period_us = 1000000 / SKI_SAMPLE_HZ;
    absolute_time_t next = get_absolute_time();
    int fails_in_row = 0;

    while (true) {
        next = delayed_by_us(next, period_us);

        mpu6050_raw_t r;
        if (mpu6050_read(&imu, &r)) {
            fails_in_row = 0;
            fix15 accel_angle = float2fix15(SKI_ACCEL_ANGLE_DEG(r.ax, r.ay, r.az));
            fix15 gyro_rate = (fix15)(((int64_t)SKI_GYRO_RATE_RAW(r.gx, r.gy, r.gz) << 15)
                                      / MPU6050_GYRO_LSB_PER_DPS);
            tilt_filter_update(&filter, accel_angle, gyro_rate);

            critical_section_enter_blocking(&imu_lock);
            imu_state.angle = filter.angle;
            imu_state.rate  = filter.rate;
            if (filter.calibrated) imu_state.flags |= SKI_FLAG_GYRO_CAL;
            imu_state.samples++;
            critical_section_exit(&imu_lock);
        } else {
            critical_section_enter_blocking(&imu_lock);
            imu_state.errors++;
            critical_section_exit(&imu_lock);

            if (++fails_in_row >= SKI_IMU_MAX_ERRORS) {
                imu_bring_up(&imu);              // bus recovery + reset + wake
                tilt_filter_reseed(&filter);     // keep the bias, restart the angle
                fails_in_row = 0;
                next = get_absolute_time();
            }
        }

        // Do not try to catch up after a long stall (e.g. a re-init).
        if (absolute_time_diff_us(next, get_absolute_time()) > 10000) {
            next = get_absolute_time();
        }
        sleep_until(next);
    }
}

// ---------------------------------------------------------------------------
// core 0: network, LED, log
// ---------------------------------------------------------------------------
int main(void) {
    stdio_init_all();
    sleep_ms(1500);   // give USB serial time to enumerate so early prints are not lost
    printf("\n[client] SkiSim client starting\n");
    printf("[client] IMU on I2C SDA=GP%d SCL=GP%d at %d kHz, %d Hz sampling; "
           "hold still %.0f s for gyro calibration\n", SKI_I2C_SDA_PIN,
           SKI_I2C_SCL_PIN, SKI_I2C_BAUD / 1000, SKI_SAMPLE_HZ,
           (double)SKI_GYRO_CAL_SECONDS);

    critical_section_init(&imu_lock);
    multicore_launch_core1(core1_main);

    if (net_client_init() != 0) {
        while (true) {
            printf("[client] wireless init failed, check PICO_BOARD=pico_w\n");
            sleep_ms(1000);
        }
    }

    bool led = false;
    absolute_time_t next_blink = get_absolute_time();
    absolute_time_t next_report = make_timeout_time_ms(1000);
    absolute_time_t next_send = get_absolute_time();
    uint32_t seconds = 0, seq = 0;
    uint32_t sent = 0, send_errors = 0;   // counts for the current second
    imu_state_t prev = {0};

    while (true) {
        net_client_poll();

        if (absolute_time_diff_us(get_absolute_time(), next_send) <= 0) {
            next_send = delayed_by_us(next_send, SKI_SEND_PERIOD_US);
            if (absolute_time_diff_us(get_absolute_time(), next_send) < 0) {
                next_send = make_timeout_time_us(SKI_SEND_PERIOD_US);
            }
            if (net_client_is_connected()) {
                imu_state_t s;
                imu_snapshot(&s);
                ski_packet_t pkt = {
                    .magic   = SKI_PKT_MAGIC,
                    .version = SKI_PKT_VERSION,
                    .unit_id = SKI_UNIT_BOARD,
                    .seq     = ++seq,
                    .t_us    = time_us_32(),
                    .angle   = s.angle,
                    .rate    = s.rate,
                    .flags   = s.flags,
                };
                if (net_client_send(&pkt, sizeof pkt)) sent++;
                else send_errors++;
            }
        }

        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            next_blink = make_timeout_time_ms(
                net_client_is_connected() ? BLINK_SLOW_MS : BLINK_FAST_MS);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
            next_report = make_timeout_time_ms(1000);
            seconds++;
            imu_state_t s;
            imu_snapshot(&s);
            printf("[client] t=%lus  wifi=%s  sent=%lu/s  "
                   "angle=%+6.2f deg  rate=%+7.1f dps  imu=%lu/s  %s%s  "
                   "imu_err=%lu  reinit=%lu\n",
                   (unsigned long)seconds,
                   net_client_is_connected() ? "connected" : "searching",
                   (unsigned long)sent,
                   (double)fix2float15(s.angle), (double)fix2float15(s.rate),
                   (unsigned long)(s.samples - prev.samples),
                   (s.flags & SKI_FLAG_GYRO_CAL) ? "cal" : "CALIBRATING",
                   (s.flags & SKI_FLAG_IMU_ERR) ? " IMU-ERROR" : "",
                   (unsigned long)s.errors, (unsigned long)s.reinits);
            prev = s;
            sent = 0;
            send_errors = 0;
        }
    }
}
