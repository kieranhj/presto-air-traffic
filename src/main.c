// Presto air traffic display (PLAN.md). Fetches the Pi's receiver.json once (the station's
// position) and its aircraft.json every second, and draws them on the radar (radar.c). A status
// line goes to the USB serial port every 5 s.
//
// Core 1 runs the panel's interrupts and nothing else; core 0 does everything else.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "feed.h"
#include "gfx.h"
#include "net.h"
#include "palette.h"
#include "radar.h"
#include "drivers/ft6236.h"
#include "drivers/st7701.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy src/secrets.example.h to src/secrets.h and fill it in (git ignores it)"
#endif

#define FETCH_EVERY_MS    1000
#define RECEIVER_RETRY_MS 5000
#define REDRAW_EVERY_MS   1000      // at least, so the status line's ages tick over
#define REPORT_EVERY_MS   5000

static uint8_t framebuffer[ST7701_WIDTH * ST7701_HEIGHT] __attribute__((aligned(4)));
static uint32_t palette[256] __attribute__((aligned(1024)));
static char json[96 * 1024];
static feed_t feed;

static void core1_main(void) {
    st7701_init(framebuffer, palette);
    multicore_fifo_push_blocking(0);    // the panel's up: core 0 can set the palette
    for (;;) __wfe();
}

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

int main(void) {
    stdio_init_all();
    gfx_init(framebuffer);
    gfx_clear(C_BG);
    multicore_launch_core1(core1_main);
    multicore_fifo_pop_blocking();
    palette_init();

    ft6236_init();
    printf("presto-air-traffic\n");
    if (!net_init(WIFI_SSID, WIFI_PASSWORD)) printf("net: the radio didn't start\n");

    enum { GET_NONE, GET_RECEIVER, GET_AIRCRAFT } in_flight = GET_NONE;
    touch_state_t touch = {0};
    bool was_down = false, redraw = true, have_data = false;
    uint32_t next_fetch = 0, next_receiver = 0, next_redraw = 0, next_report = REPORT_EVERY_MS;
    uint32_t last_ok = 0, fetch_started = 0, fetch_ms = 0, fetches = 0, fails = 0, taps = 0;
    uint32_t frames_at_report = 0, draw_us = 0, draw_us_max = 0;
    size_t last_len = 0;

    for (;;) {
        uint32_t now = now_ms();
        net_poll();

        if (ft6236_poll(&touch)) {
            bool down = touch.count > 0;
            if (down && !was_down) {
                taps++;
                if (radar_touch(touch.p[0].down ? touch.p[0].x : touch.p[1].x,
                                touch.p[0].down ? touch.p[0].y : touch.p[1].y))
                    redraw = true;
            }
            was_down = down;
        }

        // one GET at a time: the station's position until we have it, then the aircraft
        if (in_flight == GET_NONE && net_up()) {
            if (!radar_has_receiver() && (int32_t)(now - next_receiver) >= 0) {
                if (net_get_start(PI_HOST, PI_PORT, "/data/receiver.json", json, sizeof json))
                    in_flight = GET_RECEIVER;
                next_receiver = now + RECEIVER_RETRY_MS;
            } else if ((int32_t)(now - next_fetch) >= 0) {
                if (net_get_start(PI_HOST, PI_PORT, "/data/aircraft.json", json, sizeof json)) {
                    in_flight = GET_AIRCRAFT;
                    fetch_started = now;
                }
                next_fetch = now + FETCH_EVERY_MS;
            }
        }
        size_t len;
        int http;
        net_get_status_t g = net_get_poll(&len, &http);
        if (g == NET_GET_DONE || g == NET_GET_FAILED) {
            if (in_flight == GET_RECEIVER) {
                double lat, lon;
                if (g == NET_GET_DONE && feed_parse_receiver(json, len, &lat, &lon)) {
                    radar_set_receiver(lat, lon);
                    printf("receiver: position from the Pi\n");     // not printed: a home's location
                } else {
                    printf("receiver: no position (HTTP %d), retrying\n", http);
                }
            } else if (in_flight == GET_AIRCRAFT) {
                fetch_ms = now - fetch_started;
                fetches++;
                last_len = len;
                if (g == NET_GET_DONE && feed_parse(json, len, &feed)) {
                    last_ok = now;
                    have_data = true;
                    radar_track(&feed, now);
                } else {
                    fails++;
                    printf("fetch: failed (HTTP %d, %u bytes)\n", http, (unsigned)len);
                }
                redraw = true;
            }
            in_flight = GET_NONE;
        }

        if (redraw || (int32_t)(now - next_redraw) >= 0) {
            radar_status_t st = {
                .wifi_up = net_up(),
                .wifi_state = net_state(),
                .data_age_ms = have_data ? (int32_t)(now - last_ok) : -1,
            };
            st7701_wait_vsync();
            uint32_t t0 = time_us_32();
            radar_draw(&feed, &st);
            draw_us = time_us_32() - t0;
            if (draw_us > draw_us_max) draw_us_max = draw_us;
            redraw = false;
            next_redraw = now + REDRAW_EVERY_MS;
        }

        if ((int32_t)(now - next_report) >= 0) {
            uint32_t f = st7701_frames();
            int with_pos = 0;
            for (int i = 0; i < feed.count; i++) with_pos += feed.aircraft[i].has_pos;
            printf("fps %.1f | wifi %s %s rssi %d | receiver %s | fetches %lu fails %lu last %lu ms "
                   "%u bytes | aircraft %d (%d with position) | range %d NM | draw %lu us (max %lu) | "
                   "taps %lu\n",
                   (f - frames_at_report) * 1000.0 / REPORT_EVERY_MS, net_state(), net_ip(),
                   net_up() ? net_rssi() : 0, radar_has_receiver() ? "yes" : "no",
                   (unsigned long)fetches, (unsigned long)fails, (unsigned long)fetch_ms,
                   (unsigned)last_len, feed.count, with_pos, radar_range_nm(),
                   (unsigned long)draw_us, (unsigned long)draw_us_max, (unsigned long)taps);
            frames_at_report = f;
            next_report = now + REPORT_EVERY_MS;
        }

        sleep_ms(10);
    }
}
