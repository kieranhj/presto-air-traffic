// readsb's aircraft.json (as the Pi serves it at :8080/data/aircraft.json), parsed into a fixed
// table. Plain C with no SDK calls, so it builds and tests on the host too.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FEED_MAX_AIRCRAFT 160

typedef struct {
    char hex[8];        // ICAO address, "~" first for a non-ICAO (TIS-B) one
    char flight[9];     // callsign, trailing spaces removed
    char reg[12];       // registration (r), from the Pi's database
    char type[8];       // ICAO type designator (t)
    char desc[28];      // the type in words (desc), e.g. "AIRBUS A-320neo"
    char category[3];   // emitter category, e.g. "A3", "A7" (rotorcraft)
    char squawk[5];

    bool has_alt;
    bool on_ground;     // alt_baro was "ground"
    int32_t alt_baro;   // ft

    bool has_track;
    float track;        // degrees true
    float gs;           // kt
    int32_t baro_rate;  // ft/min

    bool has_pos;
    double lat, lon;
    float r_dst;        // NM from the receiver
    float r_dir;        // degrees true from the receiver
    float seen;         // s since any message
    float seen_pos;     // s since the last position
} aircraft_t;

typedef struct {
    double now;         // the file's time, Unix seconds
    uint32_t messages;
    int count;          // entries in `aircraft`
    int dropped;        // aircraft past FEED_MAX_AIRCRAFT
    aircraft_t aircraft[FEED_MAX_AIRCRAFT];
} feed_t;

// Parses `len` bytes of aircraft.json. Returns false if it isn't that file's shape.
bool feed_parse(const char *json, size_t len, feed_t *out);

// Parses receiver.json for the station's position. Returns false if it has none.
bool feed_parse_receiver(const char *json, size_t len, double *lat, double *lon);
