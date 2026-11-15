// SkiSim server (host), Phase 1 step 3:
// run the WiFi AP, receive sensor packets, and print a summary every second.
//
// LED: fast blink = waiting for a client, slow blink = a client is connected.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "net_ap.h"
#include "sensor_source.h"
#include "protocol.h"

#define BLINK_FAST_MS  100
#define BLINK_SLOW_MS  500

static void report_unit(uint8_t unit_id, sensor_stats_t *prev) {
    sensor_sample_t s;
    sensor_stats_t now;
    bool live = sensor_source_get(unit_id, &s);
    sensor_source_stats(unit_id, &now);

    if (now.received == 0) return;   // this unit has never sent anything

    uint32_t rate = now.received - prev->received;
    uint32_t gaps = now.gaps - prev->gaps;
    if (live) {
        printf("[server]   unit %u: %3lu pkt/s  seq=%-8lu  angle=%+6.2f deg  "
               "lost=%lu  stale=%lu  resets=%lu\n", unit_id,
               (unsigned long)rate, (unsigned long)s.seq, fix2float15(s.angle),
               (unsigned long)gaps, (unsigned long)(now.stale - prev->stale),
               (unsigned long)now.resets);
    } else {
        printf("[server]   unit %u: SENSOR LOST (no data for over %d ms), "
               "last seq=%lu\n", unit_id, SENSOR_TIMEOUT_US / 1000,
               (unsigned long)s.seq);
    }
    *prev = now;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);   // give USB serial time to enumerate so early prints are not lost
    printf("\n[server] SkiSim server starting\n");

    if (net_ap_init() != 0 || sensor_source_init() != 0) {
        while (true) {
            printf("[server] network init failed, check PICO_BOARD=pico_w\n");
            sleep_ms(1000);
        }
    }

    bool led = false;
    int stations = 0;
    absolute_time_t next_blink = get_absolute_time();
    absolute_time_t next_report = make_timeout_time_ms(1000);
    uint32_t seconds = 0;
    sensor_stats_t prev[SENSOR_MAX_UNITS] = {0};

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
            printf("[server] t=%lus  stations=%d  invalid=%lu\n",
                   (unsigned long)seconds, stations,
                   (unsigned long)sensor_source_invalid_count());
            for (uint8_t id = 1; id < SENSOR_MAX_UNITS; id++) {
                report_unit(id, &prev[id]);
            }
        }
    }
}
