// SkiSim server (host), Phase 1 steps 1-2:
// blink the LED, print over serial, and run the WiFi AP the client joins.
//
// LED: fast blink = waiting for a client, slow blink = a client is connected.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "net_ap.h"

#define BLINK_FAST_MS  100
#define BLINK_SLOW_MS  500

int main(void) {
    stdio_init_all();
    sleep_ms(1500);   // give USB serial time to enumerate so early prints are not lost
    printf("\n[server] SkiSim server starting\n");

    if (net_ap_init() != 0) {
        while (true) {
            printf("[server] wireless init failed, check PICO_BOARD=pico_w\n");
            sleep_ms(1000);
        }
    }

    bool led = false;
    int stations = 0;
    absolute_time_t next_blink = get_absolute_time();
    absolute_time_t next_report = make_timeout_time_ms(1000);
    uint32_t seconds = 0;

    while (true) {
        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            next_blink = make_timeout_time_ms(stations > 0 ? BLINK_SLOW_MS
                                                           : BLINK_FAST_MS);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
            next_report = make_timeout_time_ms(1000);
            seconds++;

            int now = net_ap_station_count();
            if (now != stations) {
                printf("[server] stations: %d -> %d\n", stations, now);
                stations = now;
            }
            printf("[server] t=%lus  stations=%d\n", (unsigned long)seconds,
                   stations);
        }
    }
}
