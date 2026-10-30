# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

Firmware for a Pimoroni Presto (RP2350B, 520 KB SRAM, 8 MB PSRAM, 480×480 ST7701 panel, FT6236
touch, CYW43439 Wi-Fi) that shows local air traffic. Its data comes from the Pi receiver's
`aircraft.json` over home Wi-Fi, and it will flag aircraft with the Fight or Flight rules (a
sibling repository, `../fight-or-flight`). PLAN.md has the decisions and phases; README.md has
the build, flash and use.

## Build and check

```sh
. scripts/env.sh && cmake -S . -B build -G Ninja $PRESTO_CMAKE_ARGS && ninja -C build
```

- Builds with `-Wall -Wextra`: keep it warning-free.
- `src/feed.c` (no SDK calls) has a host test, `tests/feed_test.c`, built with MSYS2's gcc
  (`/c/msys64/ucrt64/bin`).
- After a display change, look at the panel itself.

## Rules

- **The repository is public.** The house's position, the Wi-Fi details and LAN addresses never go
  in committed files. They live in git-ignored `src/secrets.h`, or come from the Pi at run time
  (the station position from `receiver.json`).
- **Licence: MIT.** Don't copy code from `rp2350-bbc-micro` (GPLv3); use it for know-how only.
  Pimoroni's code (MIT) and the Pico SDK's (BSD-3) are fine, with their notices kept.
- **Flags stay "indicative, not proof",** as in the app. The rule thresholds come from
  fight-or-flight's `src/config/` through a generated header, never hand-copied numbers.
- **Be gentle on outside services.** The Worker only gets route look-ups on tap and for close
  aircraft, rate-limited and cached. No aggregator is ever polled.
- **Core 1 runs the panel's interrupts** (`st7701_init` is called there), and they run from RAM.
  Anything that writes flash must lock core 1 out first.
