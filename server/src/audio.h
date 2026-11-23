// Sound effects through the MCP4822 DAC (channel A), synthesized in a timer
// interrupt. The interrupt runs on core 1, so audio never competes with the
// WiFi stack on core 0. Same DAC wiring and SPI settings as course Lab 1.
//
// Sounds (all synthesized, no recordings):
//   carve hiss   filtered noise, loudness set by audio_set_hiss()
//   gate chime   two quick rising notes
//   miss         falling tone plus a noise burst
//   start / end  three-note jingles
#ifndef SKISIM_AUDIO_H
#define SKISIM_AUDIO_H

#include <stdint.h>

typedef enum {
    SFX_START = 0,
    SFX_GATE,
    SFX_MISS,
    SFX_EDGE,
    SFX_END,
} sfx_t;

// Call ON CORE 1 (render_core1_entry does this). Sets up SPI and the timer.
void audio_init_core1(void);

// Safe to call from core 0 at any time.
void audio_play(sfx_t sfx);
void audio_set_hiss(uint16_t level);   // 0 = silent .. 1024 = loudest

#endif // SKISIM_AUDIO_H
