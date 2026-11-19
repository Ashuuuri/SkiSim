// VGA drawing on core 1: the game screen, seen from behind the rider.
//
// The scene: a steep groomed run between snowy hills, dropping away over a
// crest below the mountains. The camera is fixed on the run, so the run, the
// hills, the sky and the mountains are drawn once and never move. Only the
// objects on the run (sign gates, trees, edge poles, snow marks) and the
// snowboarder move.
//
// Perspective: an object z metres ahead gets
//     s  = 1 / (z / Z_SCALE + 1)                    (1 at the rider, small far away)
//     sy = HORIZON + (GROUND_Y - HORIZON) * s       (screen row of its base)
//     hw = HW_TOP + (HW_BOT - HW_TOP) * s           (half-width of the run on that row)
//     sx = CX + x * hw                              (x = -1 left edge .. +1 right edge)
// so objects come over the crest small and grow as they rush towards you,
// always lined up with the edges of the run. The rider moves across the run
// the same way at z = 0, which keeps what you see and what is scored identical.
//
// The course VGA driver is single-buffered here (DOUBLE_BUFFER_NONE). Each
// frame we repaint the background only where something was last frame
// (bg_span() recomputes the background for any row), then draw again.
// The course fill routines do not clip negative coordinates, so all drawing
// goes through the clipped helpers below.

#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/sync.h"

#include "vga16_graphics_v3.h"
#include "audio.h"
#include "config.h"

#define FRAME_MS     33          // about 30 frames per second

// Screen layout
#define SCR_W        640
#define SCR_H        480
#define HUD_H        40          // status bar at the top
#define HORIZON      170         // crest of the run / foot of the mountains
#define GROUND_Y     446         // screen row of the rider's board
#define CX           320
#define HW_TOP       46.0f       // half-width of the run at the crest
#define HW_BOT       285.0f      // half-width of the run at the rider
#define Z_SCALE      5.0f        // metres per perspective step

#define SKY          LIGHT_BLUE
#define RUN_SNOW     WHITE       // groomed run
#define SIDE_SNOW    LIGHT_BLUE  // shaded snow on the hills beside the run
#define GROOM        LIGHT_BLUE  // corduroy lines on the run
#define HUD_BG       DARK_BLUE

// ---------------------------------------------------------------------------
// Snapshot hand-over from core 0
// ---------------------------------------------------------------------------
static critical_section_t lock;
static render_frame_t shared;
static render_frame_t f;            // core 1's private copy

void render_init(void) {
    critical_section_init(&lock);
    memset(&shared, 0, sizeof shared);
}

void render_publish(const render_frame_t *src) {
    critical_section_enter_blocking(&lock);
    shared = *src;
    critical_section_exit(&lock);
}

static void take_snapshot(void) {
    critical_section_enter_blocking(&lock);
    f = shared;
    critical_section_exit(&lock);
}

// ---------------------------------------------------------------------------
// Clipped drawing helpers, with bounding-box tracking
// ---------------------------------------------------------------------------
typedef struct { short x0, y0, x1, y1; } box_t;   // inclusive; x1 < x0 means empty

static int   clip_top = 0;          // rows above this are never touched
static box_t cur_box;               // grows as one thing is drawn

static void box_begin(void) { cur_box.x0 = SCR_W; cur_box.y0 = SCR_H; cur_box.x1 = -1; cur_box.y1 = -1; }

static void box_add(int x0, int y0, int x1, int y1) {
    if (x0 < cur_box.x0) cur_box.x0 = (short)x0;
    if (y0 < cur_box.y0) cur_box.y0 = (short)y0;
    if (x1 > cur_box.x1) cur_box.x1 = (short)x1;
    if (y1 > cur_box.y1) cur_box.y1 = (short)y1;
}

static void hspan(int y, int xa, int xb, char c) {
    if (y < clip_top || y >= SCR_H) return;
    if (xa > xb) { int t = xa; xa = xb; xb = t; }
    if (xa < 0) xa = 0;
    if (xb > SCR_W - 1) xb = SCR_W - 1;
    if (xb < xa) return;
    drawHLine(xa, y, xb - xa + 1, c);
    box_add(xa, y, xb, y);
}

static void rect(int x, int y, int w, int h, char c) {
    for (int j = y; j < y + h; j++) hspan(j, x, x + w - 1, c);
}

static void disc(int cx, int cy, int r, char c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        hspan(cy + dy, cx - dx, cx + dx, c);
    }
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, char c) {
    float t;
    if (y1 < y0) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y2 < y0) { t = x0; x0 = x2; x2 = t; t = y0; y0 = y2; y2 = t; }
    if (y2 < y1) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y2 - y0 < 0.5f) return;
    for (int y = (int)ceilf(y0); y <= (int)y2; y++) {
        float fy = (float)y;
        float xa = x0 + (x2 - x0) * (fy - y0) / (y2 - y0);
        float xb = (fy < y1)
            ? (y1 - y0 < 0.5f ? x1 : x0 + (x1 - x0) * (fy - y0) / (y1 - y0))
            : (y2 - y1 < 0.5f ? x1 : x1 + (x2 - x1) * (fy - y1) / (y2 - y1));
        hspan(y, (int)xa, (int)xb, c);
    }
}

// Four-corner shape (two triangles)
static void quad(float ax, float ay, float bx, float by, float cx, float cy,
                 float dx, float dy, char c) {
    tri(ax, ay, bx, by, cx, cy, c);
    tri(ax, ay, cx, cy, dx, dy, c);
}

// Thick line as a quad, any direction
static void thick(float x0, float y0, float x1, float y1, float w, char c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) { disc((int)x0, (int)y0, (int)(w / 2), c); return; }
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

static void text(int x, int y, const char *s, char fg, char bg) {
    drawTextVGA437((short)(x & ~1), (short)y, (char *)s, fg, bg);   // 8x16 font
}

// ---------------------------------------------------------------------------
// Background: sky and mountains (drawn once), the run and the hills (bg_span
// can repaint any piece of them at any time)
// ---------------------------------------------------------------------------
static float half_width_at(int y) {
    float s = (float)(y - HORIZON) / (float)(GROUND_Y - HORIZON);
    return HW_TOP + (HW_BOT - HW_TOP) * s;
}

// Corduroy lines across the run (fractions of the half-width)
static const float GROOM_U[] = { -0.66f, -0.33f, 0.0f, 0.33f, 0.66f };

// Repaint the background on row y from xa to xb (both inclusive)
static void bg_span(int y, int xa, int xb) {
    if (y <= HORIZON || y >= SCR_H) return;
    if (xa < 0) xa = 0;
    if (xb > SCR_W - 1) xb = SCR_W - 1;
    if (xb < xa) return;
    float hw = half_width_at(y);
    int xl = (int)(CX - hw), xr = (int)(CX + hw);

    // hills | run | hills
    if (xa < xl) drawHLine(xa, y, (xb < xl ? xb : xl - 1) - xa + 1, SIDE_SNOW);
    int ra = xa > xl ? xa : xl, rb = xb < xr ? xb : xr;
    if (rb >= ra) drawHLine(ra, y, rb - ra + 1, RUN_SNOW);
    if (xb > xr) { int s = xa > xr ? xa : xr + 1; drawHLine(s, y, xb - s + 1, SIDE_SNOW); }

    // crisp edges of the run, and corduroy on alternate rows
    if (xl >= xa && xl <= xb) drawPixel((short)xl, (short)y, BLUE);
    if (xr >= xa && xr <= xb) drawPixel((short)xr, (short)y, BLUE);
    if (y & 1) {
        for (unsigned i = 0; i < sizeof GROOM_U / sizeof GROOM_U[0]; i++) {
            int gx = (int)(CX + GROOM_U[i] * hw);
            if (gx >= xa && gx <= xb) drawPixel((short)gx, (short)y, GROOM);
        }
    }
}

static void erase_box(const box_t *b) {
    if (b->x1 < b->x0) return;
    for (int y = b->y0; y <= b->y1; y++) bg_span(y, b->x0, b->x1);
}

static void draw_background(void) {
    clip_top = 0;
    box_begin();
    rect(0, 0, SCR_W, HUD_H, HUD_BG);
    rect(0, HUD_H, SCR_W, HORIZON - HUD_H + 1, SKY);

    // far mountain range: (peak x, peak y, half width, colour)
    static const short peaks[][4] = {
        {  40, 104, 150, BLUE }, { 205, 84, 170, DARK_BLUE }, { 380, 110, 140, BLUE },
        { 520,  88, 165, DARK_BLUE }, { 660, 112, 130, BLUE },
    };
    for (unsigned i = 0; i < sizeof peaks / sizeof peaks[0]; i++) {
        float px = peaks[i][0], py = peaks[i][1], hw = peaks[i][2];
        tri(px, py, px - hw, HORIZON, px + hw, HORIZON, (char)peaks[i][3]);
        float k = 0.30f;                                         // snow cap
        tri(px, py, px - hw * k, py + (HORIZON - py) * k, px + hw * k, py + (HORIZON - py) * k, WHITE);
    }
    for (int y = HORIZON + 1; y < SCR_H; y++) bg_span(y, 0, SCR_W - 1);
}

// ---------------------------------------------------------------------------
// Objects on the run
// ---------------------------------------------------------------------------
static bool project(fix15 xw, fix15 zw, float *sx, float *sy, float *k) {
    float z = fix2float15(zw);
    if (z < -0.9f) return false;
    float s = 1.0f / (z / Z_SCALE + 1.0f);
    *sy = HORIZON + (GROUND_Y - HORIZON) * s;
    float hw = HW_TOP + (HW_BOT - HW_TOP) * s;
    *sx = CX + fix2float15(xw) * hw;
    *k = hw / HW_BOT;                 // size factor: 1 at the rider
    return *sy < SCR_H + 60;
}

static float maxf(float a, float b) { return a > b ? a : b; }

// Round sign on a black pole, white arrow pointing dir (-1 left, +1 right)
static void sign_post(float sx, float sy, float k, char face, int dir) {
    float ph = 120 * k, pw = maxf(2, 6 * k), r = maxf(3, 22 * k);
    rect((int)(sx - pw / 2), (int)(sy - ph), (int)pw, (int)ph, BLACK);
    float cy = sy - ph - r * 0.6f;
    disc((int)sx, (int)cy, (int)r + 1, BLACK);                    // rim
    disc((int)sx, (int)cy, (int)r, face);
    if (r >= 6) {                                                  // arrow
        float L = r * 0.62f, t = r * 0.20f, hd = r * 0.40f;
        rect((int)(sx - L * 0.5f), (int)(cy - t), (int)(L * 0.75f), (int)maxf(1, 2 * t), WHITE);
        if (dir > 0) tri(sx + L * 0.2f, cy - hd, sx + L * 0.2f, cy + hd, sx + L * 0.75f, cy, WHITE);
        else         tri(sx - L * 0.2f, cy - hd, sx - L * 0.2f, cy + hd, sx - L * 0.75f, cy, WHITE);
        if (dir < 0) rect((int)(sx - L * 0.25f), (int)(cy - t), (int)(L * 0.75f), (int)maxf(1, 2 * t), WHITE);
    }
}

static void draw_obj(const game_obj_t *o) {
    float sx, sy, k;
    switch (o->kind) {
    case OBJ_DOT:                      // snow mark streaming past
        if (!project(o->x, o->z, &sx, &sy, &k)) return;
        rect((int)sx, (int)sy, (int)maxf(1, 10 * k), (int)maxf(1, 2 * k), LIGHT_BLUE);
        break;

    case OBJ_POLE: {                   // orange marker on the edge of the run
        if (!project(o->x, o->z, &sx, &sy, &k)) return;
        int h = (int)maxf(2, 46 * k), w = (int)maxf(1, 5 * k);
        rect((int)sx - w / 2, (int)sy - h, w, h, ORANGE);
        rect((int)sx - w / 2, (int)sy - h, w, (int)maxf(1, h / 4), RED);
        break;
    }

    case OBJ_TREE: {
        if (!project(o->x, o->z, &sx, &sy, &k)) return;
        float th = maxf(1, 18 * k), tw = maxf(2, 10 * k);
        float H = 130 * k, W = 70 * k;
        rect((int)(sx - tw / 2), (int)(sy - th), (int)tw, (int)th, DARK_ORANGE);
        char leaf = o->variant ? MED_GREEN : DARK_GREEN;
        tri(sx - W / 2, sy - th, sx + W / 2, sy - th, sx, sy - th - H * 0.75f, leaf);
        tri(sx - W * 0.38f, sy - th - H * 0.35f, sx + W * 0.38f, sy - th - H * 0.35f,
            sx, sy - th - H, leaf);
        if (k > 0.35f) {               // snow on the branches
            tri(sx - W * 0.12f, sy - th - H * 0.82f, sx + W * 0.12f, sy - th - H * 0.82f,
                sx, sy - th - H, WHITE);
        }
        break;
    }

    case OBJ_GATE: {                   // pass between the two signs
        char face = o->variant ? GREEN : BLUE;
        float lx, ly, lk, rx, ry, rk;
        if (!project(o->x - o->half_w, o->z, &lx, &ly, &lk)) return;
        if (!project(o->x + o->half_w, o->z, &rx, &ry, &rk)) return;
        sign_post(lx, ly, lk, face, +1);     // left sign points right, into the gate
        sign_post(rx, ry, rk, face, -1);     // right sign points left
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// The snowboarder, seen from behind
// ---------------------------------------------------------------------------
static int rider_x(void) {
    return CX + (int)(fix2float15(f.game.skier_x) * HW_BOT);
}

static void draw_rider(int bx, float lean, float heading_deg, bool spray, uint32_t frame) {
    // lean: -1 (heel edge) .. +1 (toe edge). The body tips into the turn.
    // heading: board direction (0 = pointing down the hill, away from the
    // camera; +-90 = across the slope). Seen from behind and above, a board
    // pointing downhill looks short and upright, one across the slope looks
    // long and flat.
    float L = lean * 26.0f;
    float by = GROUND_Y;
    float hr = heading_deg * 0.0174533f;
    float ux = sinf(hr), uy = -cosf(hr) * 0.40f;     // board direction on screen

    // snow spray thrown off the edge in hard, fast turns
    if (spray) {
        int side = lean > 0 ? -1 : 1;
        for (int i = 0; i < 18; i++) {
            uint32_t h = (frame * 2654435761u) ^ (uint32_t)(i * 40503u);
            int px = bx + side * (30 + (int)(h % 46u));
            int py = (int)by - 6 - (int)((h >> 8) % 34u);
            rect(px, py, 3, 2, i & 1 ? WHITE : LIGHT_BLUE);
        }
    }

    // board: dark plank with a red top sheet, along the heading
    float hl = 64.0f;
    thick(bx - hl * ux, by - hl * uy, bx + hl * ux, by + hl * uy, 14, BLACK);
    thick(bx - hl * ux * 0.8f, by - hl * uy * 0.8f - 3, bx + hl * ux * 0.8f, by + hl * uy * 0.8f - 3,
          3, RED);
    disc((int)(bx + hl * ux), (int)(by + hl * uy - 2), 4, YELLOW);   // nose marker

    // feet sit on the board's bindings, stance across the board
    float fl = 22.0f;
    float lfx = bx - fl * ux - 6, lfy = by - fl * uy - 6;
    float rfx = bx + fl * ux + 6, rfy = by + fl * uy - 6;
    if (ux < 0) { float t = lfx; lfx = rfx; rfx = t; t = lfy; lfy = rfy; rfy = t; }
    if (rfx - lfx < 16) { float m = (lfx + rfx) * 0.5f; lfx = m - 8; rfx = m + 8; }

    // legs (dark trousers), knees bent
    float hipx = bx + L * 0.5f, hipy = by - 66;
    float lkx = (lfx + hipx - 11) * 0.5f - 4 + L * 0.3f, lky = (lfy + hipy) * 0.5f;
    float rkx = (rfx + hipx + 11) * 0.5f + 4 + L * 0.3f, rky = (rfy + hipy) * 0.5f;
    thick(lfx, lfy, lkx, lky, 15, DARK_BLUE);                              // shins
    thick(rfx, rfy, rkx, rky, 15, DARK_BLUE);
    thick(lkx, lky, hipx - 11, hipy, 17, DARK_BLUE);                       // thighs
    thick(rkx, rky, hipx + 11, hipy, 17, DARK_BLUE);
    disc((int)lfx, (int)lfy + 2, 6, BLACK);                                // boots
    disc((int)rfx, (int)rfy + 2, 6, BLACK);

    // jacket
    float shx = bx + L, shy = by - 122;
    quad(hipx - 22, hipy + 4, hipx + 22, hipy + 4, shx + 28, shy, shx - 28, shy, ORANGE);
    thick(shx - 6, shy + 10, hipx - 6, hipy - 6, 5, WHITE);               // logo stripe
    thick(shx + 2, shy + 14, hipx + 4, hipy - 2, 3, DARK_ORANGE);

    // arms out for balance, black gloves
    thick(shx - 24, shy + 6, shx - 52 + L * 0.2f, shy + 40 - L * 0.6f, 11, ORANGE);
    thick(shx + 24, shy + 6, shx + 52 + L * 0.2f, shy + 40 + L * 0.6f, 11, ORANGE);
    disc((int)(shx - 54 + L * 0.2f), (int)(shy + 42 - L * 0.6f), 6, BLACK);
    disc((int)(shx + 54 + L * 0.2f), (int)(shy + 42 + L * 0.6f), 6, BLACK);

    // helmet with a dark band
    float hx = shx + L * 0.25f, hy = shy - 18;
    disc((int)hx, (int)hy + 4, 9, BLACK);                                 // neck
    disc((int)hx, (int)hy - 6, 19, YELLOW);
    rect((int)hx - 18, (int)hy - 2, 36, 5, DARK_ORANGE);
}

// ---------------------------------------------------------------------------
// HUD and messages
// ---------------------------------------------------------------------------
static void draw_hud(void) {
    clip_top = 0;
    const game_view_t *g = &f.game;
    char line[96];
    snprintf(line, sizeof line,
             "SCORE %4ld  BEST %4ld  GATES %2u/%-2u  MISSED %2u  %3d km/h  %5.1f s   ",
             (long)g->score, (long)g->best, g->gates_passed, SKI_RUN_GATES,
             g->gates_missed, (int)(fix2float15(g->speed) * 3.6f),
             (double)(g->run_ms / 1000.0f));
    text(8, 4, line, WHITE, HUD_BG);

    float tilt = fix2float15(f.angle) - fix2float15(f.zero);
    if (f.keyboard || !f.live) {
        snprintf(line, sizeof line, "%-26s", f.weave ? "STEER: AUTO-WEAVE" :
                 (f.ever ? (f.live ? "STEER: KEYS (k = board)" : "BOARD LOST - KEYS")
                         : "NO BOARD - KEYS a/d/w"));
    } else {
        snprintf(line, sizeof line, "BOARD LIVE  tilt %+5.1f   ", (double)tilt);
    }
    text(8, 22, line, f.live && !f.keyboard ? GREEN : YELLOW, HUD_BG);

    char msg[32];
    char col = WHITE;
    if (g->state == GS_RUN && g->last_event && g->run_ms - g->last_event_ms < 700 &&
        (g->last_event == GAME_EV_GATE || g->last_event == GAME_EV_MISS)) {
        if (g->last_event == GAME_EV_GATE) { snprintf(msg, sizeof msg, "%-20s", "+10 GATE!"); col = GREEN; }
        else                               { snprintf(msg, sizeof msg, "%-20s", "MISSED!");   col = RED; }
    } else {
        int hd = (int)lroundf(fix2float15(g->heading));
        float e = fix2float15(g->edge);
        const char *edge = e > 0.15f ? "TOE " : (e < -0.15f ? "HEEL" : "FLAT");
        const char *dir = hd > 2 ? "R" : (hd < -2 ? "L" : " ");
        snprintf(msg, sizeof msg, "BOARD %3d%s  %s     ", hd < 0 ? -hd : hd, dir, edge);
    }
    text(264, 22, msg, col, HUD_BG);

    int bx = 470, bw = 160, by = 24;            // steer bar
    rect(bx, by, bw, 10, BLACK);
    rect(bx + bw / 2, by - 2, 1, 14, WHITE);
    int nx = bx + bw / 2 + (int)(fix2float15(g->steer) * (bw / 2 - 3));
    rect(nx - 2, by, 5, 10, YELLOW);
}

// Top-down trail map in the sky: the run seen from above, with the path of
// the last 40 or so metres, so the S-turns show up clearly.
#define MAP_X     526
#define MAP_Y     48
#define MAP_W     104
#define MAP_H     112
#define TRAIL_N   56            // samples, one every TRAIL_STEP metres
#define TRAIL_STEP 0.75f
static float trail[TRAIL_N];
static int   trail_n;
static float trail_next;

static void trail_reset(void) { trail_n = 0; trail_next = 0; }

static void trail_update(void) {
    float d = fix2float15(f.game.dist);
    if (f.game.state != GS_RUN) return;
    if (d < trail_next - 1.0f) trail_reset();          // new run
    while (d >= trail_next) {
        memmove(&trail[1], &trail[0], sizeof trail[0] * (TRAIL_N - 1));
        trail[0] = fix2float15(f.game.skier_x);
        if (trail_n < TRAIL_N) trail_n++;
        trail_next += TRAIL_STEP;
    }
}

static void draw_map(void) {
    clip_top = 0;
    rect(MAP_X - 2, MAP_Y - 2, MAP_W + 4, MAP_H + 4, DARK_BLUE);
    rect(MAP_X, MAP_Y, MAP_W, MAP_H, WHITE);
    int mx = MAP_X + MAP_W / 2, half = MAP_W / 2 - 6;
    rect(MAP_X + 5, MAP_Y, 1, MAP_H, BLUE);                // run edges
    rect(MAP_X + MAP_W - 6, MAP_Y, 1, MAP_H, BLUE);
    int ry = MAP_Y + MAP_H - 14;                           // rider row (downhill = down)
    for (int i = 1; i < trail_n; i++) {
        int y0 = ry - (i - 1) * 2, y1 = ry - i * 2;
        if (y1 < MAP_Y) break;
        int x0 = mx + (int)(trail[i - 1] * half), x1 = mx + (int)(trail[i] * half);
        thick((float)x0, (float)y0, (float)x1, (float)y1, 2, BLUE);
    }
    int rx = mx + (int)(fix2float15(f.game.skier_x) * half);
    float hr = fix2float15(f.game.heading) * 0.0174533f;
    float dx = sinf(hr) * 9.0f, dy = cosf(hr) * 9.0f;     // board, nose = yellow
    thick(rx - dx, ry - dy, rx + dx, ry + dy, 4, BLACK);
    disc((int)(rx + dx), (int)(ry + dy), 2, ORANGE);
}

static void draw_overlay(void) {
    clip_top = 0;
    const game_view_t *g = &f.game;
    char line[64];
    if (g->state == GS_MENU) {
        drawTextGrotesk32(272, 196, "SKISIM", DARK_BLUE, RUN_SNOW);
        text(224, 238, "Stand level on the board", DARK_BLUE, RUN_SNOW);
        text(208, 258, "Lean RIGHT and hold to start", DARK_BLUE, RUN_SNOW);
        text(192, 278, "(or type g in the serial monitor)", BLUE, RUN_SNOW);
    } else if (g->state == GS_RESULT) {
        drawTextGrotesk32(224, 196, "RUN COMPLETE", DARK_BLUE, RUN_SNOW);
        snprintf(line, sizeof line, "  Score %ld    Best %ld  ", (long)g->score, (long)g->best);
        text(320 - (int)strlen(line) * 4, 238, line, DARK_BLUE, RUN_SNOW);
        snprintf(line, sizeof line, "  Gates %u of %u  ", g->gates_passed, SKI_RUN_GATES);
        text(320 - (int)strlen(line) * 4, 258, line, DARK_BLUE, RUN_SNOW);
        int left = SKI_RESULT_SECONDS - (int)(g->state_ms / 1000);
        if (left < 0) left = 0;
        snprintf(line, sizeof line, "  Back to the start in %d s  ", left);
        text(320 - (int)strlen(line) * 4, 278, line, BLUE, RUN_SNOW);
    }
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------
static box_t prev_box[GAME_MAX_OBJ];
static int   n_prev;
static box_t rider_box;

static void erase_objects(void) {
    clip_top = HORIZON + 1;
    for (int i = 0; i < n_prev; i++) erase_box(&prev_box[i]);
    n_prev = 0;
}

static void draw_objects(void) {
    uint8_t order[GAME_MAX_OBJ];
    int n = f.game.n_obj;
    for (int i = 0; i < n; i++) order[i] = (uint8_t)i;
    for (int i = 1; i < n; i++) {               // far to near: insertion sort by z
        uint8_t k = order[i];
        int j = i - 1;
        while (j >= 0 && f.game.obj[order[j]].z < f.game.obj[k].z) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }
    for (int i = 0; i < n; i++) {
        clip_top = HORIZON + 1;
        box_begin();
        draw_obj(&f.game.obj[order[i]]);
        if (cur_box.x1 >= cur_box.x0) prev_box[n_prev++] = cur_box;
    }
}

// One frame: erase what moved, draw it again. Kept separate from the loop so
// the PC test harness can run exactly the same code.
static int  last_state = -1;
static int  last_x = -10000, last_lean = 1000, last_head = 1000;
static bool last_spray = false;

static void render_one_frame(uint32_t frame) {
    take_snapshot();
    const game_view_t *g = &f.game;
    float lean_f = fix2float15(g->steer);
    int lean = (int)(lean_f * 26.0f);
    int x = rider_x();
    float head_f = fix2float15(g->heading);
    int head = (int)lroundf(head_f / 2.0f);
    bool spray = g->state == GS_RUN && fabsf(lean_f) > 0.45f && fix2float15(g->speed) > 4.0f;
    bool rider_moved = x != last_x || lean != last_lean || head != last_head ||
                       spray || last_spray;

    if ((int)g->state != last_state) {
        // new screen: repaint the whole run and draw everything fresh
        clip_top = HORIZON + 1;
        for (int y = HORIZON + 1; y < SCR_H; y++) bg_span(y, 0, SCR_W - 1);
        n_prev = 0;
        rider_box.x1 = -1;
        draw_objects();
        rider_moved = true;
        if (g->state == GS_RUN) trail_reset();
        last_state = (int)g->state;
    } else {
        if (rider_moved) erase_box(&rider_box);
        // objects move every frame during a run; on the other screens
        // they only need repainting after the rider's old pose is erased
        if (g->state == GS_RUN || rider_moved) {
            erase_objects();
            draw_objects();
        }
    }

    clip_top = HORIZON + 1;
    box_begin();
    draw_rider(x, lean_f, head_f, spray, frame);
    rider_box = cur_box;
    last_x = x;
    last_lean = lean;
    last_head = head;
    last_spray = spray;

    draw_hud();
    trail_update();
    if (g->state == GS_RUN || (frame & 15) == 0) draw_map();
    draw_overlay();
}

void render_core1_entry(void) {
    audio_init_core1();          // the audio interrupt lives on this core
    draw_background();

    uint32_t frame = 0;
    absolute_time_t next = get_absolute_time();
    while (true) {
        render_one_frame(frame++);
        next = delayed_by_ms(next, FRAME_MS);
        sleep_until(next);
    }
}
