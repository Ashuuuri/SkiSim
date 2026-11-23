// Game logic (core 0). Fixed-point (fix15) state and physics, as PROJECT.md
// asks; sine values come from a table built once at start-up.
//
// Snowboard physics, per tick (see game.h for the idea):
//   turn rate  = edge * (v + V_PIVOT) / TURN_RADIUS       (smoothed, so turns build up)
//   heading   += turn rate * dt
//   a          = G_SLOPE * cos(heading)                   gravity along the board
//              - MU_G - DRAG * v^2 - EDGE_DRAG * |edge| * v  friction, air, carving
//   v         += a * dt   (never below 0; at rest, static friction holds the board)
//   downhill  = v * cos(heading)   -> objects scroll towards the rider
//   across    = v * sin(heading)   -> rider moves across the run
// A gate is scored when it reaches z = 0: passed if the rider is between
// its signs, missed otherwise.

#include "game.h"

#include <math.h>
#include <string.h>
#include "config.h"

// Distances in metres, speeds in m/s
#define Z_FAR            int2fix15(60)    // objects appear this far ahead
#define Z_GONE           (-int2fix15(1))  // and are removed once behind the skier

#define GATE_SPACING     int2fix15(18)
#define POLE_SPACING     int2fix15(6)
#define TREE_SPACING     int2fix15(8)
#define DOT_SPACING      float2fix15(2.5f)

// Physics
#define SLOPE_DEG        25.0f                // a steep red/black run
#define G_SLOPE          float2fix15(9.81f * 0.42262f)   // g * sin(25 deg) = 4.15 m/s^2
#define MU_G             float2fix15(0.06f * 9.81f * 0.90631f) // snow friction: mu * g * cos(25 deg)
#define DRAG             float2fix15(0.008f)  // air drag per (m/s)^2: top speed about 80 km/h
#define EDGE_DRAG        float2fix15(0.25f)   // extra slowing per m/s when on a hard edge
#define TURN_RADIUS      float2fix15(7.0f)    // metres, at full edge
#define V_PIVOT          float2fix15(2.5f)    // lets the board pivot from a standstill
#define RATE_SMOOTH      float2fix15(0.15f)   // how quickly the turn rate follows the edge
#define HEADING_START    (-int2fix15(90))     // at rest, board across the slope (nose left)
#define HEADING_MAX      int2fix15(110)       // a little past across = brake uphill
#define RAD2DEG          float2fix15(57.2958f)
#define RUN_HALF_M       int2fix15(12)        // half-width of the run in metres

#define GATE_HALF_START  float2fix15(0.30f)
#define GATE_HALF_MIN    float2fix15(0.18f)
#define GATE_HALF_STEP   float2fix15(0.006f)  // gates narrow a little each time
#define GATE_X_MAX       float2fix15(0.65f)
#define GATE_X_JUMP      float2fix15(0.60f)   // max sideways step between gates

#define START_LEAN       float2fix15(0.6f)    // lean right this far ...
#define START_HOLD_MS    1000                 // ... for this long to start

#define SCORE_GATE       10
#define SCORE_MISS       5

static game_view_t g;
static uint32_t rng;
static fix15 next_gate, next_pole, next_tree, next_dot;  // metres until the next spawn
static uint16_t gates_spawned;
static fix15 last_gate_x;
static uint32_t lean_ms;
static bool on_edge;
static fix15 heading_rate;             // deg/s, smoothed
static fix15 sin_table[91];            // sin(0..90 deg), fix15

// sin of an angle in fix15 degrees, from the table with linear interpolation
static fix15 sin_deg(fix15 a) {
    while (a >  int2fix15(180)) a -= int2fix15(360);
    while (a < -int2fix15(180)) a += int2fix15(360);
    bool neg = a < 0;
    if (neg) a = -a;
    if (a > int2fix15(90)) a = int2fix15(180) - a;
    int i = fix2int15(a);
    fix15 frac = a - int2fix15(i);
    fix15 v = (i >= 90) ? sin_table[90]
                        : sin_table[i] + multfix15(sin_table[i + 1] - sin_table[i], frac);
    return neg ? -v : v;
}

static fix15 cos_deg(fix15 a) { return sin_deg(a + int2fix15(90)); }

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static uint32_t rand32(void) {          // xorshift32
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

// Uniform in [-1, +1) as fix15
static fix15 rand_unit(void) {
    return (fix15)((int32_t)(rand32() >> 16) - 32768);
}

static fix15 clampf15(fix15 v, fix15 lo, fix15 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void add_obj(uint8_t kind, uint8_t variant, fix15 x, fix15 z, fix15 half_w) {
    if (g.n_obj >= GAME_MAX_OBJ) return;   // full: skip this one, nothing breaks
    game_obj_t *o = &g.obj[g.n_obj++];
    o->kind = kind;
    o->variant = variant;
    o->x = x;
    o->z = z;
    o->half_w = half_w;
}

static void spawn_gate(fix15 z) {
    fix15 x = last_gate_x + multfix15(rand_unit(), GATE_X_JUMP);
    x = clampf15(x, -GATE_X_MAX, GATE_X_MAX);
    fix15 half = GATE_HALF_START - (fix15)gates_spawned * GATE_HALF_STEP;
    if (half < GATE_HALF_MIN) half = GATE_HALF_MIN;
    add_obj(OBJ_GATE, (uint8_t)(gates_spawned & 1), x, z, half);
    last_gate_x = x;
    gates_spawned++;
}

static void spawn_scenery(fix15 z, uint8_t kind) {
    switch (kind) {
    case OBJ_POLE:
        add_obj(OBJ_POLE, 0, -int2fix15(1), z, 0);
        add_obj(OBJ_POLE, 0,  int2fix15(1), z, 0);
        break;
    case OBJ_TREE: {
        // beside the slope, 1.2 to 1.8 half-widths out, random side
        fix15 out = float2fix15(1.5f) + multfix15(rand_unit(), float2fix15(0.3f));
        add_obj(OBJ_TREE, (uint8_t)(rand32() & 1), (rand32() & 1) ? out : -out, z, 0);
        break;
    }
    case OBJ_DOT:
        add_obj(OBJ_DOT, (uint8_t)(rand32() & 1), rand_unit(), z, 0);
        break;
    }
}

// Move everything towards the skier by d metres, spawn new objects at the
// far end, and score gates that reach the skier. Returns GAME_EV_* bits.
static uint32_t scroll(fix15 d, bool gates_on) {
    uint32_t ev = 0;

    // move and score
    int keep = 0;
    for (int i = 0; i < g.n_obj; i++) {
        game_obj_t o = g.obj[i];
        fix15 z_before = o.z;
        o.z -= d;
        if (o.kind == OBJ_GATE && z_before > 0 && o.z <= 0 && g.state == GS_RUN) {
            g.gates_total++;
            if (absfix15(o.x - g.skier_x) < o.half_w) {
                g.gates_passed++;
                g.score += SCORE_GATE;
                g.last_event = GAME_EV_GATE;
                ev |= GAME_EV_GATE;
            } else {
                g.gates_missed++;
                g.score -= SCORE_MISS;
                if (g.score < 0) g.score = 0;
                g.last_event = GAME_EV_MISS;
                ev |= GAME_EV_MISS;
            }
            g.last_event_ms = g.run_ms;
        }
        if (o.z > Z_GONE) g.obj[keep++] = o;
    }
    g.n_obj = (uint8_t)keep;

    // spawn at the far end, keeping exact spacing
    next_pole -= d;
    while (next_pole <= 0) { spawn_scenery(Z_FAR + next_pole, OBJ_POLE); next_pole += POLE_SPACING; }
    next_tree -= d;
    while (next_tree <= 0) { spawn_scenery(Z_FAR + next_tree, OBJ_TREE); next_tree += TREE_SPACING; }
    next_dot -= d;
    while (next_dot <= 0)  { spawn_scenery(Z_FAR + next_dot, OBJ_DOT);  next_dot += DOT_SPACING; }
    if (gates_on) {
        next_gate -= d;
        while (next_gate <= 0) {
            if (gates_spawned < SKI_RUN_GATES) spawn_gate(Z_FAR + next_gate);
            next_gate += GATE_SPACING;
        }
    }
    return ev;
}

// Empty slope with scenery from the skier to the horizon (no gates)
static void reset_scene(void) {
    g.n_obj = 0;
    next_pole = next_tree = next_dot = 0;
    next_gate = GATE_SPACING;
    // scroll a full screen of scenery into view, in small steps
    for (int i = 0; i < 60; i++) scroll(int2fix15(1), false);
}

static void enter_menu(void) {
    g.state = GS_MENU;
    g.state_ms = 0;
    g.speed = 0;
    lean_ms = 0;
    // drop leftover gates, keep the scenery
    int keep = 0;
    for (int i = 0; i < g.n_obj; i++) {
        if (g.obj[i].kind != OBJ_GATE) g.obj[keep++] = g.obj[i];
    }
    g.n_obj = (uint8_t)keep;
}

static void start_run(void) {
    g.state = GS_RUN;
    g.state_ms = 0;
    g.run_ms = 0;
    g.score = 0;
    g.gates_passed = g.gates_missed = g.gates_total = 0;
    g.last_event = 0;
    g.skier_x = 0;
    g.speed = 0;                        // start at rest ...
    g.heading = HEADING_START;          // ... with the board across the slope
    g.dist = 0;
    heading_rate = 0;
    gates_spawned = 0;
    last_gate_x = 0;
    on_edge = false;
    // place the first gates part way down the slope so the action starts soon
    for (int z = 24; z < 60; z += 18) spawn_gate(int2fix15(z));
    next_gate = GATE_SPACING - int2fix15(60 - 42);
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------
void game_init(uint32_t seed) {
    memset(&g, 0, sizeof g);
    for (int d = 0; d <= 90; d++) sin_table[d] = float2fix15(sinf((float)d * 0.0174533f));
    g.heading = HEADING_START;
    rng = seed ? seed : 0x5EED1234u;
    reset_scene();
    enter_menu();
}

uint32_t game_update(fix15 steer, bool start, uint32_t dt_ms) {
    uint32_t ev = 0;
    fix15 dt = (fix15)(((int32_t)dt_ms << 15) / 1000);
    g.steer = steer;
    g.edge = steer;                     // board tilt = edge angle (+ toe side, - heel side)
    g.state_ms += dt_ms;

    switch (g.state) {
    case GS_MENU:
        // ease the skier back to the middle; lean right and hold to start
        g.skier_x -= multfix15(g.skier_x, float2fix15(0.1f));
        g.heading = HEADING_START;
        g.speed = 0;
        lean_ms = (steer > START_LEAN) ? lean_ms + dt_ms : 0;
        if (start || lean_ms >= START_HOLD_MS) {
            start_run();
            ev |= GAME_EV_START;
        }
        break;

    case GS_RUN: {
        g.run_ms += dt_ms;

        // --- turning: the edge sets a turn rate that grows with speed ---------
        fix15 omega = divfix(multfix15(multfix15(steer, g.speed + V_PIVOT), RAD2DEG), TURN_RADIUS);
        heading_rate += multfix15(omega - heading_rate, RATE_SMOOTH);
        g.heading += multfix15(heading_rate, dt);
        if (g.heading >  HEADING_MAX) { g.heading =  HEADING_MAX; heading_rate = 0; }
        if (g.heading < -HEADING_MAX) { g.heading = -HEADING_MAX; heading_rate = 0; }

        // --- speed: gravity along the board against friction, air and edge ---
        fix15 c = cos_deg(g.heading);
        fix15 drive = multfix15(G_SLOPE, c);            // negative when pointing uphill
        fix15 resist = MU_G
                     + multfix15(DRAG, multfix15(g.speed, g.speed))
                     + multfix15(multfix15(EDGE_DRAG, absfix15(steer)), g.speed);
        fix15 acc;
        if (g.speed > 0)          acc = drive - resist;
        else if (drive > MU_G)    acc = drive - MU_G;   // breaks away from rest
        else                      acc = 0;              // static friction holds it
        g.speed += multfix15(acc, dt);
        if (g.speed < 0) g.speed = 0;

        // --- motion: downhill part scrolls the run, across part moves the rider
        fix15 v_down = multfix15(g.speed, c);
        if (v_down < 0) v_down = 0;
        fix15 v_across = multfix15(g.speed, sin_deg(g.heading));
        g.skier_x += multfix15(divfix(v_across, RUN_HALF_M), dt);

        bool edge = g.skier_x >= int2fix15(1) || g.skier_x <= -int2fix15(1);
        g.skier_x = clampf15(g.skier_x, -int2fix15(1), int2fix15(1));
        if (edge) {                                     // brushing the fence scrubs speed
            g.speed = multfix15(g.speed, float2fix15(0.97f));
            if (!on_edge) ev |= GAME_EV_EDGE;
        }
        on_edge = edge;

        g.dist += multfix15(v_down, dt);
        ev |= scroll(multfix15(v_down, dt), true);

        if (g.gates_total >= SKI_RUN_GATES) {
            g.state = GS_RESULT;
            g.state_ms = 0;
            if (g.score > g.best) g.best = g.score;
            ev |= GAME_EV_END;
        }
        break;
    }

    case GS_RESULT:
        g.speed = 0;
        if (start) {
            enter_menu();
            start_run();
            ev |= GAME_EV_START;
        } else if (g.state_ms >= SKI_RESULT_SECONDS * 1000u) {
            enter_menu();
        }
        break;
    }
    return ev;
}

const game_view_t *game_view(void) {
    return &g;
}
