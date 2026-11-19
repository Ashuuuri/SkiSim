// VGA drawing on core 1: the game screen.
//
// Core 0 runs the game and publishes a snapshot with render_publish();
// core 1 copies the latest snapshot and draws it. Core 1 also hosts the
// audio interrupt (audio_init_core1). It never touches lwIP or the
// wireless chip.
#ifndef SKISIM_RENDER_H
#define SKISIM_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include "fix15.h"
#include "game.h"

typedef struct {
    game_view_t game;
    // link and input status, for the HUD
    bool     live;        // board unit sending right now
    bool     ever;        // board unit has sent at least once
    bool     keyboard;    // steering from keys / auto-weave
    bool     weave;       // auto-weave on
    int      stations;    // WiFi clients
    fix15    angle;       // board tilt, degrees (raw, before calibration)
    fix15    zero;        // angle taken as level
    uint32_t pkt_per_s;
} render_frame_t;

// Call once on core 0 before launching core 1.
void render_init(void);

// Core 0: hand over the latest frame. Call at about 30-60 Hz.
void render_publish(const render_frame_t *f);

// Core 1 entry point: pass to multicore_launch_core1(). Never returns.
void render_core1_entry(void);

#endif // SKISIM_RENDER_H
