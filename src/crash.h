// Restarting after a crash or a hang, and saying why afterwards. A hard fault saves where it
// happened in watchdog scratch registers 0-2 (they survive a reset; 4-7 are the SDK's) and
// restarts at once; the watchdog restarts the board if the main loop stops calling crash_alive()
// for CRASH_HANG_MS.
#pragma once

#include <stddef.h>

#define CRASH_HANG_MS 8000

// Call first thing: reads why the last restart happened, then starts the watchdog.
void crash_init(void);

// Call from the main loop, at least every CRASH_HANG_MS.
void crash_alive(void);

// Why the board last restarted, e.g. "fault at pc 1000abcd lr 1000ef01", "hang (watchdog)";
// "" after a normal power-on or a new firmware.
const char *crash_last(void);
