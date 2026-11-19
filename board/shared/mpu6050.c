#include "mpu6050.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

// Registers
#define REG_SMPLRT_DIV       0x19
#define REG_CONFIG           0x1A
#define REG_GYRO_CONFIG      0x1B
#define REG_ACCEL_CONFIG     0x1C
#define REG_ACCEL_XOUT_H     0x3B
#define REG_SIGNAL_PATH_RST  0x68
#define REG_PWR_MGMT_1       0x6B
#define REG_WHO_AM_I         0x75

#define IO_TIMEOUT_US        2000

static bool write_reg(const mpu6050_t *m, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write_timeout_us(m->i2c, MPU6050_ADDR, buf, 2, false,
                                IO_TIMEOUT_US) == 2;
}

static bool read_regs(const mpu6050_t *m, uint8_t reg, uint8_t *dst, size_t n) {
    if (i2c_write_timeout_us(m->i2c, MPU6050_ADDR, &reg, 1, true,
                             IO_TIMEOUT_US) != 1) {
        return false;
    }
    return i2c_read_timeout_us(m->i2c, MPU6050_ADDR, dst, n, false,
                               IO_TIMEOUT_US) == (int)n;
}

// Drive one line open-drain style: released = input with pull-up, low = output 0.
static void line_set(uint pin, bool high) {
    if (high) {
        gpio_set_dir(pin, GPIO_IN);
    } else {
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    }
    sleep_us(5);
}

// Standard I2C bus recovery. A Pico reset in the middle of a read can leave
// the sensor (which stays powered) holding SDA low; clock SCL by hand until
// it lets go, then send a STOP.
static void bus_recover(const mpu6050_t *m) {
    i2c_deinit(m->i2c);
    gpio_init(m->sda);
    gpio_init(m->scl);
    gpio_pull_up(m->sda);
    gpio_pull_up(m->scl);
    line_set(m->sda, true);
    line_set(m->scl, true);
    for (int k = 0; k < 9 && !gpio_get(m->sda); k++) {
        line_set(m->scl, false);
        line_set(m->scl, true);
    }
    line_set(m->scl, false);    // STOP: SDA low -> high while SCL is high
    line_set(m->sda, false);
    line_set(m->scl, true);
    line_set(m->sda, true);
}

bool mpu6050_init(const mpu6050_t *m) {
    bus_recover(m);

    i2c_init(m->i2c, m->baud);
    gpio_set_function(m->sda, GPIO_FUNC_I2C);
    gpio_set_function(m->scl, GPIO_FUNC_I2C);
    gpio_pull_up(m->sda);
    gpio_pull_up(m->scl);

    // Full device reset, then reset the signal paths (register map advice).
    if (!write_reg(m, REG_PWR_MGMT_1, 0x80)) return false;
    sleep_ms(100);
    write_reg(m, REG_SIGNAL_PATH_RST, 0x07);
    sleep_ms(100);

    uint8_t who = 0;
    if (!read_regs(m, REG_WHO_AM_I, &who, 1) || who != 0x68) return false;

    return write_reg(m, REG_PWR_MGMT_1, 0x00)      // wake, internal oscillator
        && write_reg(m, REG_SMPLRT_DIV, 0x00)      // 1 kHz / (1 + 0) = 1 kHz
        && write_reg(m, REG_CONFIG, 0x03)          // DLPF ~44 Hz
        && write_reg(m, REG_GYRO_CONFIG, 0x00)     // +-250 deg/s
        && write_reg(m, REG_ACCEL_CONFIG, 0x00);   // +-2 g
}

bool mpu6050_read(const mpu6050_t *m, mpu6050_raw_t *out) {
    uint8_t b[14];
    if (!read_regs(m, REG_ACCEL_XOUT_H, b, sizeof b)) return false;

    #define BE16(i) ((int16_t)((b[(i)] << 8) | b[(i) + 1]))
    out->ax = BE16(0);
    out->ay = BE16(2);
    out->az = BE16(4);
    // b[6..7] is temperature, unused
    out->gx = BE16(8);
    out->gy = BE16(10);
    out->gz = BE16(12);
    #undef BE16

    // A sensor that browned out restarts asleep and reads all zeros.
    return out->ax | out->ay | out->az | out->gx | out->gy | out->gz;
}
