// SkiSim server (host), Phase 2: the game.
// Run the WiFi AP, receive the board's tilt, turn it into steering, run the
// game, play sounds, and print a summary every second.
//
// Core 0: network, sensor source, steering, game logic, serial (this file).
// Core 1: VGA drawing (render.c) and the audio interrupt (audio.c).
//
// The course VGA driver's PIO timing assumes a 150 MHz system clock. This
// build sets SYS_CLK_KHZ=150000 in CMakeLists.txt, so the board starts at
// 150 MHz before main() runs.
//
// LED: fast blink = waiting for a client, slow blink = a client is connected.

#include <math.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "net_ap.h"
#include "sensor_source.h"
#include "protocol.h"
#include "render.h"
#include "vga16_graphics_v3.h"
#include "config.h"
#include "game.h"
#include "audio.h"
#include "input.h"

#define BLINK_FAST_MS  100
#define BLINK_SLOW_MS  500

// Board tilt (degrees, relative to level) -> steer in -1..+1
static fix15 tilt_to_steer(float tilt) {
#if SKI_STEER_INVERT
    tilt = -tilt;
#endif
    if (fabsf(tilt) < SKI_STEER_DEADZONE) return 0;
    tilt += (tilt > 0) ? -SKI_STEER_DEADZONE : SKI_STEER_DEADZONE;
    float s = tilt / (SKI_STEER_FULL_DEG - SKI_STEER_DEADZONE);
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return float2fix15(s);
}

static void play_events(uint32_t ev) {
    if (ev & GAME_EV_START) audio_play(SFX_START);
    if (ev & GAME_EV_END)   audio_play(SFX_END);
    if (ev & GAME_EV_MISS)  audio_play(SFX_MISS);
    if (ev & GAME_EV_GATE)  audio_play(SFX_GATE);
    if (ev & GAME_EV_EDGE)  audio_play(SFX_EDGE);
}

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
    printf("\n[server] SkiSim server starting, sys clock %lu kHz\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000));

    // VGA first, so the course driver gets pio0 and its DMA channels before
    // the wireless driver claims a PIO state machine (PROJECT.md 3.4).
    initVGA();
    render_init();
    game_init(time_us_32());
    input_init();
    multicore_launch_core1(render_core1_entry);
    printf("[server] VGA and audio started on core 1\n");
    printf("[server] test keys: g start, a/d steer, s straight, w auto-weave, "
           "k keys/board, c re-zero, t sound test\n");

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
    absolute_time_t next_tick = get_absolute_time();
    uint32_t seconds = 0;
    sensor_stats_t prev[SENSOR_MAX_UNITS] = {0};
    uint32_t board_pkt_per_s = 0;

    input_state_t in = {0};
    float zero_deg = 0.0f;          // board angle taken as "level"
    bool was_live = false;

    while (true) {
        sensor_log_entry_t e;
        while (sensor_source_next_log(&e)) {
            printf("rx unit=%u seq=%lu angle=%+.2f rate=%+.1f flags=%u\n",
                   e.unit_id, (unsigned long)e.seq,
                   (double)fix2float15(e.angle), (double)fix2float15(e.rate),
                   e.flags);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            next_blink = make_timeout_time_ms(stations > 0 ? BLINK_SLOW_MS
                                                           : BLINK_FAST_MS);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_tick) <= 0) {
            next_tick = delayed_by_ms(next_tick, SKI_GAME_TICK_MS);
            if (absolute_time_diff_us(get_absolute_time(), next_tick) < 0) {
                next_tick = make_timeout_time_ms(SKI_GAME_TICK_MS);   // fell behind: no catch-up burst
            }

            // inputs
            input_poll(&in);
            sensor_sample_t s;
            sensor_stats_t st;
            bool live = sensor_source_get(SKI_UNIT_BOARD, &s);
            sensor_source_stats(SKI_UNIT_BOARD, &st);
            float angle = fix2float15(s.angle);

            // "level": taken when the board first connects, on 'c', and
            // slowly followed while the player stands in the menu
            const game_view_t *gv = game_view();
            if (live && (!was_live || in.rezero)) zero_deg = angle;
            if (live && gv->state == GS_MENU) {
                zero_deg += SKI_ZERO_TRACK_ALPHA * (angle - zero_deg);
            }
            was_live = live;

            // steering source: the board when it is live, otherwise keys
            bool use_keys = in.keyboard || in.weave || !live;
            fix15 steer;
            if (in.weave) {
                steer = float2fix15(0.9f * sinf(6.2832f * (float)(time_us_32() % 3000000u) / 3e6f));
            } else if (use_keys) {
                steer = in.key_steer;
            } else {
                steer = tilt_to_steer(angle - zero_deg);
            }

            uint32_t ev = game_update(steer, in.start, SKI_GAME_TICK_MS);
            play_events(ev);
            if (in.test_sound) {
                audio_play(SFX_GATE);
                printf("[audio] test chime on DAC channel A (CS GP%d, SCK GP%d, SDI GP%d)\n",
                       SKI_DAC_PIN_CS, SKI_DAC_PIN_SCK, SKI_DAC_PIN_MOSI);
            }

            // carve hiss: louder when going fast and turning hard
            gv = game_view();
            uint32_t hiss = 0;
            if (gv->state == GS_RUN) {
                float sp = fix2float15(gv->speed) / 20.0f;   // 0..1 of top speed
                float turn = fabsf(fix2float15(gv->steer));
                hiss = (uint32_t)(1024.0f * sp * (0.25f + 0.75f * turn));
            }
            audio_set_hiss((uint16_t)hiss);

            static render_frame_t fr;      // about 1 KB: keep it off the stack
            fr.game      = *gv;
            fr.live      = live;
            fr.ever      = st.received > 0;
            fr.keyboard  = use_keys;
            fr.weave     = in.weave;
            fr.stations  = stations;
            fr.angle     = s.angle;
            fr.zero      = float2fix15(zero_deg);
            fr.pkt_per_s = board_pkt_per_s;
            render_publish(&fr);
        }

        if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
            next_report = make_timeout_time_ms(1000);
            seconds++;
            sensor_stats_t board_now;
            sensor_source_stats(SKI_UNIT_BOARD, &board_now);
            board_pkt_per_s = board_now.received - prev[SKI_UNIT_BOARD].received;

            int now = net_ap_station_count();
            if (now != stations) {
                printf("[server] stations: %d -> %d\n", stations, now);
                stations = now;
            }
            const game_view_t *gs = game_view();
            static const char *names[] = { "MENU", "RUN", "RESULT" };
            printf("[server] t=%lus  stations=%d  invalid=%lu  game=%s score=%ld "
                   "gates=%u/%u steer=%+.2f\n",
                   (unsigned long)seconds, stations,
                   (unsigned long)sensor_source_invalid_count(),
                   names[gs->state], (long)gs->score, gs->gates_passed,
                   gs->gates_total, (double)fix2float15(gs->steer));
            for (uint8_t id = 1; id < SENSOR_MAX_UNITS; id++) {
                report_unit(id, &prev[id]);
            }
        }
    }
}
