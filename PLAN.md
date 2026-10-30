# Presto air traffic display: plan

A desk display for local air traffic: a Pimoroni Presto showing what our own receiver (the Pi,
`docs/RECEIVER-PLAN.md` in fight-or-flight) hears, flagged with the
[Fight or Flight](https://github.com/kieranhj/fight-or-flight) rules. Status: **P0 in progress**
(see [P0 status](#p0-status)).

## Decisions so far

| Question | Choice |
|---|---|
| Main screen | Radar map centred on the house, tap an aircraft for details; swipe to a nearest-first list |
| Data | The Pi's `aircraft.json` over home Wi-Fi, polled about once a second. No aggregator load |
| Routes | The Fight or Flight Worker's `/api/route-lookup?callsign=`: **on tap, and automatically for very close aircraft**, cached per callsign |
| v1 features | Fight or Flight flags, alerts, route/airline details, night dimming |
| Map layers | Range rings, the EGLF runway (plus EGLL/EGKK markers), the WebTrak swaths, the receiver's coverage outline |
| Alerts | **LEDs only, never the piezo**. Triggers: any rule flag; low and close; helicopters and specials |
| Placement | A desk or shelf indoors, dimmed on a timer, woken by a touch or an alert |
| Firmware | A **separate** C / Pico SDK project, this repository (`presto-air-traffic`), not part of the BBC dual-boot |
| Base code | **Pimoroni's MIT-licensed ST7701 driver** (ported to C, palette mode) and the **Pico SDK's** own Wi-Fi and lwIP. `rp2350-bbc-micro` (GPLv3, mostly Dominic Pajak's) is used for know-how only; none of its code is copied, so this repository is MIT |

## The device

The Pimoroni Presto has:

- **Processor:** an RP2350, with two Cortex-M33 cores (250 MHz in the BBC project).
- **Memory:** 520 KB SRAM, 8 MB PSRAM and 16 MB flash.
- **Screen:** a 480×480 ST7701 RGB panel with capacitive touch.
- **Wi-Fi:** an RM2 module (CYW43439).
- **Other hardware:** a microSD slot, 7 WS2812 LEDs on the back, a piezo and a Qw/ST socket.

**Where the base code comes from:**

- **Panel:** Pimoroni's ST7701 driver (pimoroni/presto `drivers/st7701`, MIT), ported to C in
  `src/drivers/st7701.c`. Only its **8-bit palette mode** at 480×480 is kept: a byte a pixel
  (225 KB), looked up in a 256-entry palette by PIO and DMA as the panel scans, so no CPU time per
  frame. RGB565 would be 450 KB, which doesn't fit in SRAM alongside Wi-Fi.
- **Board header:** Pimoroni's MicroPython board header (BSD-3), with the RP2350B pin count, the
  RM2 module's CYW43 GPIOs and the PSRAM chip select (GPIO 47) added; a 200 MHz system clock.
- **Touch:** an **FT6236 at I2C address 0x48 on I2C1: SDA GPIO 30, SCL GPIO 31, interrupt
  GPIO 32**, from Pimoroni's `touch.py` (MIT). Up to two touches, in panel pixels.
- **Wi-Fi and HTTP:** the SDK's CYW43 driver (UK country code) and lwIP with its HTTP client
  (`httpc`), in the threadsafe-background mode.
- **LEDs:** 7 WS2812s on GPIO 33, driven from PIO as pimoroni/presto does.
- **Know-how** from the BBC project (PIO and DMA budgets, flash writes needing core 1 locked out,
  Wi-Fi setup on the screen), but no code.

**New work:**

- **Double buffering.** P0 draws straight into the buffer being scanned out, just after vsync, so a
  redraw can tear. The fix is a back buffer in PSRAM (8 MB, unused so far) copied in at vsync, or
  drawing only what changed.
- **A small drawing layer:** lines, circles and fills so far (`src/gfx.c`); still to come are
  filled polygons, a font, and rotated aircraft icons.
- **A JSON reader** for `aircraft.json` (`src/feed.c`). It reads the whole body from a 96 KB buffer
  without building a parse tree. The file is about 6 KB with 10 aircraft and should be around
  50 KB on a busy afternoon. Reading it as it streams in would free most of that buffer.

## Data flow

```
Pi (readsb) --Wi-Fi--> Presto: GET :8080/data/aircraft.json   every 1 s
                               GET :8080/data/receiver.json   at start (the station position)
                               GET :8080/data/outline.json    every few minutes (coverage outline)
Worker   --internet--> Presto: GET /api/route-lookup?callsign=X   on tap and for close aircraft, cached
```

- **The station position comes from the Pi's `receiver.json`, so the firmware has no
  coordinates.** This matters because the repository is public: nothing about the house goes into
  the source.
- `aircraft.json` already has `r` (registration), `t` (type), `desc`, `category`, `squawk`,
  `r_dst` and `r_dir` (range and bearing from the receiver), `seen` and `seen_pos`. The device
  doesn't need its own aircraft database.
- **Finding the Pi:** the Presto's lwIP advertises its own mDNS name but doesn't resolve other
  `.local` names. For v1 the Pi's address is a setting, with a DHCP reservation on the router.
  An mDNS query can come later.
- **If the Pi goes quiet:** the screen keeps the last picture, marks it stale with its age, and
  retries every 5 s. Nothing falls back to an aggregator.
- **The Worker** sees at most one request per callsign a day (see [Route look-ups](#route-look-ups)).
  It caches its answers at the edge too (an hour for a known route, 5 minutes for none), and its
  own route chain already backs off 429s.

## The screens

1. **Radar (the main screen):**
   - the house at the centre, north up, and range rings at 5 NM;
   - zoom with on-screen +/− in four steps (5, 10, 20 and 40 NM radius);
   - the EGLF runway (06/24), with markers for EGLL and EGKK;
   - the WebTrak swaths in a faint tint;
   - the coverage outline as a thin line;
   - each aircraft as a heading arrow coloured by altitude band, with a short trail, its callsign,
     and altitude in hundreds of feet;
   - a flagged aircraft drawn in the flag colour with a halo;
   - tap an aircraft to select it and show the details strip.
2. **Details strip** (the bottom ~110 px of the radar screen):
   - callsign, registration and type (`desc`);
   - the route from the Worker, e.g. "LHR → FAB";
   - altitude and vertical rate, ground speed, range and bearing, squawk;
   - any flags, each with its one-line reason, labelled "indicative" where it is.
3. **List** (swipe left): nearest first, with flag badges. Tap a row to select it and return to
   the radar.
4. **Status** (swipe right):
   - the Pi's address and the age of its last data;
   - the number of aircraft heard, and messages a second from `stats.json`;
   - the Wi-Fi RSSI;
   - the night-mode times and alert settings.

## Fight or Flight flags on the device

The flags must mean the same as the app's. That's why there's a duplicated `NormalizedFlight`
contract, and the same applies here.

- **A generated rules config.** A build script reads fight-or-flight's `src/config/` (`rules.ts`,
  `airports.ts`, `classification.ts`, `calendar.ts`, `corridors.ts`) and the WebTrak swaths
  (`docs/data/webtrak-fab-layers.geojson`, 28 KB) and writes a C header. Changing a threshold
  stays a config edit. The firmware never has its own copy of a number.
- **A C port of `classify.ts` and `rulesEngine.ts`** (about 440 lines of TypeScript):
  - R1 hours, a deterministic breach, which needs UK local time;
  - R2 below the altitude floor and R3 outside the corridors, both indicative, using
    point-in-polygon against the swaths.
- **Routes:** classification is only "confident" with a route. Routes come from the Worker on tap,
  and automatically for very close aircraft (see [Route look-ups](#route-look-ups)). Without one,
  an aircraft is classified by proximity, altitude and callsign, and labelled as such.
- **Time:** SNTP over Wi-Fi, with UK rules for GMT and BST done in C (last Sunday of March and
  October). Bank holidays come from `calendar.ts` through the generated header.
- **Matching the app:** a test that runs the same flights through the TypeScript and the C, on
  the host, and diffs the flags. The Worker's history already holds real flights to use.
- **The wording** keeps the app's framing: "looks outside local rules", indicative, not proof. The
  device never sends a complaint. A later phase could show a QR code that opens the app with the
  aircraft preselected.

## Route look-ups

- **On tap:** the selected aircraft's callsign, if it isn't cached.
- **Automatically:** any aircraft within **5 NM and below 10,000 ft** (both are settings). That
  covers Farnborough's arrivals and departures and anything low overhead, so their flags can be
  "confident" without a tap.
- **Gentle on the Worker:**
  - one look-up in flight at a time;
  - automatic ones at most one every 10 s, with taps going first;
  - answers cached per callsign for the day (a known route) or an hour (none), in RAM, about
    200 entries, lost on restart.
- **HTTPS:** the Worker is on `workers.dev`, so the Presto needs TLS. That means mbedTLS through
  lwIP's `altcp_tls`, with the CA certificate in the firmware and SNTP time to check it. It costs
  roughly 40–60 KB of RAM during a request, which P5 has to find: PSRAM, or a smaller JSON buffer
  once parsing streams.

## Alerts (LEDs only)

The 7 LEDs on the back glow; the piezo is never used.

| Trigger | Default | Colour |
|---|---|---|
| A rule flag (R1 breach) | any | red, steady |
| A rule flag (R2/R3 indicative) | any | amber, steady |
| Low and close | within 2 NM and below 2,000 ft | white, pulsing |
| Helicopter (category A7) | within 3 NM | cyan |
| Specials | squawk 7500/7600/7700, or military | magenta, pulsing |

- The LEDs show the highest-priority alert while it lasts, then fade out after 30 s.
- An alert during night mode wakes the screen dimly; there's a setting to keep night alerts on
  the LEDs only.
- All thresholds live in one config file in the firmware project, with the rule thresholds in the
  generated header.

## Night mode

- The screen dims to a low backlight on a schedule, by default from 23:00 to 07:00 UK time, and
  also when nothing is within 10 NM for 10 minutes.
- A touch wakes it to full brightness for 60 s. An alert wakes it as described above.
- The backlight is a PWM pin (`st7701_set_backlight`), so dimming costs nothing.

## Power

- **Now:** the laptop's USB-C.
- **Later, from the Pi 3B+:** a USB-A to USB-C lead from one of the Pi's ports. The Pi's USB ports
  supply about 1.2 A in total, and the Presto should draw a few hundred mA with the backlight
  up. That's comfortable, as long as the Pi's own supply is strong, which the 2.4 A PoE splitter
  planned in fight-or-flight's RECEIVER-PLAN R3 is. Measure the Presto's real current in P0 with a USB meter.
- If the Presto sits next to the Pi, a data-capable lead could carry the feed over USB serial
  instead of Wi-Fi. That's a possible later option, not v1.
- **Separately powered:** any 5 V USB-C phone charger.

## Phases

- **P0, bring-up:**
  - this repository, built with the toolchain and SDK the BBC project installed
    (`scripts/env.sh`);
  - the panel in palette mode, with the frame rate and memory budget measured;
  - the touch controller read;
  - Wi-Fi up, and `aircraft.json` fetched and parsed;
  - the current draw measured.
- **P1, radar v1:**
  - the radar screen with rings, the house and aircraft arrows, positions from the Pi;
  - the station position from `receiver.json`, zoom, and the stale-data indicator.
- **P2, interaction:** tap to select, the details strip, the list and status screens, and swiping
  between them.
- **P3, map layers:** the runway and airport markers, the WebTrak swaths and the coverage outline,
  all generated from the repo data at build time.
- **P4, flags:**
  - the generated rules header, the C classify and rules engine, SNTP and UK time;
  - the host test that diffs the C against the TypeScript;
  - flag colours on the radar, and reasons in the details strip.
- **P5, routes and alerts:** TLS, `/api/route-lookup` on tap and for close aircraft with a cache,
  the LED alerts, night mode and the settings.
- **P6, polish:**
  - Wi-Fi and Pi-address setup on the screen (borrowing the BBC project's setup flow);
  - settings kept in flash;
  - power from the Pi;
  - optionally a QR code to the app.

## P0 status

Done (2026-10-30):

- The firmware builds with no warnings: `build/presto_air_traffic.uf2`, 370 KB of flash. Static
  RAM is 412 KB of 520 KB:
  - the framebuffer, 225 KB;
  - the JSON buffer, 96 KB;
  - lwIP's buffers, about 52 KB;
  - the aircraft table, 16 KB.
- The `aircraft.json` parser (`src/feed.c`) is checked on the host (`tests/feed_test.c`) against
  the Pi's live file. It finds the same aircraft and positions as Python's reading of it.

To do:

- Flash it and check:
  - the panel: the palette ramps along the bottom should be smooth red, green and blue;
  - touch: a tap changes the range;
  - Wi-Fi and the fetches: the USB status line.
- Measure the frame rate and the current draw.

## Open questions

- Double buffering: a PSRAM back buffer, or redrawing only what changed.
- The rules-header generator reads fight-or-flight's `src/config/`. It could live here, given the
  path to a fight-or-flight checkout, or in fight-or-flight so it tracks config changes there.
- Whether the Worker should offer a slim endpoint that returns precomputed flags. That would keep
  one rules engine, but the device would depend on the internet for flags. For now the plan is
  the C port, so flags work from the Pi alone.
