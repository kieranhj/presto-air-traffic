// Copy to src/secrets.h (git ignores it) and fill in. This repository is public: your network,
// password and addresses stay in secrets.h.
#pragma once

#define WIFI_SSID     "your network name"
#define WIFI_PASSWORD "your password"

// The receiver (the Pi running adsb.im). The Presto can't resolve .local names yet, so give the
// Pi a fixed address on the router and put it here.
#define PI_HOST "192.168.1.x"
#define PI_PORT 8080
