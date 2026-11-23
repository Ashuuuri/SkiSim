// Game logic (core 0): slope, gates, steering, scoring, and the
// MENU -> RUN -> RESULT state machine. No drawing and no sound here:
// game_update() reports events, main.c turns them into sounds, and
// render.c draws the game_view_t snapshot on core 1.
//
// World units: x is across the run (-1 = left edge, +1 = right edge),
// z is metres downhill of the rider (0 = at the rider's board).
//
// Physics (Ray's spec): the board starts at rest, across the slope. Gravity
// pulls along the board in proportion to cos(heading), so pointing downhill
// accelerates hardest and pointing across the slope hardly at all. Snow
// friction, air drag and edge drag slow it down; speed carries through turns.
// Tilting the board puts it on its toe or heel edge, which turns it smoothly
// (faster at higher speed, like a carved turn); alternating gives S-turns.
#ifndef SKISIM_GAME_H
#define SKISIM_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "fix15.h"

typedef enum { GS_MENU = 0, GS_RUN, GS_RESULT } game_state_t;

typedef enum {
    OBJ_NONE = 0,
    OBJ_GATE,       // two flags; pass between them
    OBJ_TREE,       // scenery beside the slope
    OBJ_POLE,       // orange marker on the slope edge
    OBJ_DOT,        // snow texture that streams past to show speed
} obj_kind_t;

#define GAME_MAX_OBJ 64

typedef struct {
    uint8_t kind;     // obj_kind_t
    uint8_t variant;  // gates: 0 = red, 1 = blue; trees/dots: size or shade
    fix15   x;        // across the slope
    fix15   z;        // metres ahead
    fix15   half_w;   // gates: half the gap between the flags
} game_obj_t;

// Events from one game_update() call (bit mask)
#define GAME_EV_START   (1u << 0)
#define GAME_EV_GATE    (1u << 1)   // passed a gate
#define GAME_EV_MISS    (1u << 2)   // missed a gate
#define GAME_EV_END     (1u << 3)   // run finished
#define GAME_EV_EDGE    (1u << 4)   // skier hit the slope edge

typedef struct {
    game_state_t state;
    fix15    skier_x;       // -1..+1
    fix15    steer;         // -1..+1, after dead zone and scaling
    fix15    speed;         // m/s, along the board
    fix15    heading;       // board direction in degrees: 0 = straight downhill,
                            // +90 = across the slope to the right, -90 = to the left
    fix15    edge;          // edge angle input -1 (heel side) .. +1 (toe side)
    fix15    dist;          // metres travelled down the fall line this run (trail map)
    int32_t  score;
    int32_t  best;
    uint16_t gates_passed;
    uint16_t gates_missed;
    uint16_t gates_total;   // gates reached so far this run
    uint32_t run_ms;        // time since the run started
    uint32_t state_ms;      // time in the current state
    uint8_t  last_event;    // GAME_EV_GATE or GAME_EV_MISS, for the HUD
    uint32_t last_event_ms; // run_ms when it happened
    uint8_t  n_obj;
    game_obj_t obj[GAME_MAX_OBJ];
} game_view_t;

void game_init(uint32_t seed);

// Advance the game by dt_ms. steer is -1..+1. start is a key press.
// Returns a mask of GAME_EV_* that happened during this step.
uint32_t game_update(fix15 steer, bool start, uint32_t dt_ms);

// Current state, for drawing. Valid until the next game_update().
const game_view_t *game_view(void);

#endif // SKISIM_GAME_H
