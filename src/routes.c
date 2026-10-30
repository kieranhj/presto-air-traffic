#include "routes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "net.h"
#include "radar.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef ROUTES_HOST
#define ROUTES_HOST "aircraft-complaint-proxy.kieranhj.workers.dev"   // public: fight-or-flight's docs
#endif

#define CACHE_SIZE      128
#define KNOWN_TTL_MS    (24u * 3600 * 1000)
#define UNKNOWN_TTL_MS  (3600u * 1000)
#define BACKOFF_MIN_MS  60000u
#define BACKOFF_MAX_MS  (3600u * 1000)

typedef struct {
    char callsign[9];
    bool known;
    route_info_t info;
    uint32_t fetched_ms;
} entry_t;

static entry_t cache[CACHE_SIZE];
static char wanted[9], in_flight[9];
static char answer[2048];
static uint32_t next_auto_ms, resume_ms, backoff_ms;
static routes_stats_t stats;

static bool valid(const char *cs) {
    size_t n = strlen(cs);
    if (n < 3 || n > 8) return false;
    for (size_t i = 0; i < n; i++)
        if (!isupper((unsigned char)cs[i]) && !isdigit((unsigned char)cs[i])) return false;
    return true;
}

static entry_t *lookup(const char *cs, uint32_t now_ms) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        entry_t *e = &cache[i];
        if (!e->callsign[0] || strcmp(e->callsign, cs)) continue;
        if (now_ms - e->fetched_ms > (e->known ? KNOWN_TTL_MS : UNKNOWN_TTL_MS)) {
            e->callsign[0] = 0;     // stale: ask again
            return NULL;
        }
        return e;
    }
    return NULL;
}

static uint32_t now_cached_ms;

route_state_t routes_get(const char *cs, const route_info_t **info) {
    if (!cs[0]) return ROUTE_NONE;
    if (in_flight[0] && !strcmp(in_flight, cs)) return ROUTE_PENDING;
    entry_t *e = lookup(cs, now_cached_ms);
    if (!e) return !strcmp(wanted, cs) && valid(cs) ? ROUTE_PENDING : ROUTE_NONE;
    if (!e->known) return ROUTE_UNKNOWN;
    if (info) *info = &e->info;
    return ROUTE_KNOWN;
}

void routes_want(const char *cs) {
    snprintf(wanted, sizeof wanted, "%s", cs);
}

// -- turning the Worker's labels ("Paris Beauvais Tillé (BVA)") into a code and a plain name

// Latin-1 letters (UTF-8 C3 80-BF) to their plain ASCII letter; 0 to drop
static char fold(unsigned char second) {
    static const char map[64] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    return map[second & 0x3f];
}

static void plain(char *out, size_t cap, const char *in, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n && in[i] && o + 1 < cap; i++) {
        unsigned char ch = (unsigned char)in[i];
        if (ch < 0x80) out[o++] = (char)ch;
        else if (ch == 0xC3 && i + 1 < n) out[o++] = fold((unsigned char)in[++i]);
        else while (i + 1 < n && ((unsigned char)in[i + 1] & 0xC0) == 0x80) i++;   // skip the rest
    }
    out[o] = 0;
}

static void airport(const char *label, const char *icao, char *code, size_t code_cap, char *name, size_t name_cap) {
    size_t n = strlen(label);
    const char *open = strrchr(label, '(');
    // "Name (XYZ)": the IATA code in the brackets, the name before them
    if (open && n >= 5 && label[n - 1] == ')' && label + n - 1 - open == 4) {
        snprintf(code, code_cap, "%.3s", open + 1);
        size_t name_len = (size_t)(open - label);
        while (name_len && label[name_len - 1] == ' ') name_len--;
        plain(name, name_cap, label, name_len);
    } else {
        snprintf(code, code_cap, "%s", icao[0] ? icao : label);
        plain(name, name_cap, label[0] ? label : icao, n ? n : strlen(icao));
    }
    if (!name[0]) snprintf(name, name_cap, "%s", code);
}

// -- look-ups

static void store(const char *cs, const feed_route_t *r, uint32_t now_ms) {
    entry_t *slot = NULL, *oldest = NULL;
    for (int i = 0; i < CACHE_SIZE && !slot; i++) {
        if (!cache[i].callsign[0] || !strcmp(cache[i].callsign, cs)) slot = &cache[i];
        else if (!oldest || cache[i].fetched_ms < oldest->fetched_ms) oldest = &cache[i];
    }
    if (!slot) slot = oldest;
    memset(slot, 0, sizeof *slot);
    snprintf(slot->callsign, sizeof slot->callsign, "%s", cs);
    slot->fetched_ms = now_ms;
    slot->known = r->known && (r->origin_icao[0] || r->origin_label[0]) && (r->dest_icao[0] || r->dest_label[0]);
    if (slot->known) {
        airport(r->origin_label, r->origin_icao, slot->info.from_code, sizeof slot->info.from_code,
                slot->info.from_name, sizeof slot->info.from_name);
        airport(r->dest_label, r->dest_icao, slot->info.to_code, sizeof slot->info.to_code,
                slot->info.to_name, sizeof slot->info.to_name);
    }
}

static bool start(const char *cs, uint32_t now_ms) {
    char path[64];
    snprintf(path, sizeof path, "/api/route-lookup?callsign=%s", cs);
    if (!net_get_start(NET_SLOT_ROUTES, ROUTES_HOST, 443, true, path, answer, sizeof answer)) return false;
    snprintf(in_flight, sizeof in_flight, "%s", cs);
    stats.lookups++;
    (void)now_ms;
    return true;
}

bool routes_poll(const feed_t *feed, uint32_t now_ms) {
    now_cached_ms = now_ms;
    if (in_flight[0]) {
        size_t len;
        int http;
        net_get_status_t g = net_get_poll(NET_SLOT_ROUTES, &len, &http);
        if (g == NET_GET_BUSY || g == NET_GET_IDLE) return false;
        feed_route_t r;
        if (g == NET_GET_DONE && feed_parse_route(answer, len, &r)) {
            store(in_flight, &r, now_ms);
            backoff_ms = 0;
            const entry_t *e = lookup(in_flight, now_ms);
            if (e && e->known) printf("routes: %s %s to %s\n", in_flight, e->info.from_code, e->info.to_code);
            else printf("routes: %s has no known route\n", in_flight);
        } else {
            stats.failures++;
            stats.last_http = http;
            backoff_ms = backoff_ms ? backoff_ms * 2 : BACKOFF_MIN_MS;
            if (backoff_ms > BACKOFF_MAX_MS) backoff_ms = BACKOFF_MAX_MS;
            resume_ms = now_ms + backoff_ms;
            printf("routes: %s failed (HTTP %d); next try in %lu s\n", in_flight, http,
                   (unsigned long)(backoff_ms / 1000));
        }
        in_flight[0] = 0;
        return true;
    }
    if (!net_up() || (backoff_ms && (int32_t)(now_ms - resume_ms) < 0)) return false;

    // the selected aircraft first
    if (valid(wanted) && !lookup(wanted, now_ms)) {
        start(wanted, now_ms);
        return false;
    }
    // then any close and low, one every ROUTES_AUTO_GAP_MS
    if ((int32_t)(now_ms - next_auto_ms) < 0) return false;
    for (int i = 0; i < feed->count; i++) {
        const aircraft_t *a = &feed->aircraft[i];
        float nm, deg;
        if (!valid(a->flight) || a->on_ground || !a->has_alt || a->alt_baro >= ROUTES_AUTO_FT) continue;
        if (!radar_range_bearing(a, &nm, &deg) || nm > ROUTES_AUTO_NM || lookup(a->flight, now_ms)) continue;
        if (start(a->flight, now_ms)) next_auto_ms = now_ms + ROUTES_AUTO_GAP_MS;
        break;
    }
    return false;
}

void routes_stats(routes_stats_t *out, uint32_t now_ms) {
    *out = stats;
    out->cached = 0;
    for (int i = 0; i < CACHE_SIZE; i++) out->cached += cache[i].callsign[0] != 0;
    out->backoff_s = backoff_ms && (int32_t)(resume_ms - now_ms) > 0 ? (resume_ms - now_ms) / 1000 : 0;
}
