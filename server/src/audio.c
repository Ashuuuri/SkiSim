// Sound effects: DDS synthesis in a hardware-timer interrupt on core 1,
// written to the MCP4822 over SPI. The structure follows the course Lab 1
// birdsong code (alarm ISR -> phase accumulator -> sine table -> DAC).

#include "audio.h"

#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "config.h"

#define FS            SKI_AUDIO_RATE_HZ
#define PERIOD_US     (1000000 / FS)
#define DAC_CHAN_A    0x3000            // channel A, 1x gain, active (Lab 1 value)
#define SINE_SIZE     256
#define MAX_NOTES     4

typedef struct {
    uint16_t f0, f1;     // start and end frequency (Hz); a glide if they differ
    uint16_t ms;         // duration
    uint16_t amp;        // 0..2047
    uint16_t noise_ms;   // optional noise burst at the start of this note
    uint16_t noise_amp;
} note_t;

// Each effect is a short list of notes, played one after another
typedef struct { uint8_t n; note_t notes[MAX_NOTES]; } sfx_def_t;

static const sfx_def_t SFX_DEFS[] = {
    [SFX_START] = { 3, { { 660,  660,  80, 900, 0, 0 },
                         { 880,  880,  80, 900, 0, 0 },
                         { 1320, 1320, 180, 900, 0, 0 } } },
    [SFX_GATE]  = { 2, { { 1319, 1319, 70, 1000, 0, 0 },
                         { 1760, 1760, 150, 1000, 0, 0 } } },
    [SFX_MISS]  = { 1, { { 300,  80,  350, 1000, 250, 900 } } },
    [SFX_EDGE]  = { 1, { { 150,  100, 120, 500, 120, 700 } } },
    [SFX_END]   = { 3, { { 1320, 1320, 120, 900, 0, 0 },
                         { 990,  990,  120, 900, 0, 0 },
                         { 660,  660,  320, 900, 0, 0 } } },
};
#define N_SFX (sizeof SFX_DEFS / sizeof SFX_DEFS[0])

// Shared with core 0 (single 32-bit words, so reads and writes are atomic)
static volatile int32_t  pending_sfx = -1;
static volatile uint32_t hiss_target = 0;

// ISR state (core 1 only)
static int16_t  sine[SINE_SIZE];
static int      alarm_num;
static const sfx_def_t *cur;
static uint8_t  note_idx;
static uint32_t note_left, note_len;     // samples
static uint32_t phase, phase_inc;
static int32_t  phase_step;              // per-sample change for glides
static uint32_t noise_left, noise_len;
static uint16_t lfsr = 0xACE1u;
static int32_t  hiss_lp, hiss_level;

static uint32_t hz_to_inc(uint32_t hz) {
    return (uint32_t)(((uint64_t)hz << 32) / FS);
}

static void load_note(void) {
    const note_t *n = &cur->notes[note_idx];
    note_len = note_left = (uint32_t)n->ms * FS / 1000;
    phase_inc = hz_to_inc(n->f0);
    int64_t diff = (int64_t)hz_to_inc(n->f1) - (int64_t)phase_inc;
    phase_step = note_len ? (int32_t)(diff / (int64_t)note_len) : 0;
    noise_len = noise_left = (uint32_t)n->noise_ms * FS / 1000;
}

static inline int32_t noise_sample(void) {
    // 16-bit Galois LFSR -> roughly uniform -2048..2047
    uint16_t lsb = lfsr & 1u;
    lfsr >>= 1;
    if (lsb) lfsr ^= 0xB400u;
    return (int32_t)(int16_t)lfsr >> 4;
}

static void audio_isr(void) {
    hw_clear_bits(&timer_hw->intr, 1u << alarm_num);
    timer_hw->alarm[alarm_num] = timer_hw->timerawl + PERIOD_US;   // re-arm (one-shot)

    // start a newly requested effect (the latest request wins)
    int32_t req = pending_sfx;
    if (req >= 0) {
        pending_sfx = -1;
        if ((uint32_t)req < N_SFX) {
            cur = &SFX_DEFS[req];
            note_idx = 0;
            load_note();
        }
    }

    int32_t out = 0;
    int32_t n = noise_sample();

    // tone + optional noise burst, each with a linear decay envelope
    if (cur) {
        const note_t *nt = &cur->notes[note_idx];
        if (note_left) {
            int32_t env = (int32_t)nt->amp * (int32_t)note_left / (int32_t)note_len;
            out += (sine[phase >> 24] * env) >> 11;
            phase += phase_inc;
            phase_inc += (uint32_t)phase_step;
            note_left--;
        }
        if (noise_left) {
            int32_t env = (int32_t)nt->noise_amp * (int32_t)noise_left / (int32_t)noise_len;
            out += (n * env) >> 11;
            noise_left--;
        }
        if (!note_left && !noise_left) {
            if (++note_idx < cur->n) load_note();
            else cur = NULL;
        }
    }

    // carve hiss: low-passed noise; loudness glides towards the target
    hiss_level += ((int32_t)hiss_target - hiss_level) >> 8;
    hiss_lp += (n - hiss_lp) >> 3;
    out += (hiss_lp * hiss_level) >> 11;

    out = out * SKI_AUDIO_VOLUME / 100;
    if (out > 2047) out = 2047;
    if (out < -2048) out = -2048;

    // the FIFO is 8 deep and we send one word per sample, so it never fills
    spi_get_hw(SKI_DAC_SPI)->dr = (uint32_t)(DAC_CHAN_A | ((out + 2048) & 0x0FFF));
}

void audio_init_core1(void) {
    for (int i = 0; i < SINE_SIZE; i++) {
        sine[i] = (int16_t)(2047.0f * sinf(6.2831853f * (float)i / SINE_SIZE));
    }

    spi_init(SKI_DAC_SPI, SKI_DAC_SPI_HZ);
    spi_set_format(SKI_DAC_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(SKI_DAC_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(SKI_DAC_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SKI_DAC_PIN_CS,   GPIO_FUNC_SPI);

    // A free hardware alarm whose interrupt is enabled from core 1, so the
    // handler runs on core 1.
    alarm_num = hardware_alarm_claim_unused(true);
    uint irq = timer_hardware_alarm_get_irq_num(timer_hw, alarm_num);
    irq_set_exclusive_handler(irq, audio_isr);
    hw_set_bits(&timer_hw->inte, 1u << alarm_num);
    irq_set_enabled(irq, true);
    timer_hw->alarm[alarm_num] = timer_hw->timerawl + PERIOD_US;
}

void audio_play(sfx_t sfx) {
    pending_sfx = (int32_t)sfx;
}

void audio_set_hiss(uint16_t level) {
    hiss_target = level > 1024 ? 1024 : level;
}
