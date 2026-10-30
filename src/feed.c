// A small recursive-descent reader for aircraft.json: the top-level object's "now", "messages"
// and "aircraft" are read, everything else (and any nested value, like "lastPosition") skipped.
#include "feed.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p, *end;
} cur_t;

static void ws(cur_t *c) {
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\n' || *c->p == '\r' || *c->p == '\t')) c->p++;
}

static bool eat(cur_t *c, char ch) {
    ws(c);
    if (c->p < c->end && *c->p == ch) {
        c->p++;
        return true;
    }
    return false;
}

// A string into `out` (truncated to cap - 1, escapes kept as their second character).
static bool string(cur_t *c, char *out, size_t cap) {
    ws(c);
    if (c->p >= c->end || *c->p != '"') return false;
    c->p++;
    size_t n = 0;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p++;
        if (ch == '\\' && c->p < c->end) ch = *c->p++;
        if (out && n + 1 < cap) out[n++] = ch;
    }
    if (c->p >= c->end) return false;
    c->p++;
    if (out && cap) out[n] = 0;
    return true;
}

static bool number(cur_t *c, double *out) {
    ws(c);
    char tmp[32];
    size_t n = 0;
    while (c->p < c->end && n + 1 < sizeof tmp && strchr("+-0123456789.eE", *c->p)) tmp[n++] = *c->p++;
    if (!n) return false;
    tmp[n] = 0;
    *out = strtod(tmp, NULL);
    return true;
}

static bool skip_value(cur_t *c, int depth);

static bool skip_container(cur_t *c, char close, int depth) {
    if (eat(c, close)) return true;
    for (;;) {
        if (close == '}') {
            if (!string(c, NULL, 0) || !eat(c, ':')) return false;
        }
        if (!skip_value(c, depth + 1)) return false;
        if (eat(c, close)) return true;
        if (!eat(c, ',')) return false;
    }
}

static bool skip_value(cur_t *c, int depth) {
    if (depth > 16) return false;
    ws(c);
    if (c->p >= c->end) return false;
    switch (*c->p) {
        case '"': return string(c, NULL, 0);
        case '{': c->p++; return skip_container(c, '}', depth);
        case '[': c->p++; return skip_container(c, ']', depth);
        default: {
            // a number, true, false or null
            const char *start = c->p;
            while (c->p < c->end && !strchr(",]} \n\r\t", *c->p)) c->p++;
            return c->p > start;
        }
    }
}

static bool peek_string(cur_t *c) {
    ws(c);
    return c->p < c->end && *c->p == '"';
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = 0;
}

static bool aircraft(cur_t *c, aircraft_t *a) {
    memset(a, 0, sizeof *a);
    bool has_lat = false, has_lon = false, has_dst = false;
    if (!eat(c, '{')) return false;
    if (eat(c, '}')) return true;
    for (;;) {
        char key[16];
        if (!string(c, key, sizeof key) || !eat(c, ':')) return false;
        double v;
        bool ok = true;
        if (!strcmp(key, "hex")) ok = string(c, a->hex, sizeof a->hex);
        else if (!strcmp(key, "flight")) { ok = string(c, a->flight, sizeof a->flight); trim(a->flight); }
        else if (!strcmp(key, "r")) ok = string(c, a->reg, sizeof a->reg);
        else if (!strcmp(key, "t")) ok = string(c, a->type, sizeof a->type);
        else if (!strcmp(key, "desc")) ok = string(c, a->desc, sizeof a->desc);
        else if (!strcmp(key, "category")) ok = string(c, a->category, sizeof a->category);
        else if (!strcmp(key, "squawk")) ok = string(c, a->squawk, sizeof a->squawk);
        else if (!strcmp(key, "alt_baro")) {
            if (peek_string(c)) {
                char g[8];
                ok = string(c, g, sizeof g);
                a->on_ground = a->has_alt = !strcmp(g, "ground");
            } else if ((ok = number(c, &v))) {
                a->alt_baro = (int32_t)v;
                a->has_alt = true;
            }
        }
        else if (!strcmp(key, "track") && (ok = number(c, &v))) { a->track = (float)v; a->has_track = true; }
        else if (!strcmp(key, "gs") && (ok = number(c, &v))) a->gs = (float)v;
        else if (!strcmp(key, "baro_rate") && (ok = number(c, &v))) a->baro_rate = (int32_t)v;
        else if (!strcmp(key, "lat") && (ok = number(c, &v))) { a->lat = v; has_lat = true; }
        else if (!strcmp(key, "lon") && (ok = number(c, &v))) { a->lon = v; has_lon = true; }
        else if (!strcmp(key, "r_dst") && (ok = number(c, &v))) { a->r_dst = (float)v; has_dst = true; }
        else if (!strcmp(key, "r_dir") && (ok = number(c, &v))) a->r_dir = (float)v;
        else if (!strcmp(key, "seen") && (ok = number(c, &v))) a->seen = (float)v;
        else if (!strcmp(key, "seen_pos") && (ok = number(c, &v))) a->seen_pos = (float)v;
        else ok = skip_value(c, 1);
        if (!ok) return false;
        if (eat(c, '}')) break;
        if (!eat(c, ',')) return false;
    }
    a->has_pos = has_lat && has_lon && has_dst;
    return true;
}

bool feed_parse(const char *json, size_t len, feed_t *out) {
    cur_t c = {json, json + len};
    out->now = 0;
    out->messages = 0;
    out->count = 0;
    out->dropped = 0;
    bool saw_aircraft = false;
    if (!eat(&c, '{')) return false;
    if (eat(&c, '}')) return false;
    for (;;) {
        char key[16];
        if (!string(&c, key, sizeof key) || !eat(&c, ':')) return false;
        double v;
        if (!strcmp(key, "now")) {
            if (!number(&c, &v)) return false;
            out->now = v;
        } else if (!strcmp(key, "messages")) {
            if (!number(&c, &v)) return false;
            out->messages = (uint32_t)v;
        } else if (!strcmp(key, "aircraft")) {
            if (!eat(&c, '[')) return false;
            saw_aircraft = true;
            if (!eat(&c, ']')) {
                for (;;) {
                    aircraft_t tmp;
                    aircraft_t *a = out->count < FEED_MAX_AIRCRAFT ? &out->aircraft[out->count] : &tmp;
                    if (!aircraft(&c, a)) return false;
                    if (a == &tmp) out->dropped++;
                    else out->count++;
                    if (eat(&c, ']')) break;
                    if (!eat(&c, ',')) return false;
                }
            }
        } else if (!skip_value(&c, 1)) {
            return false;
        }
        if (eat(&c, '}')) break;
        if (!eat(&c, ',')) return false;
    }
    return saw_aircraft;
}

bool feed_parse_receiver(const char *json, size_t len, double *lat, double *lon) {
    cur_t c = {json, json + len};
    bool has_lat = false, has_lon = false;
    if (!eat(&c, '{')) return false;
    if (eat(&c, '}')) return false;
    for (;;) {
        char key[16];
        if (!string(&c, key, sizeof key) || !eat(&c, ':')) return false;
        double v;
        if (!strcmp(key, "lat") && number(&c, &v)) { *lat = v; has_lat = true; }
        else if (!strcmp(key, "lon") && number(&c, &v)) { *lon = v; has_lon = true; }
        else if (!skip_value(&c, 1)) return false;
        if (eat(&c, '}')) break;
        if (!eat(&c, ',')) return false;
    }
    return has_lat && has_lon;
}

bool feed_parse_route(const char *json, size_t len, feed_route_t *r) {
    cur_t c = {json, json + len};
    memset(r, 0, sizeof *r);
    if (!eat(&c, '{')) return false;
    if (eat(&c, '}')) return false;
    bool saw_route = false;
    for (;;) {
        char key[24];
        if (!string(&c, key, sizeof key) || !eat(&c, ':')) return false;
        if (!strcmp(key, "route")) {
            saw_route = true;
            ws(&c);
            if (c.p < c.end && *c.p == '{') {
                c.p++;
                r->known = true;
                if (!eat(&c, '}')) {
                    for (;;) {
                        char k[24];
                        if (!string(&c, k, sizeof k) || !eat(&c, ':')) return false;
                        bool ok;
                        if (!peek_string(&c)) ok = skip_value(&c, 2);     // null
                        else if (!strcmp(k, "originIcao")) ok = string(&c, r->origin_icao, sizeof r->origin_icao);
                        else if (!strcmp(k, "destinationIcao")) ok = string(&c, r->dest_icao, sizeof r->dest_icao);
                        else if (!strcmp(k, "originLabel")) ok = string(&c, r->origin_label, sizeof r->origin_label);
                        else if (!strcmp(k, "destinationLabel")) ok = string(&c, r->dest_label, sizeof r->dest_label);
                        else ok = skip_value(&c, 2);
                        if (!ok) return false;
                        if (eat(&c, '}')) break;
                        if (!eat(&c, ',')) return false;
                    }
                }
            } else if (!skip_value(&c, 1)) {
                return false;
            }
        } else if (!skip_value(&c, 1)) {
            return false;
        }
        if (eat(&c, '}')) break;
        if (!eat(&c, ',')) return false;
    }
    return saw_route;
}
