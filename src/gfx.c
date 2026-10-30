#include "gfx.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *fb;

void gfx_set_target(uint8_t *framebuffer) {
    fb = framebuffer;
}

void gfx_clear(uint8_t c) {
    memset(fb, c, GFX_W * GFX_H);
}

void gfx_pixel(int x, int y, uint8_t c) {
    if ((unsigned)x < GFX_W && (unsigned)y < GFX_H) fb[y * GFX_W + x] = c;
}

void gfx_hline(int x0, int x1, int y, uint8_t c) {
    if ((unsigned)y >= GFX_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    if (x0 <= x1) memset(&fb[y * GFX_W + x0], c, x1 - x0 + 1);
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h; j++) gfx_hline(x, x + w - 1, j, c);
}

#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

// Liang-Barsky: clips the segment to the screen; false if none of it is on it
static int clip_t(int p, int q, float *t0, float *t1) {
    if (p == 0) return q >= 0;
    float r = (float)q / p;
    if (p < 0) {
        if (r > *t1) return 0;
        if (r > *t0) *t0 = r;
    } else {
        if (r < *t0) return 0;
        if (r < *t1) *t1 = r;
    }
    return 1;
}

void gfx_line(int x0, int y0, int x1, int y1, uint8_t c) {
    if ((unsigned)x0 >= GFX_W || (unsigned)y0 >= GFX_H || (unsigned)x1 >= GFX_W || (unsigned)y1 >= GFX_H) {
        int dx = x1 - x0, dy = y1 - y0;
        float t0 = 0, t1 = 1;
        if (!clip_t(-dx, x0, &t0, &t1) || !clip_t(dx, GFX_W - 1 - x0, &t0, &t1) ||
            !clip_t(-dy, y0, &t0, &t1) || !clip_t(dy, GFX_H - 1 - y0, &t0, &t1))
            return;
        int nx0 = x0 + (int)(t0 * dx + (dx >= 0 ? 0.5f : -0.5f)), ny0 = y0 + (int)(t0 * dy + (dy >= 0 ? 0.5f : -0.5f));
        int nx1 = x0 + (int)(t1 * dx + (dx >= 0 ? 0.5f : -0.5f)), ny1 = y0 + (int)(t1 * dy + (dy >= 0 ? 0.5f : -0.5f));
        x0 = CLAMP(nx0, 0, GFX_W - 1); y0 = CLAMP(ny0, 0, GFX_H - 1);
        x1 = CLAMP(nx1, 0, GFX_W - 1); y1 = CLAMP(ny1, 0, GFX_H - 1);
    }
    // all on screen now: write the pixels directly
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? GFX_W : -GFX_W;
    int err = dx + dy;
    uint8_t *p = &fb[y0 * GFX_W + x0], *end = &fb[y1 * GFX_W + x1];
    for (;;) {
        *p = c;
        if (p == end) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; p += sx; }
        if (e2 <= dx) { err += dx; p += sy; }
    }
}

void gfx_circle(int cx, int cy, int r, uint8_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, c); gfx_pixel(cx - x, cy + y, c);
        gfx_pixel(cx + x, cy - y, c); gfx_pixel(cx - x, cy - y, c);
        gfx_pixel(cx + y, cy + x, c); gfx_pixel(cx - y, cy + x, c);
        gfx_pixel(cx + y, cy - x, c); gfx_pixel(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void gfx_disc(int cx, int cy, int r, uint8_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_hline(cx - x, cx + x, cy + y, c); gfx_hline(cx - x, cx + x, cy - y, c);
        gfx_hline(cx - y, cx + y, cy + x, c); gfx_hline(cx - y, cx + y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void gfx_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c) {
    // sort by y, then fill each scanline between the long edge and the short ones
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y2 == y0) {
        int lo = x0 < x1 ? x0 : x1, hi = x0 > x1 ? x0 : x1;
        if (x2 < lo) lo = x2;
        if (x2 > hi) hi = x2;
        gfx_hline(lo, hi, y0, c);
        return;
    }
    for (int y = y0; y <= y2; y++) {
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int xb = y < y1 ? (y1 == y0 ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0))
                        : (y2 == y1 ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1));
        gfx_hline(xa, xb, y, c);
    }
}

int gfx_text(const font_t *font, int x, int y, const char *s, uint8_t ramp) {
    int x0 = x;
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch < font->first || ch > font->last) ch = '?';
        const font_glyph_t *g = &font->glyphs[ch - font->first];
        const uint8_t *row = font->bits + g->offset;
        int stride = (g->w + 1) / 2;
        for (int j = 0; j < g->h; j++, row += stride) {
            int py = y + g->y + j;
            if ((unsigned)py >= GFX_H) continue;
            for (int i = 0; i < g->w; i++) {
                int cov = i & 1 ? row[i >> 1] & 0x0f : row[i >> 1] >> 4;
                int px = x + g->x + i;
                if (cov && (unsigned)px < GFX_W) fb[py * GFX_W + px] = (uint8_t)(ramp + cov - 1);
            }
        }
        x += g->advance;
    }
    return x - x0;
}

int gfx_text_width(const font_t *font, const char *s) {
    int w = 0;
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch < font->first || ch > font->last) ch = '?';
        w += font->glyphs[ch - font->first].advance;
    }
    return w;
}
