#include "input.h"

#include <stdio.h>
#include "pico/stdlib.h"

void input_init(void) {
}

void input_poll(input_state_t *st) {
    st->start = false;
    st->rezero = false;
    st->test_sound = false;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        switch (c) {
        case 'g': case 'G': case ' ':
            st->start = true;
            break;
        case 'a': case 'A':
            st->key_steer -= float2fix15(0.5f);
            if (st->key_steer < -int2fix15(1)) st->key_steer = -int2fix15(1);
            st->weave = false;
            break;
        case 'd': case 'D':
            st->key_steer += float2fix15(0.5f);
            if (st->key_steer > int2fix15(1)) st->key_steer = int2fix15(1);
            st->weave = false;
            break;
        case 's': case 'S':
            st->key_steer = 0;
            st->weave = false;
            break;
        case 'w': case 'W':
            st->weave = !st->weave;
            printf("[input] auto-weave %s\n", st->weave ? "on" : "off");
            break;
        case 'k': case 'K':
            st->keyboard = !st->keyboard;
            printf("[input] steering from %s\n", st->keyboard ? "keys" : "board");
            break;
        case 'c': case 'C':
            st->rezero = true;
            break;
        case 't': case 'T':
            st->test_sound = true;
            break;
        default:
            break;   // ignore Enter, newlines, and anything else
        }
    }
}
