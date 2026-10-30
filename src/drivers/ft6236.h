// The Presto's FT6236 capacitive touch controller: up to two touches, in panel pixels.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool down;
    uint16_t x, y;      // 0-479
} touch_point_t;

typedef struct {
    touch_point_t p[2];
    int count;          // touches down
} touch_state_t;

void ft6236_init(void);

// Reads the controller if it has something new (its interrupt line is low) or a touch is still
// down. Returns false if nothing could be read; `state` then keeps its last value.
bool ft6236_poll(touch_state_t *state);
