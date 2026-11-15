// SkiSim client (board unit), Phase 1 step 3:
// join the server's WiFi AP and send a test packet every 10 ms.
// The angle is a fixed test value until the MPU6050 is wired in (step 4).
//
// LED: fast blink = looking for the server, slow blink = connected.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "net_client.h"
#include "protocol.h"
#include "fix15.h"

#define BLINK_FAST_MS     100
#define BLINK_SLOW_MS     500
#define SEND_PERIOD_US    10000          // 100 packets per second
#define TEST_ANGLE_DEG    1.5f           // fixed test value for step 3

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
    absolute_time_t next_send = get_absolute_time();
    uint32_t seconds = 0;

    uint32_t seq = 0;
    uint32_t sent = 0, send_errors = 0;   // counts for the current second

    while (true) {
        net_client_poll();

        if (absolute_time_diff_us(get_absolute_time(), next_send) <= 0) {
            next_send = delayed_by_us(next_send, SEND_PERIOD_US);
            // If we fell far behind (e.g. a long join), do not burst to catch up.
            if (absolute_time_diff_us(get_absolute_time(), next_send) < 0) {
                next_send = make_timeout_time_us(SEND_PERIOD_US);
            }
            if (net_client_is_connected()) {
                ski_packet_t pkt = {
                    .magic   = SKI_PKT_MAGIC,
                    .version = SKI_PKT_VERSION,
                    .unit_id = SKI_UNIT_BOARD,
                    .seq     = ++seq,
                    .t_us    = time_us_32(),
                    .angle   = float2fix15(TEST_ANGLE_DEG),
                    .rate    = 0,
                    .flags   = 0,
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
            printf("[client] t=%lus  wifi=%s  sent=%lu/s  errors=%lu  seq=%lu\n",
                   (unsigned long)seconds,
                   net_client_is_connected() ? "connected" : "searching",
                   (unsigned long)sent, (unsigned long)send_errors,
                   (unsigned long)seq);
            sent = 0;
            send_errors = 0;
        }
    }
}
