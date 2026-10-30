// Drawing into the 8-bit palette framebuffer. Coordinates are clipped to the screen.
#pragma once

#include <stdint.h>

#define GFX_W 480
#define GFX_H 480

void gfx_init(uint8_t *framebuffer);
void gfx_clear(uint8_t c);
void gfx_pixel(int x, int y, uint8_t c);
void gfx_hline(int x0, int x1, int y, uint8_t c);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t c);
void gfx_circle(int cx, int cy, int r, uint8_t c);
void gfx_disc(int cx, int cy, int r, uint8_t c);
