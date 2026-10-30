#include "palette.h"

#include "drivers/st7701.h"

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static const rgb_t BG = {0x06, 0x10, 0x1a};
static const rgb_t PANEL = {0x14, 0x24, 0x33};

static const rgb_t named[C_RAMPS] = {
    [C_BG] = {0x06, 0x10, 0x1a},
    [C_RING] = {0x1f, 0x4d, 0x3a},
    [C_AXIS] = {0x12, 0x2b, 0x24},
    [C_HOUSE] = {0xff, 0xff, 0xff},
    [C_PANEL] = {0x14, 0x24, 0x33},
    [C_PANEL_EDGE] = {0x2c, 0x45, 0x5c},
    [C_TOUCH] = {0x00, 0xe5, 0xff},
    [C_OK] = {0x2e, 0xcc, 0x71},
    [C_WAIT] = {0xf1, 0xc4, 0x0f},
    [C_BAD] = {0xe7, 0x4c, 0x3c},
    [C_GROUND] = {0x8a, 0x96, 0x98},
    [C_ALT_LOW] = {0xff, 0x6b, 0x3d},     // < 3,000 ft
    [C_ALT_MID] = {0xff, 0xc9, 0x3c},     // 3,000-10,000 ft
    [C_ALT_HIGH] = {0x6b, 0xd6, 0x6b},    // 10,000-20,000 ft
    [C_ALT_TOP] = {0x5d, 0xad, 0xe2},     // > 20,000 ft
    [C_TRAIL_GROUND] = {0x3a, 0x42, 0x46},
    [C_TRAIL_LOW] = {0x73, 0x37, 0x26},
    [C_TRAIL_MID] = {0x73, 0x5d, 0x26},
    [C_TRAIL_HIGH] = {0x33, 0x63, 0x38},
    [C_TRAIL_TOP] = {0x2c, 0x51, 0x6c},
    [C_MAP_COAST] = {0x24, 0x4f, 0x73},
    [C_MAP_MOTORWAY] = {0x3a, 0x4c, 0x63},
    [C_MAP_TRUNK] = {0x2b, 0x3b, 0x4e},
    [C_MAP_PRIMARY] = {0x1d, 0x2b, 0x3a},
    [C_MAP_RUNWAY] = {0x6a, 0x77, 0x82},
    [C_MAP_TOWN] = {0x4a, 0x5a, 0x6a},
};

static const struct {
    rgb_t fg;
    const rgb_t *bg;
} text[T_COUNT] = {
    [T_WHITE] = {{0xf2, 0xf5, 0xf7}, &BG},
    [T_GREY] = {{0x95, 0xa3, 0xad}, &BG},
    [T_RING] = {{0x4c, 0x8c, 0x6e}, &BG},
    [T_GROUND] = {{0x8a, 0x96, 0x98}, &BG},
    [T_ALT_LOW] = {{0xff, 0x6b, 0x3d}, &BG},
    [T_ALT_MID] = {{0xff, 0xc9, 0x3c}, &BG},
    [T_ALT_HIGH] = {{0x6b, 0xd6, 0x6b}, &BG},
    [T_ALT_TOP] = {{0x5d, 0xad, 0xe2}, &BG},
    [T_OK] = {{0x2e, 0xcc, 0x71}, &BG},
    [T_WAIT] = {{0xf1, 0xc4, 0x0f}, &BG},
    [T_BAD] = {{0xe7, 0x4c, 0x3c}, &BG},
    [T_ON_PANEL] = {{0xf2, 0xf5, 0xf7}, &PANEL},
    [T_MAP] = {{0x6b, 0x7d, 0x8e}, &BG},
};

static uint8_t mix(uint8_t a, uint8_t b, int num, int den) {
    return (uint8_t)(a + (b - a) * num / den);
}

void palette_init(void) {
    for (int i = 0; i < C_RAMPS; i++) st7701_set_palette(i, named[i].r, named[i].g, named[i].b);
    for (int t = 0; t < T_COUNT; t++) {
        const rgb_t *bg = text[t].bg, fg = text[t].fg;
        for (int c = 1; c <= 15; c++)
            st7701_set_palette(TEXT_RAMP(t) + c - 1, mix(bg->r, fg.r, c, 15), mix(bg->g, fg.g, c, 15),
                               mix(bg->b, fg.b, c, 15));
    }
}
