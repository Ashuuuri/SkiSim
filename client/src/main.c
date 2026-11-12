// SkiSim client (board unit), Phase 1 steps 1-2:
// blink the LED, print over serial, and join the server's WiFi AP.
//
// LED: fast blink = looking for the server, slow blink = connected.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "net_client.h"

#define BLINK_FAST_MS  100
#define BLINK_SLOW_MS  500

int main(void) {
    stdio_init_all();
    sleep_ms(1500);   // give USB serial time to enumerate so early prints are not lost
    printf("\n[client] SkiSim client starting\n");

    if (net_client_init() != 0) {
        // Without the wireless chip there is no LED either; just report and stop.
        while (true) {
            printf("[client] wireless init failed, check PICO_BOARD=pico_w\n");
            sleep_ms(1000);
        }
    }

    bool led = false;
    absolute_time_t next_blink = get_absolute_time();
    absolute_time_t next_report = make_timeout_time_ms(1000);
    uint32_t seconds = 0;

    while (true) {
        net_client_poll();

        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            next_blink = make_timeout_time_ms(
                net_client_is_connected() ? BLINK_SLOW_MS : BLINK_FAST_MS);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
            next_report = make_timeout_time_ms(1000);
            seconds++;
            printf("[client] t=%lus  wifi=%s\n", (unsigned long)seconds,
                   net_client_is_connected() ? "connected" : "searching");
        }
    }
}
