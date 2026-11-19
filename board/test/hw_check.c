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
#include "pico/stdio_usb.h"
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

// Diagnostic: 0 = never start the wireless chip, to rule out supply noise
// from WiFi activity. The LED is driven by the wireless chip, so it stays off.
#define USE_WIFI_CHIP 1

static bool wifi_up = false;
static bool led = false;
static void blink(void) {
    led = !led;
    if (wifi_up) cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
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

// Last I2C return codes, for diagnostics (PICO_ERROR_GENERIC = -1 is a NACK,
// PICO_ERROR_TIMEOUT = -2 is a stuck or noisy bus).
static int last_wr_rc, last_rd_rc;

static bool mpu_read(uint8_t reg, uint8_t *dst, size_t n) {
    last_rd_rc = 0;
    last_wr_rc = i2c_write_timeout_us(I2C_PORT, MPU_ADDR, &reg, 1, true, 5000);
    if (last_wr_rc != 1) return false;
    last_rd_rc = i2c_read_timeout_us(I2C_PORT, MPU_ADDR, dst, n, false, 5000);
    return last_rd_rc == (int)n;
}

// Standard I2C bus recovery (same idea as the Lab 3 demo): if the MPU6050 was
// left mid-transfer by a Pico reset, it can hold SDA low. Clock SCL by hand
// until SDA is released, then send a STOP. Returns true if SDA was stuck.
static void line_set(uint pin, bool high) {
    if (high) {
        gpio_set_dir(pin, GPIO_IN);       // released; pull-up makes it high
    } else {
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);      // driven low
    }
    sleep_us(5);
}

static bool i2c_bus_recover(pin_pair_t p) {
    i2c_deinit(I2C_PORT);
    gpio_init(p.sda);
    gpio_init(p.scl);
    gpio_pull_up(p.sda);
    gpio_pull_up(p.scl);
    line_set(p.sda, true);
    line_set(p.scl, true);
    bool stuck = !gpio_get(p.sda);
    for (int k = 0; k < 9 && !gpio_get(p.sda); k++) {
        line_set(p.scl, false);
        line_set(p.scl, true);
    }
    // STOP: SDA low -> high while SCL is high
    line_set(p.scl, false);
    line_set(p.sda, false);
    line_set(p.scl, true);
    line_set(p.sda, true);
    return stuck;
}

// --------------------------------------------------------- software I2C
// Bit-banged I2C that bypasses the RP2040 I2C block entirely. Used only for
// diagnosis: it shows whether the wiring works, and in which orientation.
#define SOFT_HALF_US 50    // about 10 kHz, slow on purpose for diagnosis

static bool soft_scl_high(uint scl) {
    line_set(scl, true);
    // Wait for the line to actually rise (detects a slave holding SCL low).
    for (int i = 0; i < 1000; i++) {
        if (gpio_get(scl)) return true;
        sleep_us(1);
    }
    return false;
}

static void soft_start(pin_pair_t p) {
    line_set(p.sda, true);
    soft_scl_high(p.scl);
    sleep_us(SOFT_HALF_US);
    line_set(p.sda, false);
    sleep_us(SOFT_HALF_US);
    line_set(p.scl, false);
}

static void soft_stop(pin_pair_t p) {
    line_set(p.sda, false);
    sleep_us(SOFT_HALF_US);
    soft_scl_high(p.scl);
    sleep_us(SOFT_HALF_US);
    line_set(p.sda, true);
    sleep_us(SOFT_HALF_US);
}

// Returns true on ACK; *scl_ok goes false if SCL never rose.
static bool soft_write(pin_pair_t p, uint8_t b, bool *scl_ok) {
    for (int i = 7; i >= 0; i--) {
        line_set(p.sda, (b >> i) & 1);
        sleep_us(SOFT_HALF_US);
        if (!soft_scl_high(p.scl)) *scl_ok = false;
        sleep_us(SOFT_HALF_US);
        line_set(p.scl, false);
    }
    line_set(p.sda, true);               // release SDA for ACK
    sleep_us(SOFT_HALF_US);
    if (!soft_scl_high(p.scl)) *scl_ok = false;
    bool ack = !gpio_get(p.sda);
    sleep_us(SOFT_HALF_US);
    line_set(p.scl, false);
    return ack;
}

static uint8_t soft_read(pin_pair_t p, bool ack, bool *scl_ok) {
    uint8_t b = 0;
    line_set(p.sda, true);
    for (int i = 0; i < 8; i++) {
        sleep_us(SOFT_HALF_US);
        if (!soft_scl_high(p.scl)) *scl_ok = false;
        b = (b << 1) | gpio_get(p.sda);
        sleep_us(SOFT_HALF_US);
        line_set(p.scl, false);
    }
    line_set(p.sda, !ack);               // ACK = pull low, NACK = release
    sleep_us(SOFT_HALF_US);
    if (!soft_scl_high(p.scl)) *scl_ok = false;
    sleep_us(SOFT_HALF_US);
    line_set(p.scl, false);
    line_set(p.sda, true);
    return b;
}

static void soft_probe(pin_pair_t p) {
    i2c_deinit(I2C_PORT);
    gpio_init(p.sda);
    gpio_init(p.scl);
    gpio_pull_up(p.sda);
    gpio_pull_up(p.scl);
    line_set(p.sda, true);
    line_set(p.scl, true);

    bool scl_ok = true;
    soft_start(p);
    bool a1 = soft_write(p, MPU_ADDR << 1, &scl_ok);          // address + write
    bool a2 = a1 && soft_write(p, REG_WHO_AM_I, &scl_ok);    // register
    uint8_t who = 0;
    bool a3 = false;
    if (a2) {
        soft_start(p);                                       // repeated start
        a3 = soft_write(p, (MPU_ADDR << 1) | 1, &scl_ok);    // address + read
        if (a3) who = soft_read(p, false, &scl_ok);
    }
    soft_stop(p);
    printf("[soft] SDA=GP%u SCL=GP%u: addr+W %s, reg %s, addr+R %s, "
           "WHO_AM_I=0x%02X, SCL %s\n", p.sda, p.scl,
           a1 ? "ACK" : "NACK", a2 ? "ACK" : "-", a3 ? "ACK" : "-", who,
           scl_ok ? "ok" : "HELD LOW (stretching or wiring)");
    gpio_disable_pulls(p.sda);
    gpio_disable_pulls(p.scl);
    gpio_deinit(p.sda);
    gpio_deinit(p.scl);
}

static void soft_scan(pin_pair_t p) {
    i2c_deinit(I2C_PORT);
    gpio_init(p.sda);
    gpio_init(p.scl);
    gpio_pull_up(p.sda);
    gpio_pull_up(p.scl);
    line_set(p.sda, true);
    line_set(p.scl, true);
    printf("[soft] scan SDA=GP%u SCL=GP%u (idle SDA=%d SCL=%d):", p.sda, p.scl,
           gpio_get(p.sda), gpio_get(p.scl));
    int n = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        bool scl_ok = true;
        soft_start(p);
        bool ack = soft_write(p, a << 1, &scl_ok);
        soft_stop(p);
        if (ack) { printf(" 0x%02X", a); n++; }
    }
    printf(n ? "\n" : " none\n");
    gpio_disable_pulls(p.sda);
    gpio_disable_pulls(p.scl);
    gpio_deinit(p.sda);
    gpio_deinit(p.scl);
}

static bool mpu_configure(void);

static bool mpu_init(pin_pair_t p) {
    bool stuck = i2c_bus_recover(p);
    printf("[mpu] bus recovery: SDA was %s; now SDA=%d SCL=%d\n",
           stuck ? "STUCK LOW" : "free", gpio_get(p.sda), gpio_get(p.scl));
    i2c_setup(p);

    uint8_t who = 0;
    bool ok = false;
    for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
        ok = mpu_read(REG_WHO_AM_I, &who, 1);
        if (!ok) {
            printf("[mpu] WHO_AM_I attempt %d (repeated start) failed: "
                   "write rc=%d, read rc=%d, SDA=%d SCL=%d\n", attempt,
                   last_wr_rc, last_rd_rc, gpio_get(p.sda), gpio_get(p.scl));
            sleep_ms(50);
        }
    }
    if (!ok) {
        // Alternative: write the register pointer with a STOP, then read.
        uint8_t reg = REG_WHO_AM_I;
        int wr = i2c_write_timeout_us(I2C_PORT, MPU_ADDR, &reg, 1, false, 50000);
        int rd = i2c_read_timeout_us(I2C_PORT, MPU_ADDR, &who, 1, false, 50000);
        printf("[mpu] WHO_AM_I with STOP in between: write rc=%d, read rc=%d, "
               "value 0x%02X\n", wr, rd, who);
        ok = (wr == 1 && rd == 1);
    }
    if (!ok) {
        printf("[mpu] FAIL: cannot read WHO_AM_I\n");
        return false;
    }
    printf("[mpu] WHO_AM_I = 0x%02X %s\n", who,
           who == 0x68 ? "(OK)" : "(unexpected, but continuing)");
    return mpu_configure();
}

// Diagnostic: 1 = keep the three gyro axes in standby (accelerometer only),
// which cuts the sensor's supply current by roughly 90%.
#define GYRO_STANDBY 0
#define REG_PWR_MGMT_2    0x6C

// Wake the MPU6050 and set ranges. Assumes the I2C bus is already up.
static bool mpu_configure(void) {
#if GYRO_STANDBY
    printf("[mpu] gyro in standby (accelerometer only) for this test\n");
    if (!mpu_write(REG_PWR_MGMT_2, 0x07)) return false;   // STBY_XG|YG|ZG
#endif
    bool ok = mpu_write(REG_PWR_MGMT_1, 0x00)      // wake up
           && mpu_write(REG_ACCEL_CONFIG, 0x00)    // +-2 g
           && mpu_write(REG_GYRO_CONFIG, 0x00)     // +-250 deg/s
           && mpu_write(REG_CONFIG, 0x03);         // DLPF about 44 Hz
    if (!ok) printf("[mpu] FAIL: configuration write\n");
    return ok;
}

// After waking the sensor, check it is still alive with single-register
// reads before starting the 14-byte burst reads.
static void mpu_after_wake_check(void) {
    sleep_ms(100);   // let the MPU6050 settle after leaving sleep mode
    for (int i = 1; i <= 5; i++) {
        uint8_t who = 0, pwr = 0xFF;
        bool a = mpu_read(REG_WHO_AM_I, &who, 1);
        int wr1 = last_wr_rc, rd1 = last_rd_rc;
        bool b = mpu_read(REG_PWR_MGMT_1, &pwr, 1);
        printf("[wake] check %d: WHO_AM_I %s 0x%02X (rc %d/%d), PWR_MGMT_1 %s 0x%02X "
               "(rc %d/%d), SDA=%d SCL=%d\n", i, a ? "ok" : "FAIL", who, wr1, rd1,
               b ? "ok" : "FAIL", pwr, last_wr_rc, last_rd_rc,
               gpio_get(16), gpio_get(17));
        sleep_ms(200);
    }
}

// 50 Hz is smooth enough for tools/imu_viewer and light on USB serial.
#define STREAM_PERIOD_MS  20

static void mpu_stream(void) {
    printf("\n[mpu] streaming at %d Hz. Tilt the board left/right and watch\n"
           "      which of roll / pitch changes; that is the axis to use.\n\n",
           1000 / STREAM_PERIOD_MS);
    int errors = 0;
    int n = 0;
    absolute_time_t next = get_absolute_time();
    while (true) {
        next = delayed_by_ms(next, STREAM_PERIOD_MS);   // fixed sample times
        uint8_t b[14];
        if (!mpu_read(REG_ACCEL_XOUT_H, b, sizeof b)) {
            printf("[mpu] read error #%d (write rc=%d, read rc=%d, SDA=%d SCL=%d)\n",
                   ++errors, last_wr_rc, last_rd_rc, gpio_get(16), gpio_get(17));
            sleep_ms(500);
            continue;
        }
        int16_t raw[7];
        for (int i = 0; i < 7; i++) raw[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]);
        float ax = raw[0] / 16384.0f, ay = raw[1] / 16384.0f, az = raw[2] / 16384.0f;
        float gx = raw[4] / 131.0f,   gy = raw[5] / 131.0f,   gz = raw[6] / 131.0f;
        float roll  = atan2f(ay, az) * 57.2958f;
        float pitch = atan2f(ax, az) * 57.2958f;
        printf("acc g [%+6.3f %+6.3f %+6.3f]  gyro dps [%+7.1f %+7.1f %+7.1f]  "
               "roll %+6.1f  pitch %+6.1f\n", ax, ay, az, gx, gy, gz, roll, pitch);
        if (++n % (500 / STREAM_PERIOD_MS) == 0) blink();   // LED toggles twice a second
        sleep_until(next);
    }
}

// Talk to the MPU6050 on GP16/GP17 before any scan touches the bus, first
// with software I2C, then with the I2C hardware at two speeds.
// On success the I2C bus is LEFT UP at 100 kHz, so the caller can keep using
// it without tearing it down (tear-down and re-init seemed to upset the sensor).
static bool fresh_probe(pin_pair_t p) {
    printf("\n[fresh] first contact on SDA=GP%u, SCL=GP%u, before any scan:\n",
           p.sda, p.scl);
    // A Pico reboot does not power-cycle the sensor, so un-stick the bus and
    // reset the MPU6050 in software first (same sequence as the Lab 3 demo).
    bool stuck = i2c_bus_recover(p);
    if (stuck) printf("[fresh] SDA was stuck low; bus recovered\n");
    i2c_init(I2C_PORT, I2C_BAUD);
    gpio_set_function(p.sda, GPIO_FUNC_I2C);
    gpio_set_function(p.scl, GPIO_FUNC_I2C);
    gpio_pull_up(p.sda);
    gpio_pull_up(p.scl);
    if (mpu_write(REG_PWR_MGMT_1, 0x80)) {   // full device reset
        sleep_ms(100);
        mpu_write(0x68, 0x07);               // SIGNAL_PATH_RESET: gyro, accel, temp
        sleep_ms(100);
    }
    uint8_t who = 0;
    bool ok = mpu_read(REG_WHO_AM_I, &who, 1) && who == 0x68;
    printf("[fresh] hardware I2C at %u kHz: %s (write rc=%d, read rc=%d, "
           "WHO_AM_I=0x%02X)\n", I2C_BAUD / 1000, ok ? "OK" : "FAIL",
           last_wr_rc, last_rd_rc, who);
    if (!ok) i2c_release(p);
    return ok;
}

int main(void) {
    stdio_init_all();
    // Wait until a serial monitor opens the USB port, so the first contact
    // with the sensor is always captured. The LED blinks while waiting.
    while (!stdio_usb_connected()) sleep_ms(100);
    sleep_ms(300);
    printf("\n===== SkiSim client hardware check =====\n");

#if USE_WIFI_CHIP
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        while (true) {
            printf("[led] FAIL: wireless chip init failed (is PICO_BOARD=pico_w?)\n");
            sleep_ms(1000);
        }
    }
    wifi_up = true;
    printf("[led] wireless chip OK, LED should be blinking\n");
    cyw43_arch_enable_sta_mode();
#else
    printf("[led] wireless chip NOT started for this test (LED stays off)\n");
#endif

    // Fast path: if the sensor answers on its expected pins, go straight to
    // streaming. The scans below can confuse the MPU6050, so they only run
    // when this first contact fails.
    for (int i = (int)(sizeof candidates / sizeof candidates[0]) - 1; i >= 0; i--) {
        if (fresh_probe(candidates[i])) {
            printf("\n[mpu] found on SDA=GP%u, SCL=GP%u\n", candidates[i].sda,
                   candidates[i].scl);
            if (!mpu_configure()) {
                while (true) {
                    printf("[mpu] stopped: configuration write failed (write rc=%d)\n",
                           last_wr_rc);
                    blink(); sleep_ms(5000);
                }
            }
            mpu_after_wake_check();
            mpu_stream();
        }
    }

    printf("\n[diag] first contact failed on all pin pairs; running diagnostics\n");
    if (wifi_up) wifi_scan();
    line_check();

    int hit = -1;
    for (int i = 0; i < (int)(sizeof candidates / sizeof candidates[0]); i++) {
        if (i2c_scan(candidates[i])) { hit = i; break; }
    }

    if (hit < 0) {
        printf("\n[soft] hardware scan found nothing; software scan of both "
               "orientations:\n");
        for (int i = 0; i < (int)(sizeof candidates / sizeof candidates[0]); i++) {
            soft_scan(candidates[i]);
            soft_scan((pin_pair_t){ candidates[i].scl, candidates[i].sda });
        }
        printf("\n[mpu] FAIL: no MPU6050 at 0x68 on GP8/GP9 or GP16/GP17.\n"
               "      Check VIN/GND, and that SDA and SCL are not swapped.\n");
        while (true) {   // slow blink = failed
            printf("[mpu] stopped: no MPU6050 found (see the log above)\n");
            blink(); sleep_ms(5000);
        }
    }

    printf("\n[mpu] found on SDA=GP%u, SCL=GP%u\n", candidates[hit].sda,
           candidates[hit].scl);
    if (!mpu_init(candidates[hit])) {
        while (true) {
            printf("[mpu] stopped: found 0x68 in a scan, but WHO_AM_I could not be read\n");
            blink(); sleep_ms(5000);
        }
    }
    mpu_stream();
}
