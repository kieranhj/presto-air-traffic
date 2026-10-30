// Host test for src/feed.c: parses an aircraft.json (or, with -r, a receiver.json) file and
// prints what it read.
//   gcc -O1 -Wall -Wextra -Isrc tests/feed_test.c src/feed.c -o build-host/feed_test
//   build-host/feed_test aircraft.json
//   build-host/feed_test -r receiver.json
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "feed.h"

static feed_t feed;
static char buf[512 * 1024];

int main(int argc, char **argv) {
    int receiver = argc == 3 && !strcmp(argv[1], "-r");
    if (argc != 2 && !receiver) {
        fprintf(stderr, "usage: feed_test aircraft.json | -r receiver.json\n");
        return 2;
    }
    const char *path = argv[argc - 1];
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 2;
    }
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    if (receiver) {
        double lat, lon;
        if (!feed_parse_receiver(buf, n, &lat, &lon)) {
            printf("no position\n");
            return 1;
        }
        printf("position found\n");     // not printed: it's the receiver's, a home's, location
        return 0;
    }

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
        printf("%-7s %-8s %-8s %-4s %-2s sq %-4s alt %s%6ld trk %5.1f gs %5.1f vr %5ld pos %d "
               "%5.1f NM @ %5.1f seen %.1f/%.1f\n",
               a->hex, a->flight, a->reg, a->type, a->category, a->squawk,
               a->on_ground ? "G" : a->has_alt ? " " : "?", (long)a->alt_baro,
               a->track, a->gs, (long)a->baro_rate, a->has_pos, a->r_dst, a->r_dir,
               a->seen, a->seen_pos);
    }
    return 0;
}
