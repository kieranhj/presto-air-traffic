# Presto air traffic display

Firmware for a [Pimoroni Presto](https://shop.pimoroni.com/products/presto) (RP2350, 480×480
touchscreen, Wi-Fi) that shows local air traffic. It takes its data from our own ADS-B receiver,
a Raspberry Pi running [adsb.im](https://adsb.im), and it will flag aircraft with the
[Fight or Flight](https://github.com/kieranhj/fight-or-flight) rules. Plain C on the Pico SDK.

**Status: P1, the radar.** It shows the Pi's aircraft on a radar over a light OpenStreetMap
background, with trails, labels and zoom. Tapping aircraft and the flags come next. See
[PLAN.md](PLAN.md) for what's coming and why.

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

## Using it

- **The radar** is centred on the receiver (the Pi tells the Presto where it is), with north up,
  over a dim map of roads, the coast, runways and towns.
- **Aircraft** are darts pointing along their track, with a two-minute trail, coloured by
  altitude:
  - orange, below 3,000 ft;
  - yellow, 3,000–10,000 ft;
  - green, 10,000–20,000 ft;
  - blue, above 20,000 ft;
  - grey, on the ground or with no altitude.
- **Labels:** the callsign (or registration), then the altitude in hundreds of feet ("035" is
  3,500 ft, "GND" on the ground), with a small triangle if it's climbing or descending.
- **Top line:** a green dot and "Live" while the data's fresh; yellow while Wi-Fi joins; red, with
  the age, when the Pi hasn't answered for 5 s. The aircraft count is on the right.
- **Tap an aircraft** (on the radar, or a row of the list, which is a swipe right away) for its
  details and route along the bottom. Close them with the ×, a swipe down anywhere on the radar,
  or a tap on empty map. Swipe left for the status screen.
- **− and +** at the bottom right step the range through 5, 10, 20 and 40 NM.
- **USB serial:** every 5 s it reports the frame rate, Wi-Fi, fetches, aircraft, drawing times,
  the PSRAM self-test's error count (should be 0) and taps.

## Test on the host

```sh
gcc -O1 -Wall -Wextra -Isrc tests/feed_test.c src/feed.c -o build-host/feed_test
curl -s http://<pi address>:8080/data/aircraft.json -o aircraft.json
build-host/feed_test aircraft.json
```

## Licence

MIT, see [LICENSE](LICENSE). Some parts keep their own licences:

- **Panel driver:** ported from Pimoroni's MIT-licensed [presto](https://github.com/pimoroni/presto)
  driver (`src/drivers/LICENSE.pimoroni`).
- **PSRAM set-up:** from SparkFun's MIT-licensed
  [sparkfun-pico](https://github.com/sparkfun/sparkfun-pico) (`src/drivers/LICENSE.sparkfun`).
- **Font:** IBM Plex Sans Condensed, under the SIL Open Font License (`tools/fonts/OFL.txt`).
- **Map:** © OpenStreetMap contributors, under the
  [ODbL](https://www.openstreetmap.org/copyright). The radar shows the credit.
  `tools/make_map.py` rebuilds it from one Overpass download, backing off if the server's busy.
- **Board header and `pico_sdk_import.cmake`:** Raspberry Pi's, under BSD-3-Clause.
