# presto-air-traffic: start here

Firmware for a Pimoroni Presto (RP2350B, 480x480 touchscreen, Wi-Fi), in plain C on the Pico SDK. It shows local air traffic on a radar, from the user's own ADS-B receiver (a Raspberry Pi running adsb.im), and is meant to flag aircraft with the Fight or Flight rules.

| | |
|---|---|
| GitHub | https://github.com/kieranhj/presto-air-traffic (public; never commit `src/secrets.h`) |
| Local path | D:\pico-esp32\mine\presto-air-traffic (moved from C:\Users\khcon\OneDrive\Projects\presto-air-traffic on 2026-11-21) |
| Branch | `main`; other branches: `wip/2026-11-tidy` |
| Last commit | 2026-11-02: Move the clock to the left of the top line, giving way to problem messages |

## What's in here
- `src/`: `main.c`, `feed.c` (aircraft.json parser), `radar.c`, `ui.c`, `gfx.c`, `net.c`, `routes.c`, `crash.c`; generated `font_data.c` / `map_data.c` / `roots.c`; `drivers/` (ST7701 panel and PSRAM, ported from MIT code). `src/secrets.h` (gitignored) holds Wi-Fi and the Pi's address.
- `boards/pimoroni_presto.h`, `pico_sdk_import.cmake`, `CMakeLists.txt`.
- `scripts/env.sh`: points cmake at the toolchain and Pico SDK.
- `tools/`: `make_fonts.py`, `make_map.py` (OpenStreetMap base map), `make_roots.py` (TLS roots).
- `tests/feed_test.c`: host test for `src/feed.c`.

## Build and run
- `make`: firmware build (`. scripts/env.sh`, `cmake -G Ninja`, `ninja`) under Git's sh, giving `build\presto_air_traffic.uf2`. **Right now it stops on purpose**: `build\` was configured at the old OneDrive path and CMake will refuse it. Delete `build\` (gitignored) and run `make` again. A full build into a fresh folder at the new path succeeded on 2026-11-22. Other targets: `make test` (host test with `zig cc`), `make help`.
- Toolchain: Arm GNU 14.2 + Pico SDK 2.2 + pioasm/picotool in D:\pico-esp32\rp2350-bbc-micro-deps (the `env.sh` default now; override with `PRESTO_DEPS`).
- Flash by hand: hold BOOT, tap RESET, copy the `.uf2` to the `RP2350` drive; or `picotool load -f -v -x build/presto_air_traffic.uf2`.

## Local changes
- `scripts/env.sh` (2026-11-22, uncommitted): default `PRESTO_DEPS` changed from `C:/Dev/rp2350-bbc-micro-deps` to `D:/pico-esp32/rp2350-bbc-micro-deps`.
- Uncommitted from before: `README.md` shows as modified, but `git diff` shows no content change (probably line endings). The README still names `C:/Dev/rp2350-bbc-micro-deps`; it was left alone because it was already modified.
- `wip/2026-11-tidy` (pushed): the 2026-11 tidy snapshot (no content differences from `main`).

## Status and next steps
- PLAN.md says P1 (radar + base map) is done and P2 is next. The commits since show P2 work done too (three screens, tap to select, details strip, routes from the Worker over HTTPS, panning, clock). PLAN.md's Progress section needs catching up.
- Still to do (PLAN.md): measure current draw; cut the 34 ms map-layer copy per redraw; P3 map layers (WebTrak swaths, coverage), P4 flags (C port of the rules engine from fight-or-flight's `src/config/`), P5 LED alerts and night mode, P6 on-screen setup.

## See also
- README.md (build, flash, use), PLAN.md, CLAUDE.md.
- D:\misc\mine\fight-or-flight (rules, Worker, receiver plan in docs/RECEIVER-PLAN.md); D:\pico-esp32\third-party\rp2350-bbc-micro (know-how only, GPLv3).
