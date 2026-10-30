// Wi-Fi (the RM2 module's CYW43439) and plain-HTTP GETs over lwIP. lwIP runs in the SDK's
// "threadsafe background" mode: its callbacks run in a low-priority interrupt on core 0.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Starts the radio and joins `ssid`. Returns false if the radio didn't start.
bool net_init(const char *ssid, const char *password);

// Call often: rejoins after the link drops.
void net_poll(void);

bool net_up(void);
const char *net_state(void);    // "up", "joining", "wrong password", ...
const char *net_ip(void);       // "" until up
int net_rssi(void);

// One GET at a time, into `buf` (the body only, NUL-terminated). Returns false if one is already
// in flight or the link is down.
bool net_get_start(const char *host, uint16_t port, const char *path, char *buf, size_t cap);

typedef enum { NET_GET_IDLE, NET_GET_BUSY, NET_GET_DONE, NET_GET_FAILED } net_get_status_t;

// The GET's state; on DONE, `len` is the body's length and `http_status` the server's status.
// Reading DONE or FAILED returns the slot to IDLE.
net_get_status_t net_get_poll(size_t *len, int *http_status);
