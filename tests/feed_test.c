// Host test for src/feed.c: parses an aircraft.json file and prints what it read.
//   gcc -O1 -Wall -Wextra -Isrc tests/feed_test.c src/feed.c -o build-host/feed_test
//   build-host/feed_test aircraft.json
#include <stdio.h>
#include <stdlib.h>
#include "feed.h"

static feed_t feed;

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: feed_test aircraft.json\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    static char buf[512 * 1024];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (!feed_parse(buf, n, &feed)) {
        printf("parse failed\n");
        return 1;
    }
    int with_pos = 0;
    for (int i = 0; i < feed.count; i++) with_pos += feed.aircraft[i].has_pos;
    printf("now %.1f messages %u aircraft %d with position %d dropped %d\n",
           feed.now, (unsigned)feed.messages, feed.count, with_pos, feed.dropped);
    for (int i = 0; i < feed.count; i++) {
        const aircraft_t *a = &feed.aircraft[i];
        printf("%-7s %-8s %-8s %-4s %-2s sq %-4s alt %s%6ld trk %5.1f gs %5.1f vr %5ld pos %d %.4f,%.4f %5.1f NM @ %5.1f seen %.1f/%.1f\n",
               a->hex, a->flight, a->reg, a->type, a->category, a->squawk,
               a->on_ground ? "G" : a->has_alt ? " " : "?", (long)a->alt_baro,
               a->track, a->gs, (long)a->baro_rate, a->has_pos, a->lat, a->lon, a->r_dst, a->r_dir,
               a->seen, a->seen_pos);
    }
    return 0;
}
