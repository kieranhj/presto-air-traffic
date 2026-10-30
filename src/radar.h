// The radar screen: centred on the receiver, north up, range rings, aircraft as arrows with
// trails and labels, a status line, and zoom buttons.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feed.h"

typedef struct {
    bool wifi_up;
    const char *wifi_state;     // net_state()
    int32_t data_age_ms;        // since the last good aircraft.json; -1 before the first
} radar_status_t;

// The station's position, from the Pi's receiver.json. Until it's set, aircraft are placed by
// their range and bearing (r_dst, r_dir) and get no trails.
void radar_set_receiver(double lat, double lon);
bool radar_has_receiver(void);

// Adds each aircraft's new position to its trail. Call once per new aircraft.json.
void radar_track(const feed_t *feed, uint32_t now_ms);

// Two more screen-sized buffers (in PSRAM) let it keep the map in a layer that's only redrawn
// when the range changes, and compose each frame off screen. Without them it draws straight onto
// `screen`, every time.
void radar_set_buffers(uint8_t *screen, uint8_t *base_layer, uint8_t *back_buffer);

// Draws the radar. Returns where: the back buffer (copy it to the screen at vsync) or the screen.
uint8_t *radar_draw(const feed_t *feed, const radar_status_t *status);

// A touch going down at (x, y). Returns true if it changed what's drawn.
bool radar_touch(int x, int y);

int radar_range_nm(void);

// The last redraw's map: its time and the line segments it drew.
void radar_map_stats(uint32_t *us, uint32_t *segments);
