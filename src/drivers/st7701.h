// The Presto's 480x480 ST7701 panel in 8-bit palette mode: a byte per pixel, each looked up in a
// 256-entry palette by PIO and DMA as the panel is scanned out. No CPU time per frame.
//
// C port of Pimoroni's ST7701 driver (MIT, see LICENSE.pimoroni), cut down to the full-resolution
// palette mode.
#pragma once

#include <stdint.h>

#define ST7701_WIDTH  480
#define ST7701_HEIGHT 480

// Starts the panel scanning out `framebuffer` (ST7701_WIDTH * ST7701_HEIGHT bytes, in SRAM) through
// `palette` (256 words, in SRAM, aligned to 1024 bytes). Its interrupts run on the calling core,
// so call it from the core that will service them (core 1 here).
void st7701_init(uint8_t *framebuffer, uint32_t *palette);

// Sets a palette entry from 8-bit RGB; takes effect as the scan reaches it. Ignored before
// st7701_init.
void st7701_set_palette(uint8_t entry, uint8_t r, uint8_t g, uint8_t b);

// Scans out `framebuffer` from the next frame on.
void st7701_set_framebuffer(uint8_t *framebuffer);

// Waits for the end of the current frame.
void st7701_wait_vsync(void);

// 0 (off) to 255 (full).
void st7701_set_backlight(uint8_t brightness);

// Frames scanned out since st7701_init.
uint32_t st7701_frames(void);
