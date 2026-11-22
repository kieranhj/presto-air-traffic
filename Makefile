# Wrapper so that `make` works from PowerShell / Windows Terminal.
# GNU make runs recipes with cmd.exe here (no sh on PATH), so the build runs
# under Git's sh: . scripts/env.sh, then cmake + ninja as in README.md.
# Flashing is left to you (see `make help`).

SH ?= C:\Program Files\Git\bin\sh.exe
BUILD ?= build

.PHONY: all firmware test help
all: firmware

firmware:
	@"$(SH)" -c "if [ -f $(BUILD)/CMakeCache.txt ] && grep -q OneDrive/Projects/presto-air-traffic $(BUILD)/CMakeCache.txt; then echo $(BUILD)/ was configured at the old OneDrive path and CMake will refuse it: delete the $(BUILD) folder, it is gitignored, then run make again.; exit 1; fi; . scripts/env.sh; if [ ! -d $$PICO_SDK_PATH ]; then echo Pico SDK environment not set up: expected the toolchain and Pico SDK in $$PRESTO_DEPS, as installed by rp2350-bbc-micro scripts/setup.sh. Set PRESTO_DEPS to such a folder. See START_HERE.md.; exit 1; fi; if [ ! -f src/secrets.h ]; then echo Copy src/secrets.example.h to src/secrets.h and fill in Wi-Fi and the Pi address first.; exit 1; fi; cmake -S . -B $(BUILD) -G Ninja $$PRESTO_CMAKE_ARGS && ninja -C $(BUILD)"

# Host test of src/feed.c (README uses MSYS2 gcc; zig cc is on PATH here).
test:
	zig cc -O1 -Wall -Wextra -Isrc tests/feed_test.c src/feed.c -o build-host/feed_test.exe
	@echo Built build-host\feed_test.exe; run it on an aircraft.json saved from the Pi (see README.md).

help:
	@echo make         - firmware: build\presto_air_traffic.uf2 (Arm GNU toolchain + Pico SDK via scripts/env.sh)
	@echo make test    - build the host test build-host\feed_test.exe
	@echo make help    - this list
	@echo Flash by hand: hold BOOT, tap RESET, copy build\presto_air_traffic.uf2 to the RP2350 drive,
	@echo or picotool load -f -v -x build/presto_air_traffic.uf2
