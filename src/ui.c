#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "palette.h"
#include "radar.h"
#include "routes.h"

enum { SCREEN_LIST, SCREEN_RADAR, SCREEN_STATUS, SCREENS };

#define HEADER_H 34             // the status line along the top of every screen
#define STRIP_H 118             // the details strip, along the bottom of the radar
#define STRIP_Y (GFX_H - STRIP_H)
#define ROW_H 48                // a row of the list
#define LIST_Y (HEADER_H + 4)
#define LIST_ROWS ((GFX_H - LIST_Y) / ROW_H)

#define TAP_SLOP 22             // a touch that moves less than this is a tap
#define SWIPE_MIN 70            // a horizontal swipe moves at least this
#define FORGET_SELECTED_MS 60000    // a selected aircraft that's gone is let go after this
#define HOLD_MS 350             // a touch held still this long drags the map

static uint8_t *screen, *back_buffer;
static int current = SCREEN_RADAR;

// The selection, and its last known details (shown, marked stale, after it drops out)
static char selected[8];
static aircraft_t selected_last;
static uint32_t selected_seen_ms;

// The list as last drawn: which aircraft each row was, for taps
static int list_scroll;
static char list_hex[LIST_ROWS][8];

// The touch in progress: where it started and is now; whether it has moved (a swipe), or was
// held still long enough to drag the map (panning, from pan_x, pan_y)
static bool was_down, moved, panning;
static int start_x, start_y, last_x, last_y, pan_x, pan_y;
static uint32_t down_ms;

void ui_init(uint8_t *screen_fb, uint8_t *base_layer, uint8_t *back) {
    screen = screen_fb;
    back_buffer = base_layer && back ? back : NULL;
    radar_set_base_layer(back_buffer ? base_layer : NULL);
}

// -- helpers

static const aircraft_t *find(const feed_t *feed, const char *hex) {
    if (!hex[0]) return NULL;
    for (int i = 0; i < feed->count; i++)
        if (!strcmp(feed->aircraft[i].hex, hex)) return &feed->aircraft[i];
    return NULL;
}

static void select_aircraft(const aircraft_t *a, uint32_t now_ms) {
    if (!a) {
        selected[0] = 0;
        routes_want("");
        return;
    }
    strncpy(selected, a->hex, sizeof selected - 1);
    selected[sizeof selected - 1] = 0;
    selected_last = *a;
    selected_seen_ms = now_ms;
    routes_want(a->flight);
}

static const char *name_of(const aircraft_t *a) {
    return a->flight[0] ? a->flight : a->reg[0] ? a->reg : a->hex;
}

static const char *compass(float deg) {
    static const char *const points[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                         "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    return points[(int)((deg + 11.25f) / 22.5f) % 16];
}

// 12345 -> "12,345"
static void thousands(char *out, size_t cap, long v) {
    char digits[16];
    snprintf(digits, sizeof digits, "%ld", labs(v));
    size_t n = strlen(digits), o = 0;
    if (v < 0 && o + 1 < cap) out[o++] = '-';
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        out[o++] = digits[i];
        if ((n - i - 1) % 3 == 0 && i + 1 < n && o + 1 < cap) out[o++] = ',';
    }
    out[o] = 0;
}

static void altitude_text(char *out, size_t cap, const aircraft_t *a) {
    char n[16];
    if (a->on_ground) snprintf(out, cap, "On the ground");
    else if (a->has_alt) {
        thousands(n, sizeof n, a->alt_baro);
        snprintf(out, cap, "%s ft", n);
    } else snprintf(out, cap, "Altitude unknown");
}

// -- touch

static void tap(int x, int y, const feed_t *feed, uint32_t now_ms) {
    if (current == SCREEN_RADAR) {
        bool changed;
        int bottom = selected[0] ? STRIP_Y : GFX_H;
        if (y < bottom && radar_button(x, y, &changed)) return;
        if (selected[0] && y >= STRIP_Y) {
            if (x >= GFX_W - 60 && y < STRIP_Y + 44) select_aircraft(NULL, now_ms);     // the strip's close mark
            return;
        }
        int i = radar_pick(x, y);
        select_aircraft(i >= 0 ? &feed->aircraft[i] : NULL, now_ms);
    } else if (current == SCREEN_LIST) {
        int row = (y - LIST_Y) / ROW_H;
        if (y < LIST_Y || row >= LIST_ROWS || !list_hex[row][0]) return;
        const aircraft_t *a = find(feed, list_hex[row]);
        if (a) {
            select_aircraft(a, now_ms);
            current = SCREEN_RADAR;
        }
    }
}

bool ui_touch(const touch_state_t *t, const feed_t *feed, uint32_t now_ms) {
    bool down = t->count > 0;
    const touch_point_t *p = t->p[0].down ? &t->p[0] : &t->p[1];
    if (down) {
        if (!was_down) {
            start_x = last_x = p->x;
            start_y = last_y = p->y;
            down_ms = now_ms;
            moved = panning = false;
        } else {
            last_x = p->x;
            last_y = p->y;
        }
        was_down = true;

        // on the radar, a touch held still for HOLD_MS, away from the buttons and the strip,
        // then drags the map; one that moves sooner is a swipe (or a tap) as usual
        if (current == SCREEN_RADAR && !panning && !moved) {
            int bottom = selected[0] ? STRIP_Y : GFX_H;
            bool on_controls = start_y >= bottom - 72 && start_x >= 280;
            if (abs(last_x - start_x) >= TAP_SLOP || abs(last_y - start_y) >= TAP_SLOP) moved = true;
            else if (now_ms - down_ms >= HOLD_MS && start_y < bottom && !on_controls) {
                panning = true;
                pan_x = last_x;
                pan_y = last_y;
            }
        }
        if (panning && (last_x != pan_x || last_y != pan_y)) {
            int dx = last_x - pan_x, dy = last_y - pan_y;
            pan_x = last_x;
            pan_y = last_y;
            return radar_pan(dx, dy);
        }
        return false;
    }
    if (!was_down) return false;
    was_down = false;
    if (panning) {
        panning = false;
        return true;
    }

    // released: a swipe, a scroll of the list, a swipe down on the radar, or a tap
    int dx = last_x - start_x, dy = last_y - start_y;
    if (current == SCREEN_RADAR && selected[0] && dy >= SWIPE_MIN / 2 && dy > 2 * abs(dx)) {
        select_aircraft(NULL, now_ms);      // closes the strip
        return true;
    }
    if (abs(dx) >= SWIPE_MIN && abs(dx) > 2 * abs(dy)) {
        int next = current + (dx < 0 ? 1 : -1);
        if (next < 0 || next >= SCREENS) return false;
        current = next;
        return true;
    }
    if (current == SCREEN_LIST && abs(dy) >= TAP_SLOP * 2 && abs(dy) > abs(dx)) {
        list_scroll -= dy / ROW_H ? dy / ROW_H : (dy < 0 ? -1 : 1);
        if (list_scroll < 0) list_scroll = 0;
        return true;
    }
    if (abs(dx) < TAP_SLOP && abs(dy) < TAP_SLOP) {
        tap(start_x, start_y, feed, now_ms);
        return true;
    }
    return false;
}

// The clock: the Pi's time (aircraft.json's "now"; it keeps it by NTP) and when we had it, ticking
// on from there by our own timer between fetches
static double clock_unix;
static uint32_t clock_ms;

void ui_update(const feed_t *feed, uint32_t now_ms) {
    if (feed->now > 0) {
        clock_unix = feed->now;
        clock_ms = now_ms;
    }
    if (!selected[0]) return;
    const aircraft_t *a = find(feed, selected);
    if (a) {
        selected_last = *a;
        selected_seen_ms = now_ms;
    } else if (now_ms - selected_seen_ms > FORGET_SELECTED_MS) {
        select_aircraft(NULL, now_ms);
    }
}

// -- drawing

static int with_position(const feed_t *feed) {
    int n = 0;
    for (int i = 0; i < feed->count; i++) n += feed->aircraft[i].has_pos;
    return n;
}

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar (Howard Hinnant's
// days_from_civil)
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// ... and back (civil_from_days)
static void civil_from_days(long days, int *y, int *m, int *d) {
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400) + (*m <= 2);
}

// The Unix time of 01:00 UTC on the last Sunday of a month: when UK clocks change
static long long clocks_change(int year, int month) {
    long last = days_from_civil(year, month, 31);
    long weekday = (last + 4) % 7;              // 1970-01-01 was a Thursday; 0 is Sunday
    return (long long)(last - weekday) * 86400 + 3600;
}

// "Mon 2 Nov  14:32", in UK time (BST from the last Sunday in March to the last in October)
static bool uk_time_text(char *out, size_t cap, uint32_t now_ms) {
    static const char *const days_of_week[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (clock_unix <= 0) return false;
    long long t = (long long)clock_unix + (now_ms - clock_ms) / 1000;
    int year, month, day;
    civil_from_days((long)(t / 86400), &year, &month, &day);
    if (t >= clocks_change(year, 3) && t < clocks_change(year, 10)) t += 3600;

    long days = (long)(t / 86400), secs = (long)(t % 86400);
    civil_from_days(days, &year, &month, &day);
    snprintf(out, cap, "%s %d %s  %02ld:%02ld", days_of_week[(days + 4) % 7], day, months[month - 1],
             secs / 3600, secs / 60 % 60);
    return true;
}

static void draw_header(const feed_t *feed, const ui_status_t *st, uint32_t now_ms) {
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
    // the date and time first, then the status; a status too long to fit beside them before the
    // page dots has the line to itself
    char when[24];
    int x = 10;
    if (uk_time_text(when, sizeof when, now_ms)) {
        int after = x + gfx_text_width(&font_small, when) + 18;
        if (after + 12 + gfx_text_width(&font_small, text) < GFX_W / 2 - 24) {
            gfx_text(&font_small, x, 22, when, TEXT_RAMP(T_GREY));
            x = after;
        }
    }
    gfx_disc(x + 6, 17, 5, dot);
    gfx_text(&font_small, x + 18, 22, text, TEXT_RAMP(col));

    int n = with_position(feed);
    snprintf(text, sizeof text, n == 1 ? "1 aircraft" : "%d aircraft", n);
    gfx_text(&font_small, GFX_W - 10 - gfx_text_width(&font_small, text), 22, text, TEXT_RAMP(T_GREY));

    // which screen: three dots, the current one bright
    for (int i = 0; i < SCREENS; i++) gfx_disc(GFX_W / 2 + (i - 1) * 16, 17, 3, i == current ? C_HOUSE : C_RING);
}

static void draw_strip(const feed_t *feed, uint32_t now_ms) {
    const aircraft_t *live = find(feed, selected);
    const aircraft_t *a = live ? live : &selected_last;
    char line[80], n1[16], n2[16];
    int y = STRIP_Y;

    gfx_fill_rect(0, y, GFX_W, STRIP_H, C_BG);
    gfx_hline(0, GFX_W - 1, y, C_PANEL_EDGE);
    gfx_hline(0, GFX_W - 1, y + 1, C_PANEL_EDGE);

    // the close mark
    int cx = GFX_W - 26, cy = y + 22;
    for (int d = -1; d <= 1; d++) {
        gfx_line(cx - 8 + d, cy - 8, cx + 8 + d, cy + 8, C_HOUSE);
        gfx_line(cx - 8 + d, cy + 8, cx + 8 + d, cy - 8, C_HOUSE);
    }

    // callsign, then registration and type
    int x = 14 + gfx_text(&font_large, 14, y + 31, name_of(a), TEXT_RAMP(T_WHITE)) + 12;
    line[0] = 0;
    if (a->flight[0] && a->reg[0]) snprintf(line, sizeof line, "%s", a->reg);
    if (a->type[0]) snprintf(line + strlen(line), sizeof line - strlen(line), "%s%s", line[0] ? "  " : "", a->type);
    x += gfx_text(&font_small, x, y + 30, line, TEXT_RAMP(T_GREY));

    // where it's flying from and to (the Worker); without a route, what it is (the Pi's
    // database), or its ICAO address
    const route_info_t *route;
    route_state_t rs = routes_get(a->flight, &route);
    if (rs == ROUTE_KNOWN) {
        // the type in words after the registration, if there's room
        if (a->desc[0] && x + 12 + gfx_text_width(&font_small, a->desc) < GFX_W - 56)
            gfx_text(&font_small, x + 12, y + 30, a->desc, TEXT_RAMP(T_GREY));
        snprintf(line, sizeof line, "%s \x80 %s", route->from_code, route->to_code);
        x = 14 + gfx_text(&font_small, 14, y + 53, line, TEXT_RAMP(T_WHITE)) + 12;
        snprintf(line, sizeof line, "%s to %s", route->from_name, route->to_name);
        if (x + gfx_text_width(&font_small, line) < GFX_W - 8) gfx_text(&font_small, x, y + 53, line, TEXT_RAMP(T_GREY));
    } else {
        if (rs == ROUTE_PENDING) snprintf(line, sizeof line, "Looking up the route...");
        else if (rs == ROUTE_UNKNOWN) snprintf(line, sizeof line, "Route not known%s%s", a->desc[0] ? "   " : "", a->desc);
        else if (a->desc[0]) snprintf(line, sizeof line, "%s", a->desc);
        else snprintf(line, sizeof line, "ICAO %s", a->hex);
        gfx_text(&font_small, 14, y + 53, line, TEXT_RAMP(T_GREY));
    }

    // altitude and climb, speed and track
    altitude_text(line, sizeof line, a);
    x = 14 + gfx_text(&font_small, 14, y + 79, line, TEXT_RAMP(radar_alt_text(a))) + 14;
    line[0] = 0;
    if (!a->on_ground && abs((int)a->baro_rate) >= 200) {
        thousands(n1, sizeof n1, labs((long)a->baro_rate));
        snprintf(line, sizeof line, "%s %s ft/min   ", a->baro_rate > 0 ? "climbing" : "descending", n1);
    }
    if (a->gs > 0) snprintf(line + strlen(line), sizeof line - strlen(line), "%d kt   ", (int)lroundf(a->gs));
    if (a->has_track) snprintf(line + strlen(line), sizeof line - strlen(line), "track %03d", (int)lroundf(a->track) % 360);
    gfx_text(&font_small, x, y + 79, line, TEXT_RAMP(T_GREY));

    // where it is, its squawk, and how fresh this is
    float nm, deg;
    line[0] = 0;
    if (radar_range_bearing(a, &nm, &deg)) snprintf(line, sizeof line, "%.1f NM %s   ", nm, compass(deg));
    if (a->squawk[0]) snprintf(line + strlen(line), sizeof line - strlen(line), "squawk %s   ", a->squawk);
    x = 14 + gfx_text(&font_small, 14, y + 103, line, TEXT_RAMP(T_WHITE));
    uint32_t age_s = live ? (uint32_t)a->seen : (now_ms - selected_seen_ms) / 1000 + (uint32_t)a->seen;
    if (!live || age_s >= 10) {
        snprintf(n2, sizeof n2, "%lu s", (unsigned long)age_s);
        snprintf(line, sizeof line, live ? "last heard %s ago" : "lost: last heard %s ago", n2);
        gfx_text(&font_small, x, y + 103, line, TEXT_RAMP(live ? T_WAIT : T_BAD));
    }
}

static int by_distance(const void *pa, const void *pb) {
    const aircraft_t *a = *(const aircraft_t *const *)pa, *b = *(const aircraft_t *const *)pb;
    float da, db, deg;
    bool ha = radar_range_bearing(a, &da, &deg), hb = radar_range_bearing(b, &db, &deg);
    if (ha != hb) return ha ? -1 : 1;           // those with a position first
    if (!ha) return strcmp(name_of(a), name_of(b));
    return da < db ? -1 : da > db;
}

static void draw_list(const feed_t *feed) {
    static const aircraft_t *order[FEED_MAX_AIRCRAFT];
    int n = 0;
    for (int i = 0; i < feed->count; i++) order[n++] = &feed->aircraft[i];
    qsort(order, n, sizeof order[0], by_distance);
    if (list_scroll > n - LIST_ROWS) list_scroll = n - LIST_ROWS > 0 ? n - LIST_ROWS : 0;

    memset(list_hex, 0, sizeof list_hex);
    if (!n) {
        gfx_text(&font_large, 24, LIST_Y + 40, "No aircraft heard", TEXT_RAMP(T_GREY));
        return;
    }
    char text[48], alt[24];
    for (int r = 0; r < LIST_ROWS && list_scroll + r < n; r++) {
        const aircraft_t *a = order[list_scroll + r];
        int y = LIST_Y + r * ROW_H;
        snprintf(list_hex[r], sizeof list_hex[r], "%s", a->hex);
        if (selected[0] && !strcmp(a->hex, selected)) {
            gfx_hline(0, GFX_W - 1, y, C_HOUSE);
            gfx_hline(0, GFX_W - 1, y + ROW_H - 1, C_HOUSE);
        }
        gfx_fill_rect(6, y + 8, 5, ROW_H - 16, radar_alt_colour(a));
        gfx_text(&font_large, 20, y + 26, name_of(a), TEXT_RAMP(T_WHITE));
        text[0] = 0;
        if (a->type[0]) snprintf(text, sizeof text, "%s", a->type);
        if (a->flight[0] && a->reg[0]) snprintf(text + strlen(text), sizeof text - strlen(text), "%s%s", text[0] ? "  " : "", a->reg);
        const route_info_t *route;
        if (routes_get(a->flight, &route) == ROUTE_KNOWN)
            snprintf(text + strlen(text), sizeof text - strlen(text), "%s%s \x80 %s", text[0] ? "   " : "", route->from_code, route->to_code);
        gfx_text(&font_small, 20, y + 43, text, TEXT_RAMP(T_GREY));

        float nm, deg;
        if (radar_range_bearing(a, &nm, &deg)) snprintf(text, sizeof text, "%.1f NM %s", nm, compass(deg));
        else snprintf(text, sizeof text, "no position");
        gfx_text(&font_large, GFX_W - 14 - gfx_text_width(&font_large, text), y + 26, text,
                 TEXT_RAMP(radar_range_bearing(a, &nm, &deg) ? T_WHITE : T_GREY));
        altitude_text(alt, sizeof alt, a);
        gfx_text(&font_small, GFX_W - 14 - gfx_text_width(&font_small, alt), y + 43, alt, TEXT_RAMP(radar_alt_text(a)));
        if (r + 1 < LIST_ROWS) gfx_hline(20, GFX_W - 14, y + ROW_H - 1, C_AXIS);
    }
    if (n > LIST_ROWS) {
        // where we are in it: a thin bar down the right edge
        int h = (GFX_H - LIST_Y) * LIST_ROWS / n, top = LIST_Y + (GFX_H - LIST_Y - h) * list_scroll / (n - LIST_ROWS);
        gfx_fill_rect(GFX_W - 4, top, 3, h, C_RING);
    }
}

static void status_row(int *y, const char *label, const char *value, text_colour_t col) {
    gfx_text(&font_small, 24, *y, label, TEXT_RAMP(T_GREY));
    gfx_text(&font_small, 150, *y, value, TEXT_RAMP(col));
    *y += 30;
}

static void draw_status_screen(const feed_t *feed, const ui_status_t *st) {
    char v[64];
    int y = HEADER_H + 40;
    gfx_text(&font_large, 24, y - 6, "Status", TEXT_RAMP(T_WHITE));
    y += 34;

    snprintf(v, sizeof v, "%s, port 8080", st->pi_host);
    status_row(&y, "Receiver", v, T_WHITE);
    if (st->data_age_ms < 0) status_row(&y, "Data", "none yet", T_WAIT);
    else {
        snprintf(v, sizeof v, st->data_age_ms > 5000 ? "none for %ld s" : "live", (long)(st->data_age_ms / 1000));
        status_row(&y, "Data", v, st->data_age_ms > 5000 ? T_BAD : T_OK);
    }
    snprintf(v, sizeof v, "%d heard, %d with a position", feed->count, with_position(feed));
    status_row(&y, "Aircraft", v, T_WHITE);
    snprintf(v, sizeof v, "%.0f a second", st->messages_per_s);
    status_row(&y, "Messages", v, T_WHITE);
    if (st->wifi_up) snprintf(v, sizeof v, "%s, %d dBm", st->ip, st->rssi);
    else snprintf(v, sizeof v, "%s", st->wifi_state);
    status_row(&y, "Wi-Fi", v, st->wifi_up ? T_WHITE : T_WAIT);
    snprintf(v, sizeof v, "%.1f frames a second", st->fps);
    status_row(&y, "Screen", v, T_WHITE);
    if (st->psram_kb) {
        snprintf(v, sizeof v, st->psram_errors ? "%lu KB, %lu errors in the self-test" : "%lu KB, self-test passed",
                 (unsigned long)st->psram_kb, (unsigned long)st->psram_errors);
        status_row(&y, "PSRAM", v, st->psram_errors ? T_BAD : T_WHITE);
    } else status_row(&y, "PSRAM", "none found", T_WAIT);
    uint32_t h = st->uptime_s / 3600, m = st->uptime_s / 60 % 60;
    if (h) snprintf(v, sizeof v, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
    else snprintf(v, sizeof v, "%lu min", (unsigned long)m);
    status_row(&y, "Running for", v, T_WHITE);
    snprintf(v, sizeof v, "built %s", __DATE__);
    status_row(&y, "Firmware", v, T_WHITE);
    if (st->last_restart[0]) status_row(&y, "Restarted", st->last_restart, T_BAD);
    routes_stats_t rs;
    routes_stats(&rs, st->uptime_s * 1000);
    if (rs.backoff_s) snprintf(v, sizeof v, "%lu, %lu failed; paused %lu s (HTTP %d)", (unsigned long)rs.lookups,
                               (unsigned long)rs.failures, (unsigned long)rs.backoff_s, rs.last_http);
    else snprintf(v, sizeof v, "%lu looked up, %lu failed, %lu kept", (unsigned long)rs.lookups,
                  (unsigned long)rs.failures, (unsigned long)rs.cached);
    status_row(&y, "Routes", v, rs.backoff_s ? T_WAIT : T_WHITE);
    status_row(&y, "Map", "\x7f OpenStreetMap contributors (ODbL)", T_GREY);
}

uint8_t *ui_draw(const feed_t *feed, const ui_status_t *st, uint32_t now_ms) {
    uint8_t *target = back_buffer ? back_buffer : screen;
    if (current == SCREEN_RADAR) {
        radar_draw(target, feed, selected, selected[0] ? STRIP_Y : GFX_H);
        if (selected[0]) draw_strip(feed, now_ms);
    } else {
        gfx_set_target(target);
        gfx_clear(C_BG);
        if (current == SCREEN_LIST) draw_list(feed);
        else draw_status_screen(feed, st);
    }
    draw_header(feed, st, now_ms);
    return target;
}
