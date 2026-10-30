// The screens and touch: the radar (radar.c) with a details strip for the selected aircraft, a
// nearest-first list, and a status screen. Swipe left and right between them; tap an aircraft (on
// the radar or in the list) to select it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feed.h"
#include "drivers/ft6236.h"

typedef struct {
    bool wifi_up;
    const char *wifi_state;     // net_state()
    const char *ip;
    int rssi;
    int32_t data_age_ms;        // since the last good aircraft.json; -1 before the first
    float messages_per_s;       // the receiver's decoded messages a second
    float fps;
    uint32_t psram_kb, psram_errors;
    uint32_t uptime_s;
    const char *pi_host;
} ui_status_t;

// `screen` is the panel's framebuffer. `base_layer` and `back_buffer`, screen-sized and in PSRAM,
// let frames be composed off screen (and the radar keep its map); without them (NULL) everything
// is drawn straight onto the screen.
void ui_init(uint8_t *screen, uint8_t *base_layer, uint8_t *back_buffer);

// Feeds it the touch controller's state. Returns true if what's shown has changed.
bool ui_touch(const touch_state_t *touch, const feed_t *feed, uint32_t now_ms);

// Keeps the selection's last known details. Call once per new aircraft.json.
void ui_update(const feed_t *feed, uint32_t now_ms);

// Draws the current screen. Returns where: the back buffer (copy it to the screen at vsync) or
// the screen itself.
uint8_t *ui_draw(const feed_t *feed, const ui_status_t *status, uint32_t now_ms);
