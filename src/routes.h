// Origin and destination airports, from the Fight or Flight Worker's /api/route-lookup over
// HTTPS, cached per callsign. Gentle on the Worker: one look-up at a time; the selected aircraft
// first, then automatically any within ROUTES_AUTO_NM and below ROUTES_AUTO_FT, at most one
// every ROUTES_AUTO_GAP_MS; answers kept a day (or an hour if it had no route); and on any
// failure a back-off from a minute, doubling, to an hour.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feed.h"

#define ROUTES_AUTO_NM      5.0f
#define ROUTES_AUTO_FT      10000
#define ROUTES_AUTO_GAP_MS  10000

typedef enum { ROUTE_NONE, ROUTE_PENDING, ROUTE_KNOWN, ROUTE_UNKNOWN } route_state_t;

typedef struct {
    char from_code[8], to_code[8];      // IATA codes where the Worker gave them, else ICAO
    char from_name[32], to_name[32];    // names, in plain ASCII (accents dropped), or the codes
} route_info_t;

// What's known of `callsign`'s route. On ROUTE_KNOWN, `info` points at it.
route_state_t routes_get(const char *callsign, const route_info_t **info);

// The selected aircraft's callsign ("" for none): looked up before any other.
void routes_want(const char *callsign);

// Starts and finishes look-ups. Returns true when a new answer came in (worth a redraw).
bool routes_poll(const feed_t *feed, uint32_t now_ms);

typedef struct {
    uint32_t lookups, failures, cached;
    uint32_t backoff_s;         // seconds until look-ups resume after a failure; 0 if none
    int last_http;              // the last failure's HTTP status (0: no answer)
} routes_stats_t;
void routes_stats(routes_stats_t *stats, uint32_t now_ms);
