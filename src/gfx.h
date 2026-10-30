// Drawing into the 8-bit palette framebuffer. Coordinates are clipped to the screen.
#pragma once

#include <stdint.h>

#include "font.h"

#define GFX_W 480
#define GFX_H 480

// What to draw into: the screen's framebuffer or another of the same size.
void gfx_set_target(uint8_t *framebuffer);
void gfx_clear(uint8_t c);
void gfx_pixel(int x, int y, uint8_t c);
void gfx_hline(int x0, int x1, int y, uint8_t c);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t c);
void gfx_circle(int cx, int cy, int r, uint8_t c);
void gfx_disc(int cx, int cy, int r, uint8_t c);
void gfx_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c);

// Text with its pen on the baseline at (x, y); `ramp` is a palette.h TEXT_RAMP. Returns the
// advance.
int gfx_text(const font_t *font, int x, int y, const char *s, uint8_t ramp);
int gfx_text_width(const font_t *font, const char *s);
