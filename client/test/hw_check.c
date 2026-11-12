// Standalone hardware check for the client board. Needs only one Pico W.
//
// 1. Blinks the on-board LED (proves the wireless chip and PICO_BOARD=pico_w).
// 2. Scans for WiFi networks and reports whether "skisim" is visible.
// 3. Scans I2C0 on GP8/GP9 and on GP16/GP17 and reports where the MPU6050 is.
// 4. If found, wakes the MPU6050 and prints accel, gyro, and two tilt angles
//    at 10 Hz so you can see which axis changes when the board tilts.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"

#include "protocol.h"

#define I2C_PORT      i2c0
#define I2C_BAUD      100000
#define MPU_ADDR      0x68

// MPU6050 registers
#define REG_CONFIG        0x1A
#define REG_GYRO_CONFIG   0x1B
#define REG_ACCEL_CONFIG  0x1C
#define REG_ACCEL_XOUT_H  0x3B
#define REG_PWR_MGMT_1    0x6B
#define REG_WHO_AM_I      0x75

typedef struct { uint sda, scl; } pin_pair_t;
static const pin_pair_t candidates[] = { {8, 9}, {16, 17} };

static bool led = false;
static void blink(void) {
    led = !led;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
}

// ---------------------------------------------------------------- WiFi scan
static bool skisim_seen = false;

static int scan_cb(void *env, const cyw43_ev_scan_result_t *r) {
    (void)env;
    if (!r) return 0;
    char ssid[33];
    int len = r->ssid_len < 32 ? r->ssid_len : 32;
    memcpy(ssid, r->ssid, len);
    ssid[len] = '\0';
    bool ours = (strcmp(ssid, SKI_WIFI_SSID) == 0);
    if (ours) skisim_seen = true;
    printf("  %-32s rssi %4d  ch %2d%s\n", len ? ssid : "(hidden)", r->rssi,
           r->channel, ours ? "   <-- SkiSim server" : "");
    return 0;
}

static void wifi_scan(void) {
    printf("\n[wifi] scanning (about 5 s)...\n");
    cyw43_wifi_scan_options_t opts = {0};
    if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb) != 0) {
        printf("[wifi] FAIL: scan could not start\n");
        return;
    }
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        blink();
        sleep_ms(100);
    }
    printf("[wifi] scan done. Radio works. \"%s\" %s\n", SKI_WIFI_SSID,
           skisim_seen ? "is visible (server AP is up)"
                       : "not seen (fine if the server is not running)");
}

// ------------------------------------------------------------- line check
// The MPU6050 breakout has its own pull-up resistors on SDA and SCL. With a
// weak internal pull-down on the Pico side, a pin wired to a powered sensor
// reads HIGH, while an unconnected pin reads LOW. This shows which GPIOs the
// I2C wires actually land on, without trusting labels or photos.
static void line_check(void) {
    // Every user GPIO except UART0 (GP0/GP1); GP23-25 and GP29 are wireless-chip pins.
    static const uint pins[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28 };
    printf("\n[line] pins pulled HIGH by the sensor (pull-down test):\n");
    int high = 0;
    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++) {
        uint p = pins[i];
        gpio_init(p);
        gpio_set_dir(p, GPIO_IN);
        gpio_pull_down(p);
        sleep_us(200);
        if (gpio_get(p)) {
            printf("  GP%u is HIGH\n", p);
            high++;
        }
        gpio_disable_pulls(p);
        gpio_deinit(p);
    }
    if (high == 0) {
        printf("  none. The sensor is probably unpowered, or no I2C wire reaches the Pico.\n");
    }
}

// ----------------------------------------------------------------- I2C scan
static void i2c_setup(pin_pair_t p) {
    i2c_init(I2C_PORT, I2C_BAUD);
    gpio_set_function(p.sda, GPIO_FUNC_I2C);
    gpio_set_function(p.scl, GPIO_FUNC_I2C);
    gpio_pull_up(p.sda);
    gpio_pull_up(p.scl);
}

static void i2c_release(pin_pair_t p) {
    i2c_deinit(I2C_PORT);
    gpio_deinit(p.sda);
    gpio_deinit(p.scl);
    gpio_disable_pulls(p.sda);
    gpio_disable_pulls(p.scl);
}

// Returns true if MPU_ADDR answered on this pin pair.
static bool i2c_scan(pin_pair_t p) {
    printf("\n[i2c] scanning I2C0 with SDA=GP%u, SCL=GP%u\n", p.sda, p.scl);
    i2c_setup(p);
    int found = 0;
    bool mpu = false;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        if (i2c_read_timeout_us(I2C_PORT, addr, &dummy, 1, false, 2000) >= 0) {
            printf("  device at 0x%02X%s\n", addr,
                   addr == MPU_ADDR ? "   <-- MPU6050" : "");
            found++;
            if (addr == MPU_ADDR) mpu = true;
        }
    }
    if (!found) printf("  no devices\n");
    if (!mpu) i2c_release(p);   // keep the bus up if the MPU is here
    return mpu;
}

// ------------------------------------------------------------------ MPU6050
static bool mpu_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write_timeout_us(I2C_PORT, MPU_ADDR, buf, 2, false, 5000) == 2;
}

static bool mpu_read(uint8_t reg, uint8_t *dst, size_t n) {
    if (i2c_write_timeout_us(I2C_PORT, MPU_ADDR, &reg, 1, true, 5000) != 1) return false;
    return i2c_read_timeout_us(I2C_PORT, MPU_ADDR, dst, n, false, 5000) == (int)n;
}

static bool mpu_init(void) {
    uint8_t who = 0;
    if (!mpu_read(REG_WHO_AM_I, &who, 1)) {
        printf("[mpu] FAIL: cannot read WHO_AM_I\n");
        return false;
    }
    printf("[mpu] WHO_AM_I = 0x%02X %s\n", who,
           who == 0x68 ? "(OK)" : "(unexpected, but continuing)");
    bool ok = mpu_write(REG_PWR_MGMT_1, 0x00)      // wake up
           && mpu_write(REG_ACCEL_CONFIG, 0x00)    // +-2 g
           && mpu_write(REG_GYRO_CONFIG, 0x00)     // +-250 deg/s
           && mpu_write(REG_CONFIG, 0x03);         // DLPF about 44 Hz
    if (!ok) printf("[mpu] FAIL: configuration write\n");
    return ok;
}

static void mpu_stream(void) {
    printf("\n[mpu] streaming at 10 Hz. Tilt the board left/right and watch\n"
           "      which of roll / pitch changes; that is the axis to use.\n\n");
    int errors = 0;
    while (true) {
        uint8_t b[14];
        if (!mpu_read(REG_ACCEL_XOUT_H, b, sizeof b)) {
            printf("[mpu] read error #%d\n", ++errors);
            sleep_ms(500);
            continue;
        }
        int16_t raw[7];
        for (int i = 0; i < 7; i++) raw[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]);
        float ax = raw[0] / 16384.0f, ay = raw[1] / 16384.0f, az = raw[2] / 16384.0f;
        float gx = raw[4] / 131.0f,   gy = raw[5] / 131.0f,   gz = raw[6] / 131.0f;
        float roll  = atan2f(ay, az) * 57.2958f;
        float pitch = atan2f(ax, az) * 57.2958f;
        printf("acc g [%+5.2f %+5.2f %+5.2f]  gyro dps [%+7.1f %+7.1f %+7.1f]  "
               "roll %+6.1f  pitch %+6.1f\n", ax, ay, az, gx, gy, gz, roll, pitch);
        blink();
        sleep_ms(100);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);   // time to open the serial monitor
    printf("\n===== SkiSim client hardware check =====\n");

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        while (true) {
            printf("[led] FAIL: wireless chip init failed (is PICO_BOARD=pico_w?)\n");
            sleep_ms(1000);
        }
    }
    printf("[led] wireless chip OK, LED should be blinking\n");
    cyw43_arch_enable_sta_mode();

    wifi_scan();
    line_check();

    int hit = -1;
    for (int i = 0; i < (int)(sizeof candidates / sizeof candidates[0]); i++) {
        if (i2c_scan(candidates[i])) { hit = i; break; }
    }

    if (hit < 0) {
        printf("\n[mpu] FAIL: no MPU6050 at 0x68 on GP8/GP9 or GP16/GP17.\n"
               "      Check VIN/GND, and that SDA and SCL are not swapped.\n");
        while (true) { blink(); sleep_ms(1000); }   // slow blink = failed
    }

    printf("\n[mpu] found on SDA=GP%u, SCL=GP%u\n", candidates[hit].sda,
           candidates[hit].scl);
    if (!mpu_init()) {
        while (true) { blink(); sleep_ms(1000); }
    }
    mpu_stream();
}
