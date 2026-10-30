// The radar screen: centred on the receiver, north up, a map, range rings, aircraft as arrows with
// trails and labels, and zoom buttons. ui.c puts it on screen, with the header and details strip.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feed.h"
#include "palette.h"

// The station's position, from the Pi's receiver.json. Until it's set, aircraft are placed by
// their range and bearing (r_dst, r_dir) and get no trails, and there's no map.
void radar_set_receiver(double lat, double lon);
bool radar_has_receiver(void);

// Adds each aircraft's new position to its trail. Call once per new aircraft.json.
void radar_track(const feed_t *feed, uint32_t now_ms);

// An aircraft's distance (NM) and bearing (degrees true) from the receiver; false if it has no
// position.
bool radar_range_bearing(const aircraft_t *a, float *nm, float *deg);

// Its altitude band's colour, as a palette entry and as a text colour.
uint8_t radar_alt_colour(const aircraft_t *a);
text_colour_t radar_alt_text(const aircraft_t *a);

// A screen-sized buffer (in PSRAM) to keep the map and rings in, redrawn only when the range
// changes. Without one radar_draw draws them every time.
void radar_set_base_layer(uint8_t *base_layer);

// Draws the radar into `target` (a screen-sized buffer). `selected` is the hex of the aircraft
// to ring ("" for none); `bottom` is where the free area ends (a details strip below it), and
// the range label and zoom buttons sit just above it.
void radar_draw(uint8_t *target, const feed_t *feed, const char *selected, int bottom);

// The feed index of the aircraft drawn nearest (x, y), within reach of a finger; -1 if none.
int radar_pick(int x, int y);

// A tap at (x, y) on a zoom button, or the centre button (shown when panned)? `changed` says
// whether that changed the view.
bool radar_button(int x, int y, bool *changed);

// Drags the map by (dx, dy) pixels, keeping the view's centre within the range of the receiver.
// Returns true if it moved.
bool radar_pan(int dx, int dy);
bool radar_panned(void);

int radar_range_nm(void);

// The last time the map was drawn: how long it took and the line segments it drew.
void radar_map_stats(uint32_t *us, uint32_t *segments);
