#include "gfx.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *fb;

void gfx_init(uint8_t *framebuffer) {
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

void gfx_line(int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
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
