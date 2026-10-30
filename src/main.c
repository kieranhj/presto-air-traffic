// Presto air traffic display, P0 bring-up (PLAN.md): the panel in palette mode, touch, Wi-Fi, and
// the Pi's aircraft.json fetched every second and drawn as dots on a radar. Tap to change the
// range. A status line goes to the USB serial port every 5 s.
//
// Core 1 runs the panel's interrupts and nothing else; core 0 does everything else.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "feed.h"
#include "gfx.h"
#include "net.h"
#include "drivers/ft6236.h"
#include "drivers/st7701.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy src/secrets.example.h to src/secrets.h and fill it in (git ignores it)"
#endif

#define FETCH_EVERY_MS  1000
#define STALE_AFTER_MS  5000
#define REPORT_EVERY_MS 5000

// Palette entries
enum {
    C_BG, C_RING, C_AXIS, C_HOUSE, C_TOUCH,
    C_OK, C_WAIT, C_BAD,
    C_GROUND, C_ALT_LOW, C_ALT_MID, C_ALT_HIGH, C_ALT_TOP,
    C_RAMP = 16,    // 16-255: three 80-step ramps (red, green, blue) to check the palette
};

static uint8_t framebuffer[ST7701_WIDTH * ST7701_HEIGHT] __attribute__((aligned(4)));
static uint32_t palette[256] __attribute__((aligned(1024)));
static char json[96 * 1024];
static feed_t feed;

static const int ranges_nm[] = {5, 10, 20, 40};
static int range_i = 2;

static void core1_main(void) {
    st7701_init(framebuffer, palette);
    for (;;) __wfe();
}

static void set_palette(void) {
    st7701_set_palette(C_BG, 0x06, 0x10, 0x1a);
    st7701_set_palette(C_RING, 0x1f, 0x4d, 0x3a);
    st7701_set_palette(C_AXIS, 0x14, 0x30, 0x26);
    st7701_set_palette(C_HOUSE, 0xff, 0xff, 0xff);
    st7701_set_palette(C_TOUCH, 0x00, 0xe5, 0xff);
    st7701_set_palette(C_OK, 0x2e, 0xcc, 0x71);
    st7701_set_palette(C_WAIT, 0xf1, 0xc4, 0x0f);
    st7701_set_palette(C_BAD, 0xe7, 0x4c, 0x3c);
    st7701_set_palette(C_GROUND, 0x7f, 0x8c, 0x8d);
    st7701_set_palette(C_ALT_LOW, 0xff, 0x6b, 0x3d);    // < 3,000 ft
    st7701_set_palette(C_ALT_MID, 0xff, 0xc9, 0x3c);    // 3,000-10,000 ft
    st7701_set_palette(C_ALT_HIGH, 0x6b, 0xd6, 0x6b);   // 10,000-20,000 ft
    st7701_set_palette(C_ALT_TOP, 0x5d, 0xad, 0xe2);    // > 20,000 ft
    for (int i = 0; i < 80; i++) {
        uint8_t v = (uint8_t)(i * 255 / 79);
        st7701_set_palette(C_RAMP + i, v, 0, 0);
        st7701_set_palette(C_RAMP + 80 + i, 0, v, 0);
        st7701_set_palette(C_RAMP + 160 + i, 0, 0, v);
    }
}

static uint8_t alt_colour(const aircraft_t *a) {
    if (!a->has_alt) return C_GROUND;
    if (a->on_ground) return C_GROUND;
    if (a->alt_baro < 3000) return C_ALT_LOW;
    if (a->alt_baro < 10000) return C_ALT_MID;
    if (a->alt_baro < 20000) return C_ALT_HIGH;
    return C_ALT_TOP;
}

static void draw(uint8_t status, const touch_state_t *touch) {
    const int cx = GFX_W / 2, cy = GFX_H / 2, radius = 230;
    const float px_per_nm = (float)radius / ranges_nm[range_i];

    gfx_clear(C_BG);
    gfx_line(cx, cy - radius, cx, cy + radius, C_AXIS);
    gfx_line(cx - radius, cy, cx + radius, cy, C_AXIS);
    for (int i = 1; i <= 4; i++) gfx_circle(cx, cy, radius * i / 4, C_RING);
    gfx_fill_rect(cx - 2, cy - radius - 8, 5, 8, C_RING);   // north

    for (int i = 0; i < feed.count; i++) {
        const aircraft_t *a = &feed.aircraft[i];
        if (!a->has_pos || a->seen_pos > 60) continue;
        float d = a->r_dir * (float)M_PI / 180;
        int x = cx + (int)lroundf(a->r_dst * px_per_nm * sinf(d));
        int y = cy - (int)lroundf(a->r_dst * px_per_nm * cosf(d));
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > (radius + 6) * (radius + 6)) continue;
        uint8_t col = alt_colour(a);
        if (a->has_track) {
            float t = a->track * (float)M_PI / 180;
            gfx_line(x, y, x + (int)lroundf(14 * sinf(t)), y - (int)lroundf(14 * cosf(t)), col);
        }
        gfx_disc(x, y, 4, col);
    }

    gfx_disc(cx, cy, 3, C_HOUSE);
    gfx_fill_rect(6, 6, 14, 14, status);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 80; j++) gfx_fill_rect(80 + j * 4, GFX_H - 24 + i * 6, 4, 5, C_RAMP + i * 80 + j);
    for (int n = 0; n < 2; n++)
        if (touch->p[n].down) gfx_circle(touch->p[n].x, touch->p[n].y, 18, C_TOUCH);
}

int main(void) {
    stdio_init_all();
    set_palette();
    gfx_init(framebuffer);
    gfx_clear(C_BG);
    multicore_launch_core1(core1_main);

    ft6236_init();
    printf("presto-air-traffic P0\n");
    if (!net_init(WIFI_SSID, WIFI_PASSWORD)) printf("net: the radio didn't start\n");

    touch_state_t touch = {0};
    bool was_down = false, redraw = true;
    absolute_time_t next_fetch = get_absolute_time(), next_report = make_timeout_time_ms(REPORT_EVERY_MS);
    absolute_time_t last_ok = nil_time, fetch_started = nil_time;
    uint32_t fetch_ms = 0, fetches = 0, fails = 0, taps = 0, frames_at_report = 0;
    size_t last_len = 0;
    uint8_t status = C_WAIT;

    for (;;) {
        net_poll();

        if (ft6236_poll(&touch)) {
            bool down = touch.count > 0;
            if (down && !was_down) {
                range_i = (range_i + 1) % (int)(sizeof ranges_nm / sizeof ranges_nm[0]);
                taps++;
            }
            if (down || was_down) redraw = true;
            was_down = down;
        }

        if (time_reached(next_fetch) && net_up()) {
            if (net_get_start(PI_HOST, PI_PORT, "/data/aircraft.json", json, sizeof json))
                fetch_started = get_absolute_time();
            next_fetch = make_timeout_time_ms(FETCH_EVERY_MS);
        }
        size_t len;
        int http;
        net_get_status_t g = net_get_poll(&len, &http);
        if (g == NET_GET_DONE || g == NET_GET_FAILED) {
            fetch_ms = (uint32_t)(absolute_time_diff_us(fetch_started, get_absolute_time()) / 1000);
            fetches++;
            last_len = len;
            if (g == NET_GET_DONE && feed_parse(json, len, &feed)) {
                last_ok = get_absolute_time();
            } else {
                fails++;
                printf("fetch: failed (HTTP %d, %u bytes)\n", http, (unsigned)len);
            }
            redraw = true;
        }

        uint8_t s = !net_up() ? C_WAIT
                  : is_nil_time(last_ok) ? C_WAIT
                  : absolute_time_diff_us(last_ok, get_absolute_time()) > STALE_AFTER_MS * 1000 ? C_BAD
                  : C_OK;
        if (s != status) {
            status = s;
            redraw = true;
        }

        if (redraw) {
            st7701_wait_vsync();
            draw(status, &touch);
            redraw = false;
        }

        if (time_reached(next_report)) {
            uint32_t f = st7701_frames();
            int with_pos = 0;
            for (int i = 0; i < feed.count; i++) with_pos += feed.aircraft[i].has_pos;
            printf("fps %.1f | wifi %s %s rssi %d | fetches %lu fails %lu last %lu ms %u bytes | "
                   "aircraft %d (%d with position, %d dropped) | range %d NM | taps %lu\n",
                   (f - frames_at_report) * 1000.0 / REPORT_EVERY_MS, net_state(), net_ip(),
                   net_up() ? net_rssi() : 0, (unsigned long)fetches, (unsigned long)fails,
                   (unsigned long)fetch_ms, (unsigned)last_len, feed.count, with_pos, feed.dropped,
                   ranges_nm[range_i], (unsigned long)taps);
            frames_at_report = f;
            next_report = make_timeout_time_ms(REPORT_EVERY_MS);
        }

        sleep_ms(10);
    }
}
