# Presto air traffic display

Firmware for a [Pimoroni Presto](https://shop.pimoroni.com/products/presto) (RP2350, 480×480
touchscreen, Wi-Fi) that shows local air traffic. It takes its data from our own ADS-B receiver,
a Raspberry Pi running [adsb.im](https://adsb.im), and it will flag aircraft with the
[Fight or Flight](https://github.com/kieranhj/fight-or-flight) rules. Plain C on the Pico SDK.

**Status: P0, bring-up.** It runs on the board: the panel, touch and Wi-Fi work, and the Pi's
aircraft are drawn as dots on a radar. See [PLAN.md](PLAN.md) for what's coming and why.

## Build

You need the Arm GNU toolchain, the Pico SDK 2.2, CMake and Ninja. `scripts/env.sh` points at the
ones [rp2350-bbc-micro](https://github.com/8bitkick/rp2350-bbc-micro)'s `scripts/setup.sh`
installs (`C:/Dev/rp2350-bbc-micro-deps`); set `PRESTO_DEPS` to use your own folder, laid out the
same way.

```sh
cp src/secrets.example.h src/secrets.h      # your Wi-Fi and the Pi's address; git ignores it
. scripts/env.sh
cmake -S . -B build -G Ninja $PRESTO_CMAKE_ARGS
ninja -C build                              # -> build/presto_air_traffic.uf2
```

## Flash

Hold **BOOT** on the back of the Presto and tap **RESET**. It shows up as a USB drive called
`RP2350`; copy `build/presto_air_traffic.uf2` onto it. To go back to other firmware, flash that
firmware's `.uf2` the same way.

With picotool, which can also reboot this firmware into the bootloader over USB:

```sh
picotool load -f -v -x build/presto_air_traffic.uf2
```

If the board had firmware with a partition table on it, such as rp2350-bbc-micro's dual-boot,
the first load needs `--ignore-partitions`.

## Using it (P0)

- **On the screen:** the radar is centred on the receiver, with north up. Aircraft are dots with a
  heading line, coloured by altitude:
  - orange, below 3,000 ft;
  - yellow, 3,000–10,000 ft;
  - green, 10,000–20,000 ft;
  - blue, above 20,000 ft;
  - grey, on the ground or with no altitude.
- **The square at the top left:** green when the data is fresh, yellow while Wi-Fi is joining,
  red when the Pi hasn't answered for 5 s.
- **Touch:** tap anywhere to change the range, through 5, 10, 20 and 40 NM.
- **The bars along the bottom** are a palette test: smooth red, green and blue ramps.
- **USB serial:** every 5 s it reports the frame rate, Wi-Fi, fetches, aircraft and taps.

## Test on the host

```sh
gcc -O1 -Wall -Wextra -Isrc tests/feed_test.c src/feed.c -o build-host/feed_test
curl -s http://<pi address>:8080/data/aircraft.json -o aircraft.json
build-host/feed_test aircraft.json
```

## Licence

MIT, see [LICENSE](LICENSE). The panel driver is ported from Pimoroni's MIT-licensed
[presto](https://github.com/pimoroni/presto) driver (`src/drivers/LICENSE.pimoroni`). The board
header and `pico_sdk_import.cmake` are Raspberry Pi's, under BSD-3-Clause.
