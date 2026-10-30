#include "radar.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/time.h"

#include "gfx.h"
#include "map.h"
#include "palette.h"

#define CX 240
#define CY 240
#define RADIUS 228              // pixels to the outer ring

#define TRAIL_POINTS 24
#define TRAIL_GAP_MS 5000       // a trail point every 5 s: two minutes of track
#define TRAIL_FORGET_MS 180000  // a trail is dropped 3 minutes after its aircraft was last seen
#define TRAIL_UNIT 32           // trail points are stored in 1/32 NM
#define FRESH_POS_S 3.0f        // a position this new extends the trail
#define SHOW_POS_S 60.0f        // older positions aren't drawn

static const struct {
    int range_nm, ring_nm;
} ranges[] = {{5, 1}, {10, 2}, {20, 5}, {40, 10}};
#define RANGES ((int)(sizeof ranges / sizeof ranges[0]))
static int range_i = 2;

typedef struct {
    char hex[8];
    uint32_t last_point_ms, last_seen_ms;
    uint8_t n, head;            // points held, and where the next goes
    int16_t x[TRAIL_POINTS], y[TRAIL_POINTS];
} trail_t;

static trail_t trails[FEED_MAX_AIRCRAFT];

static bool have_receiver;
static double rx_lat, rx_lon, rx_coslat;
static float rx_map_e, rx_map_n;    // the receiver in NM east and north of the map's origin

// -- zoom buttons, which sit just above `bottom` (radar_draw's)
typedef struct {
    int x, y, w, h;
} rect_t;
static rect_t btn_out = {356, 416, 52, 52}, btn_in = {418, 416, 52, 52};

void radar_set_receiver(double lat, double lon) {
    rx_lat = lat;
    rx_lon = lon;
    rx_coslat = cos(lat * M_PI / 180);
    rx_map_e = (float)((lon - map.origin_lon) * 60 * cos(map.origin_lat * M_PI / 180));
    rx_map_n = (float)((lat - map.origin_lat) * 60);
    have_receiver = true;
}

bool radar_has_receiver(void) {
    return have_receiver;
}

int radar_range_nm(void) {
    return ranges[range_i].range_nm;
}

// An aircraft's position in NM east and north of the receiver
static bool position_nm(const aircraft_t *a, float *e, float *n) {
    if (!a->has_pos) return false;
    if (have_receiver) {
        *e = (float)((a->lon - rx_lon) * 60 * rx_coslat);
        *n = (float)((a->lat - rx_lat) * 60);
    } else {
        float d = a->r_dir * (float)M_PI / 180;
        *e = a->r_dst * sinf(d);
        *n = a->r_dst * cosf(d);
    }
    return true;
}

static trail_t *trail_for(const char *hex, uint32_t now_ms, bool create) {
    trail_t *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < FEED_MAX_AIRCRAFT; i++) {
        trail_t *t = &trails[i];
        if (!t->hex[0]) {
            if (!free_slot) free_slot = t;
            continue;
        }
        if (!strcmp(t->hex, hex)) return t;
        if (!oldest || t->last_seen_ms < oldest->last_seen_ms) oldest = t;
    }
    if (!create) return NULL;
    trail_t *t = free_slot ? free_slot : oldest;
    memset(t, 0, sizeof *t);
    strncpy(t->hex, hex, sizeof t->hex - 1);
    t->last_seen_ms = now_ms;
    return t;
}

void radar_track(const feed_t *feed, uint32_t now_ms) {
    for (int i = 0; i < FEED_MAX_AIRCRAFT; i++)
        if (trails[i].hex[0] && now_ms - trails[i].last_seen_ms > TRAIL_FORGET_MS) trails[i].hex[0] = 0;
    if (!have_receiver) return;

    for (int i = 0; i < feed->count; i++) {
        const aircraft_t *a = &feed->aircraft[i];
        float e, n;
        if (!a->hex[0] || a->seen_pos > FRESH_POS_S || !position_nm(a, &e, &n)) continue;
        trail_t *t = trail_for(a->hex, now_ms, true);
        t->last_seen_ms = now_ms;
        if (t->n && now_ms - t->last_point_ms < TRAIL_GAP_MS) continue;
        if (fabsf(e) > 1000 || fabsf(n) > 1000) continue;
        t->x[t->head] = (int16_t)lroundf(e * TRAIL_UNIT);
        t->y[t->head] = (int16_t)lroundf(n * TRAIL_UNIT);
        t->head = (t->head + 1) % TRAIL_POINTS;
        if (t->n < TRAIL_POINTS) t->n++;
        t->last_point_ms = now_ms;
    }
}

// -- drawing

bool radar_range_bearing(const aircraft_t *a, float *nm, float *deg) {
    float e, n;
    if (!position_nm(a, &e, &n)) return false;
    *nm = sqrtf(e * e + n * n);
    float b = atan2f(e, n) * 180 / (float)M_PI;
    *deg = b < 0 ? b + 360 : b;
    return true;
}

static int band(const aircraft_t *a) {
    if (!a->has_alt || a->on_ground) return 0;
    if (a->alt_baro < 3000) return 1;
    if (a->alt_baro < 10000) return 2;
    if (a->alt_baro < 20000) return 3;
    return 4;
}

static const uint8_t band_colour[] = {C_GROUND, C_ALT_LOW, C_ALT_MID, C_ALT_HIGH, C_ALT_TOP};
static const uint8_t band_trail[] = {C_TRAIL_GROUND, C_TRAIL_LOW, C_TRAIL_MID, C_TRAIL_HIGH, C_TRAIL_TOP};
static const text_colour_t band_text[] = {T_GROUND, T_ALT_LOW, T_ALT_MID, T_ALT_HIGH, T_ALT_TOP};

uint8_t radar_alt_colour(const aircraft_t *a) {
    return band_colour[band(a)];
}

text_colour_t radar_alt_text(const aircraft_t *a) {
    return band_text[band(a)];
}

static void to_screen(float e, float n, float px_per_nm, int *x, int *y) {
    *x = CX + (int)lroundf(e * px_per_nm);
    *y = CY - (int)lroundf(n * px_per_nm);
}

// -- the background map: needs the receiver's position to place it

static const uint8_t map_colour[] = {
    [MAP_COAST] = C_MAP_COAST, [MAP_MOTORWAY] = C_MAP_MOTORWAY, [MAP_TRUNK] = C_MAP_TRUNK,
    [MAP_PRIMARY] = C_MAP_PRIMARY, [MAP_RUNWAY] = C_MAP_RUNWAY,
};

static uint32_t map_us, map_segments;

static inline int iround(float v) {
    return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

static void draw_map(float px_per_nm) {
    if (!have_receiver) return;
    uint32_t t0 = time_us_32();
    map_segments = 0;
    int range = ranges[range_i].range_nm;
    const float k = map.unit_m / 1852.0f * px_per_nm;     // pixels per map unit
    const float ox = CX - rx_map_e * px_per_nm, oy = CY + rx_map_n * px_per_nm;    // the map's origin
    // what's on screen, in map units (with a margin for runway widths and labels)
    const float vx0 = (-8 - ox) / k, vx1 = (GFX_W + 8 - ox) / k;
    const float vy0 = (oy - GFX_H - 8) / k, vy1 = (oy + 8) / k;

    for (uint32_t i = 0; i < map.line_count; i++) {
        const map_line_t *l = &map.lines[i];
        if (l->cls == MAP_PRIMARY && range > 20) continue;
        if (l->box[2] < vx0 || l->box[0] > vx1 || l->box[3] < vy0 || l->box[1] > vy1) continue;
        const int16_t *p = &map.xy[l->start * 2];
        uint8_t c = map_colour[l->cls];
        int px = iround(ox + p[0] * k), py = iround(oy - p[1] * k);
        map_segments += l->n - 1;
        for (int j = 1; j < l->n; j++) {
            int x = iround(ox + p[2 * j] * k), y = iround(oy - p[2 * j + 1] * k);
            gfx_line(px, py, x, y, c);
            if (l->cls == MAP_RUNWAY || (l->cls == MAP_MOTORWAY && range <= 10)) {
                // thicker: one more pixel either side, across the line's main direction
                int ddx = abs(x - px) >= abs(y - py) ? 0 : 1, ddy = 1 - ddx;
                gfx_line(px + ddx, py + ddy, x + ddx, y + ddy, c);
                if (l->cls == MAP_RUNWAY) gfx_line(px - ddx, py - ddy, x - ddx, y - ddy, c);
            }
            px = x;
            py = y;
        }
    }

    // town names: the biggest at every range, more as you zoom in. The table is biggest first,
    // and a name that would overlap one already drawn is left out.
    enum { MAX_NAMES = 48, PAD = 4 };
    rect_t placed[MAX_NAMES];
    int n_placed = 0;
    int max_rank = range > 20 ? 0 : range > 10 ? 1 : 2;
    for (uint32_t i = 0; i < map.place_count && n_placed < MAX_NAMES; i++) {
        const map_place_t *pl = &map.places[i];
        if (pl->rank > max_rank) continue;
        int x = iround(ox + pl->x * k), y = iround(oy - pl->y * k);
        if (x < 0 || x >= GFX_W || y < 30 || y >= GFX_H - 30) continue;
        rect_t r = {x - 2, y - 8, gfx_text_width(&font_small, pl->name) + 9, 16};
        bool clash = false;
        for (int j = 0; j < n_placed && !clash; j++)
            clash = r.x < placed[j].x + placed[j].w + PAD && placed[j].x < r.x + r.w + PAD &&
                    r.y < placed[j].y + placed[j].h + PAD && placed[j].y < r.y + r.h + PAD;
        if (clash) continue;
        placed[n_placed++] = r;
        gfx_fill_rect(x - 1, y - 1, 3, 3, C_MAP_TOWN);
        gfx_text(&font_small, x + 5, y + 5, pl->name, TEXT_RAMP(T_MAP));
    }
    map_us = time_us_32() - t0;
}

void radar_map_stats(uint32_t *us, uint32_t *segments) {
    *us = map_us;
    *segments = map_segments;
}

static void draw_rings(float px_per_nm) {
    gfx_line(CX, CY - RADIUS, CX, CY + RADIUS, C_AXIS);
    gfx_line(CX - RADIUS, CY, CX + RADIUS, CY, C_AXIS);
    int range = ranges[range_i].range_nm, step = ranges[range_i].ring_nm;
    for (int nm = step; nm <= range; nm += step) {
        int r = (int)lroundf(nm * px_per_nm);
        gfx_circle(CX, CY, r, C_RING);
        char label[12];
        snprintf(label, sizeof label, nm == range ? "%d NM" : "%d", nm);
        // on the ring, just below the east axis
        gfx_text(&font_small, CX + r - gfx_text_width(&font_small, label) - 3, CY + 15, label,
                 TEXT_RAMP(T_RING));
    }
    gfx_fill_triangle(CX, CY - RADIUS - 9, CX - 5, CY - RADIUS + 1, CX + 5, CY - RADIUS + 1, C_RING);
}

static void draw_trail(const trail_t *t, int bx, int by, float px_per_nm, uint8_t colour) {
    int px = 0, py = 0;
    for (int k = 0; k < t->n; k++) {
        int i = (t->head - t->n + k + TRAIL_POINTS) % TRAIL_POINTS;
        int x, y;
        to_screen((float)t->x[i] / TRAIL_UNIT, (float)t->y[i] / TRAIL_UNIT, px_per_nm, &x, &y);
        if (k) gfx_line(px, py, x, y, colour);
        px = x;
        py = y;
    }
    if (t->n) gfx_line(px, py, bx, by, colour);
}

static void draw_arrow(int x, int y, const aircraft_t *a, uint8_t colour) {
    if (!a->has_track) {
        gfx_disc(x, y, 4, colour);
        return;
    }
    float t = a->track * (float)M_PI / 180, s = sinf(t), c = cosf(t);
    // a dart: the tip 11 px ahead, the tail corners 7 px back and 6 px either side, a notch behind
    int tx = x + (int)lroundf(11 * s), ty = y - (int)lroundf(11 * c);
    int lx = x + (int)lroundf(-7 * s - 6 * c), ly = y - (int)lroundf(-7 * c + 6 * s);
    int rx = x + (int)lroundf(-7 * s + 6 * c), ry = y - (int)lroundf(-7 * c - 6 * s);
    int nx = x + (int)lroundf(-3 * s), ny = y - (int)lroundf(-3 * c);
    gfx_fill_triangle(tx, ty, lx, ly, nx, ny, colour);
    gfx_fill_triangle(tx, ty, rx, ry, nx, ny, colour);
}

static void draw_label(int x, int y, const aircraft_t *a, text_colour_t alt_colour) {
    const char *name = a->flight[0] ? a->flight : a->reg[0] ? a->reg : a->hex;
    char alt[12];
    if (a->on_ground) snprintf(alt, sizeof alt, "GND");
    else if (a->has_alt) snprintf(alt, sizeof alt, "%03ld", (long)((a->alt_baro + 50) / 100));
    else snprintf(alt, sizeof alt, "---");

    int w = gfx_text_width(&font_small, name), wa = gfx_text_width(&font_small, alt) + 10;
    if (wa > w) w = wa;
    int lx = x + 13;
    if (lx + w > GFX_W - 2) lx = x - 13 - w;
    gfx_text(&font_small, lx, y - 1, name, TEXT_RAMP(T_WHITE));
    int aw = gfx_text(&font_small, lx, y + 14, alt, TEXT_RAMP(alt_colour));
    // climbing or descending: a small triangle after the altitude
    int ax = lx + aw + 5, ay = y + 9;
    uint8_t col = band_colour[band(a)];
    if (a->baro_rate >= 500) gfx_fill_triangle(ax, ay - 4, ax - 4, ay + 3, ax + 4, ay + 3, col);
    else if (a->baro_rate <= -500) gfx_fill_triangle(ax, ay + 3, ax - 4, ay - 4, ax + 4, ay - 4, col);
}

static void draw_button(const rect_t *b, bool plus) {
    gfx_fill_rect(b->x, b->y, b->w, b->h, C_PANEL);
    gfx_hline(b->x, b->x + b->w - 1, b->y, C_PANEL_EDGE);
    gfx_hline(b->x, b->x + b->w - 1, b->y + b->h - 1, C_PANEL_EDGE);
    gfx_fill_rect(b->x, b->y, 1, b->h, C_PANEL_EDGE);
    gfx_fill_rect(b->x + b->w - 1, b->y, 1, b->h, C_PANEL_EDGE);
    int cx = b->x + b->w / 2, cy = b->y + b->h / 2;
    gfx_fill_rect(cx - 11, cy - 2, 22, 4, C_HOUSE);
    if (plus) gfx_fill_rect(cx - 2, cy - 11, 4, 22, C_HOUSE);
}

// What changes only with the range: the background, the map and the rings
static void draw_base(float px_per_nm) {
    gfx_clear(C_BG);
    draw_map(px_per_nm);
    draw_rings(px_per_nm);
}

static uint8_t *base_layer;
static int base_range_i = -1;
static bool base_has_receiver;

// Where each aircraft was last drawn, for radar_pick
static int drawn_count;
static struct {
    int16_t x, y;
    uint8_t index;
} drawn[FEED_MAX_AIRCRAFT];

void radar_set_base_layer(uint8_t *base) {
    base_layer = base;
    base_range_i = -1;
}

void radar_draw(uint8_t *target, const feed_t *feed, const char *selected, int bottom) {
    const float px_per_nm = (float)RADIUS / ranges[range_i].range_nm;
    if (base_layer) {
        // the base layer only when the range or the map's placement has changed; then a copy of
        // it to draw the rest on
        if (base_range_i != range_i || base_has_receiver != have_receiver) {
            gfx_set_target(base_layer);
            draw_base(px_per_nm);
            base_range_i = range_i;
            base_has_receiver = have_receiver;
        }
        memcpy(target, base_layer, GFX_W * GFX_H);
        gfx_set_target(target);
    } else {
        gfx_set_target(target);
        draw_base(px_per_nm);
    }

    // trails first, so arrows and labels sit on top of every trail
    int sx[FEED_MAX_AIRCRAFT], sy[FEED_MAX_AIRCRAFT], sel = -1;
    bool on[FEED_MAX_AIRCRAFT];
    drawn_count = 0;
    for (int i = 0; i < feed->count; i++) {
        const aircraft_t *a = &feed->aircraft[i];
        float e, n;
        on[i] = a->seen_pos <= SHOW_POS_S && position_nm(a, &e, &n);
        if (!on[i]) continue;
        to_screen(e, n, px_per_nm, &sx[i], &sy[i]);
        if (sx[i] < -20 || sx[i] > GFX_W + 20 || sy[i] < -20 || sy[i] > GFX_H + 20) {
            on[i] = false;
            continue;
        }
        if (selected[0] && !strcmp(a->hex, selected)) sel = i;
        drawn[drawn_count].x = (int16_t)sx[i];
        drawn[drawn_count].y = (int16_t)sy[i];
        drawn[drawn_count].index = (uint8_t)i;
        drawn_count++;
        const trail_t *t = trail_for(a->hex, 0, false);
        if (t) draw_trail(t, sx[i], sy[i], px_per_nm, band_trail[band(a)]);
    }
    if (sel >= 0) {
        gfx_circle(sx[sel], sy[sel], 16, C_HOUSE);
        gfx_circle(sx[sel], sy[sel], 17, C_HOUSE);
    }
    for (int i = 0; i < feed->count; i++)
        if (on[i]) draw_arrow(sx[i], sy[i], &feed->aircraft[i], band_colour[band(&feed->aircraft[i])]);
    for (int i = 0; i < feed->count; i++)
        if (on[i]) draw_label(sx[i], sy[i], &feed->aircraft[i], band_text[band(&feed->aircraft[i])]);

    // the receiver
    gfx_fill_rect(CX - 1, CY - 6, 3, 13, C_HOUSE);
    gfx_fill_rect(CX - 6, CY - 1, 13, 3, C_HOUSE);

    // the range, the map's credit (OpenStreetMap's licence asks for it) and the zoom buttons,
    // along the bottom of what's free
    char text[12];
    snprintf(text, sizeof text, "%d NM", ranges[range_i].range_nm);
    int w = gfx_text(&font_large, 12, bottom - 14, text, TEXT_RAMP(T_WHITE));
    if (have_receiver)
        gfx_text(&font_small, 12 + w + 12, bottom - 15, "\x7f OpenStreetMap", TEXT_RAMP(T_MAP));
    btn_out.y = btn_in.y = bottom - 64;
    draw_button(&btn_out, false);
    draw_button(&btn_in, true);
}

int radar_pick(int x, int y) {
    const int reach = 28;
    int best = -1, best_d = reach * reach + 1;
    for (int i = 0; i < drawn_count; i++) {
        int dx = drawn[i].x - x, dy = drawn[i].y - y, d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = drawn[i].index;
        }
    }
    return best;
}

static bool hit(const rect_t *r, int x, int y) {
    const int slop = 8;
    return x >= r->x - slop && x < r->x + r->w + slop && y >= r->y - slop && y < r->y + r->h + slop;
}

bool radar_button(int x, int y, bool *changed) {
    *changed = false;
    if (hit(&btn_in, x, y)) {
        if (range_i > 0) {
            range_i--;
            *changed = true;
        }
        return true;
    }
    if (hit(&btn_out, x, y)) {
        if (range_i < RANGES - 1) {
            range_i++;
            *changed = true;
        }
        return true;
    }
    return false;
}
