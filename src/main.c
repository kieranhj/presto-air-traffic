// Presto air traffic display (PLAN.md). Fetches the Pi's receiver.json once (the station's
// position) and its aircraft.json every second, and shows them on the screens in ui.c. A status
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
#include "routes.h"
#include "crash.h"
#include "ui.h"
#include "drivers/ft6236.h"
#include "drivers/psram.h"
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

#define SCREEN_BYTES (ST7701_WIDTH * ST7701_HEIGHT)

// The PSRAM's layout. lwipopts.h has the heap's address too (LWIP_RAM_HEAP_POINTER): keep them
// the same.
#define PSRAM_BASE_LAYER  (PSRAM_BASE)                  // the radar's map, 225 KB
#define PSRAM_BACK_BUFFER (PSRAM_BASE + 0x038400)       // frames composed here, 225 KB
#define PSRAM_LWIP_HEAP   (PSRAM_BASE + 0x080000)       // lwIP's heap and mbedTLS's buffers, 192 KB
#define PSRAM_JSON        (PSRAM_BASE + 0x0C0000)       // aircraft.json as it arrives
#define JSON_BYTES        (96 * 1024)
#define PSRAM_NEEDED      (0x0C0000 + JSON_BYTES)

// Core 0's stack. The SDK gives it 2 KB at the top of SCRATCH_Y, with core 1's right below: a TLS
// handshake, which lwIP runs in an interrupt on this stack, ran past it into core 1's and froze
// the panel. main() moves to this one, with the stack limit register at its bottom so an
// overflow faults (crash.c) rather than corrupting.
#define MAIN_STACK_BYTES  (32 * 1024)

static uint8_t framebuffer[SCREEN_BYTES] __attribute__((aligned(4)));
static uint32_t palette[256] __attribute__((aligned(1024)));
static uint32_t main_stack[MAIN_STACK_BYTES / 4] __attribute__((aligned(8)));
static char *const json = (char *)PSRAM_JSON;
static feed_t feed;

static void core1_main(void) {
    st7701_init(framebuffer, palette);
    multicore_fifo_push_blocking(0);    // the panel's up: core 0 can set the palette
    for (;;) __wfe();
}

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void __attribute__((noreturn)) app_main(void);

int main(void) {
    __asm volatile(
        "msr msplim, %0\n"
        "msr msp, %1\n"
        "bx %2\n"
        :: "r"(main_stack), "r"(main_stack + MAIN_STACK_BYTES / 4), "r"(app_main));
    __builtin_unreachable();
}

#define STACK_PAINT 0x57AC57ACu

// How deep core 0's stack has been: the painted words at its bottom that are still untouched
static uint32_t stack_used_bytes(void) {
    uint32_t i = 0;
    while (i < MAIN_STACK_BYTES / 4 && main_stack[i] == STACK_PAINT) i++;
    return MAIN_STACK_BYTES - i * 4;
}

static void __attribute__((noreturn)) app_main(void) {
    // paint the stack below where we are, to measure how deep it goes (stack_used_bytes)
    uint32_t *sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t *p = main_stack; p < sp - 64; p++) *p = STACK_PAINT;
    crash_init();
    stdio_init_all();
    // PSRAM first, while core 1 isn't running: its set-up takes over the QMI the flash is on
    size_t psram = psram_init(PIMORONI_PRESTO_PSRAM_CS_PIN);
    uint32_t psram_errors = psram ? psram_test(2 * SCREEN_BYTES) : 0;
    bool psram_ok = psram >= PSRAM_NEEDED && !psram_errors;
    ui_init(framebuffer, PSRAM_BASE_LAYER, PSRAM_BACK_BUFFER);
    gfx_set_target(framebuffer);
    gfx_clear(C_BG);
    multicore_launch_core1(core1_main);
    multicore_fifo_pop_blocking();
    palette_init();

    printf("presto-air-traffic: PSRAM %u KB, %lu errors%s%s\n", (unsigned)(psram / 1024),
           (unsigned long)psram_errors, crash_last()[0] ? "; restarted after a " : "", crash_last());
    if (!psram_ok) {
        // the network's heap and the frames live in PSRAM: nothing works without it
        gfx_text(&font_large, 24, 220, "The PSRAM isn't working", TEXT_RAMP(T_BAD));
        gfx_text(&font_small, 24, 250, psram ? "Its self-test found errors." : "None was found.",
                 TEXT_RAMP(T_GREY));
        for (;;) {
            crash_alive();
            sleep_ms(100);
        }
    }

    ft6236_init();
    if (!net_init(WIFI_SSID, WIFI_PASSWORD)) printf("net: the radio didn't start\n");

    enum { GET_NONE, GET_RECEIVER, GET_AIRCRAFT } in_flight = GET_NONE;
    touch_state_t touch = {0};
    bool redraw = true, have_data = false;
    uint32_t next_fetch = 0, next_receiver = 0, next_redraw = 0, next_report = REPORT_EVERY_MS;
    uint32_t last_ok = 0, fetch_started = 0, fetch_ms = 0, fetches = 0, fails = 0;
    uint32_t frames_at_report = 0, draw_us = 0, draw_us_max = 0, copy_us = 0;
    uint32_t messages_at = 0, messages_ms = 0;
    float messages_per_s = 0, fps = 0;
    size_t last_len = 0;

    for (;;) {
        uint32_t now = now_ms();
        crash_alive();
        net_poll();

        if (ft6236_poll(&touch) && ui_touch(&touch, &feed, now)) redraw = true;

        // one GET at a time: the station's position until we have it, then the aircraft
        if (in_flight == GET_NONE && net_up()) {
            if (!radar_has_receiver() && (int32_t)(now - next_receiver) >= 0) {
                if (net_get_start(NET_SLOT_PI, PI_HOST, PI_PORT, false, "/data/receiver.json", json, JSON_BYTES))
                    in_flight = GET_RECEIVER;
                next_receiver = now + RECEIVER_RETRY_MS;
            } else if ((int32_t)(now - next_fetch) >= 0) {
                if (net_get_start(NET_SLOT_PI, PI_HOST, PI_PORT, false, "/data/aircraft.json", json, JSON_BYTES)) {
                    in_flight = GET_AIRCRAFT;
                    fetch_started = now;
                }
                next_fetch = now + FETCH_EVERY_MS;
            }
        }
        size_t len;
        int http;
        net_get_status_t g = net_get_poll(NET_SLOT_PI, &len, &http);
        if (g == NET_GET_DONE || g == NET_GET_FAILED) {
            if (in_flight == GET_RECEIVER) {
                double lat, lon;
                if (g == NET_GET_DONE && feed_parse_receiver(json, len, &lat, &lon)) {
                    radar_set_receiver(lat, lon);
                    printf("receiver: position from the Pi\n");     // not printed: a home's location
                } else {
                    printf("receiver: no position (HTTP %d), retrying\n", http);
                }
                redraw = true;
            } else if (in_flight == GET_AIRCRAFT) {
                fetch_ms = now - fetch_started;
                fetches++;
                last_len = len;
                if (g == NET_GET_DONE && feed_parse(json, len, &feed)) {
                    last_ok = now;
                    have_data = true;
                    radar_track(&feed, now);
                    ui_update(&feed, now);
                    // the receiver's message rate, over at least 10 s
                    if (!messages_ms || feed.messages < messages_at) {
                        messages_at = feed.messages;
                        messages_ms = now;
                    } else if (now - messages_ms >= 10000) {
                        messages_per_s = (feed.messages - messages_at) * 1000.0f / (now - messages_ms);
                        messages_at = feed.messages;
                        messages_ms = now;
                    }
                } else {
                    fails++;
                    printf("fetch: failed (HTTP %d, %u bytes)\n", http, (unsigned)len);
                }
                redraw = true;
            }
            in_flight = GET_NONE;
        }

        // origin and destination airports, from the Worker
        if (routes_poll(&feed, now)) redraw = true;

        if (redraw || (int32_t)(now - next_redraw) >= 0) {
            ui_status_t st = {
                .wifi_up = net_up(),
                .wifi_state = net_state(),
                .ip = net_ip(),
                .rssi = net_up() ? net_rssi() : 0,
                .data_age_ms = have_data ? (int32_t)(now - last_ok) : -1,
                .messages_per_s = messages_per_s,
                .fps = fps,
                .psram_kb = (uint32_t)(psram / 1024),
                .psram_errors = psram_errors,
                .uptime_s = now / 1000,
                .pi_host = PI_HOST,
                .last_restart = crash_last(),
            };
            uint32_t t0 = time_us_32();
            uint8_t *drawn = ui_draw(&feed, &st, now);
            draw_us = time_us_32() - t0;
            if (draw_us > draw_us_max) draw_us_max = draw_us;
            if (drawn != framebuffer) {
                // top to bottom from the start of a frame, faster than the panel reads it: no tearing
                st7701_wait_vsync();
                t0 = time_us_32();
                memcpy(framebuffer, drawn, SCREEN_BYTES);
                copy_us = time_us_32() - t0;
            }
            redraw = false;
            next_redraw = now + REDRAW_EVERY_MS;
        }

        if ((int32_t)(now - next_report) >= 0) {
            uint32_t f = st7701_frames(), map_us, map_segments;
            fps = (f - frames_at_report) * 1000.0f / REPORT_EVERY_MS;
            radar_map_stats(&map_us, &map_segments);
            routes_stats_t rs;
            routes_stats(&rs, now);
            int with_pos = 0;
            for (int i = 0; i < feed.count; i++) with_pos += feed.aircraft[i].has_pos;
            printf("fps %.1f | wifi %s %s rssi %d | receiver %s | fetches %lu fails %lu last %lu ms "
                   "%u bytes | aircraft %d (%d with position) | %.0f msg/s | range %d NM | draw %lu us "
                   "(max %lu; map %lu us, %lu segments; copy %lu us) | PSRAM errors %lu | routes %lu "
                   "looked up, %lu failed, %lu kept | stack %lu of %u bytes\n",
                   fps, net_state(), net_ip(), net_up() ? net_rssi() : 0,
                   radar_has_receiver() ? "yes" : "no", (unsigned long)fetches, (unsigned long)fails,
                   (unsigned long)fetch_ms, (unsigned)last_len, feed.count, with_pos, messages_per_s,
                   radar_range_nm(), (unsigned long)draw_us, (unsigned long)draw_us_max,
                   (unsigned long)map_us, (unsigned long)map_segments, (unsigned long)copy_us,
                   (unsigned long)psram_errors, (unsigned long)rs.lookups, (unsigned long)rs.failures,
                   (unsigned long)rs.cached, (unsigned long)stack_used_bytes(), MAIN_STACK_BYTES);
            frames_at_report = f;
            next_report = now + REPORT_EVERY_MS;
        }

        sleep_ms(10);
    }
}
