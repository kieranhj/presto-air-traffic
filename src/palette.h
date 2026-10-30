// The 256-entry palette: named colours, then a ramp of 15 shades for each text colour (font
// coverage 1-15, from its background to the colour) so text is anti-aliased in 8-bit colour.
#pragma once

#include <stdint.h>

enum {
    C_BG, C_RING, C_AXIS, C_HOUSE, C_PANEL, C_PANEL_EDGE, C_TOUCH,
    C_OK, C_WAIT, C_BAD,
    // aircraft by altitude band, and their trails (dimmer)
    C_GROUND, C_ALT_LOW, C_ALT_MID, C_ALT_HIGH, C_ALT_TOP,
    C_TRAIL_GROUND, C_TRAIL_LOW, C_TRAIL_MID, C_TRAIL_HIGH, C_TRAIL_TOP,
    // the background map, kept dim so aircraft stand out
    C_MAP_COAST, C_MAP_MOTORWAY, C_MAP_TRUNK, C_MAP_PRIMARY, C_MAP_RUNWAY, C_MAP_TOWN,
    C_RAMPS = 32,
};

// Text colours. Each is drawn over the background named in palette.c (the radar's, or a button's).
typedef enum {
    T_WHITE, T_GREY, T_RING,
    T_GROUND, T_ALT_LOW, T_ALT_MID, T_ALT_HIGH, T_ALT_TOP,
    T_OK, T_WAIT, T_BAD,
    T_ON_PANEL,
    T_MAP,
    T_COUNT,
} text_colour_t;

#define TEXT_RAMP(t) ((uint8_t)(C_RAMPS + (t) * 15))

// Writes every entry. Call once the panel is up.
void palette_init(void);
