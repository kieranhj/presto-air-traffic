#include "radar.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
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

// -- zoom buttons
typedef struct {
    int x, y, w, h;
} rect_t;
static const rect_t btn_out = {356, 416, 52, 52}, btn_in = {418, 416, 52, 52};

void radar_set_receiver(double lat, double lon) {
    rx_lat = lat;
    rx_lon = lon;
    rx_coslat = cos(lat * M_PI / 180);
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

static void to_screen(float e, float n, float px_per_nm, int *x, int *y) {
    *x = CX + (int)lroundf(e * px_per_nm);
    *y = CY - (int)lroundf(n * px_per_nm);
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

static void draw_status(const radar_status_t *st, int shown) {
    char text[40];
    uint8_t dot;
    text_colour_t col;
    if (!st->wifi_up) {
        snprintf(text, sizeof text, "Wi-Fi: %s", st->wifi_state);
        dot = C_WAIT;
        col = T_WAIT;
    } else if (st->data_age_ms < 0) {
        snprintf(text, sizeof text, "Waiting for the receiver");
        dot = C_WAIT;
        col = T_WAIT;
    } else if (st->data_age_ms > 5000) {
        snprintf(text, sizeof text, "No data for %ld s", (long)(st->data_age_ms / 1000));
        dot = C_BAD;
        col = T_BAD;
    } else {
        snprintf(text, sizeof text, "Live");
        dot = C_OK;
        col = T_GREY;
    }
    gfx_disc(16, 17, 5, dot);
    gfx_text(&font_small, 28, 22, text, TEXT_RAMP(col));

    snprintf(text, sizeof text, shown == 1 ? "1 aircraft" : "%d aircraft", shown);
    gfx_text(&font_small, GFX_W - 10 - gfx_text_width(&font_small, text), 22, text, TEXT_RAMP(T_GREY));

    snprintf(text, sizeof text, "%d NM", ranges[range_i].range_nm);
    gfx_text(&font_large, 12, GFX_H - 14, text, TEXT_RAMP(T_WHITE));
}

void radar_draw(const feed_t *feed, const radar_status_t *st) {
    const float px_per_nm = (float)RADIUS / ranges[range_i].range_nm;
    gfx_clear(C_BG);
    draw_rings(px_per_nm);

    // trails first, so arrows and labels sit on top of every trail
    int shown = 0;
    int sx[FEED_MAX_AIRCRAFT], sy[FEED_MAX_AIRCRAFT];
    bool on[FEED_MAX_AIRCRAFT];
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
        shown++;
        const trail_t *t = trail_for(a->hex, 0, false);
        if (t) draw_trail(t, sx[i], sy[i], px_per_nm, band_trail[band(a)]);
    }
    for (int i = 0; i < feed->count; i++)
        if (on[i]) draw_arrow(sx[i], sy[i], &feed->aircraft[i], band_colour[band(&feed->aircraft[i])]);
    for (int i = 0; i < feed->count; i++)
        if (on[i]) draw_label(sx[i], sy[i], &feed->aircraft[i], band_text[band(&feed->aircraft[i])]);

    // the receiver
    gfx_fill_rect(CX - 1, CY - 6, 3, 13, C_HOUSE);
    gfx_fill_rect(CX - 6, CY - 1, 13, 3, C_HOUSE);

    draw_status(st, shown);
    draw_button(&btn_out, false);
    draw_button(&btn_in, true);
}

static bool hit(const rect_t *r, int x, int y) {
    const int slop = 8;
    return x >= r->x - slop && x < r->x + r->w + slop && y >= r->y - slop && y < r->y + r->h + slop;
}

bool radar_touch(int x, int y) {
    if (hit(&btn_in, x, y) && range_i > 0) {
        range_i--;
        return true;
    }
    if (hit(&btn_out, x, y) && range_i < RANGES - 1) {
        range_i++;
        return true;
    }
    return false;
}
