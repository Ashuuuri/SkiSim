// Player input other than the board tilt. For now: test keys typed into the
// serial monitor (USB or UART). The rotary encoder (PROJECT.md 3.2) joins here
// later.
//
//   g or space   start a run
//   a / d        steer left / right (each press adds 0.5, so "dd" = full right)
//   s            steer straight
//   w            toggle auto-weave (the skier steers itself in S-turns)
//   k            toggle keyboard steering even while the board unit is live
//   c            take the current board angle as "level"
//   t            sound test: play the gate chime (checks the DAC wiring)
//
// Most serial monitors send what you typed when you press Enter.
#ifndef SKISIM_INPUT_H
#define SKISIM_INPUT_H

#include <stdbool.h>
#include "fix15.h"

typedef struct {
    bool  start;        // start pressed since the last poll
    bool  rezero;       // 'c' pressed since the last poll
    bool  test_sound;   // 't' pressed since the last poll
    bool  keyboard;     // steer from keys/auto-weave instead of the board
    bool  weave;        // auto-weave on
    fix15 key_steer;    // -1..+1 from a/d/s
} input_state_t;

void input_init(void);

// Read any waiting characters (never blocks) and update *st.
void input_poll(input_state_t *st);

#endif // SKISIM_INPUT_H
