# . scripts/env.sh: the toolchain and Pico SDK to build with, then
#   cmake -S . -B build -G Ninja $PRESTO_CMAKE_ARGS && ninja -C build
#
# By default it uses the ones rp2350-bbc-micro's scripts/setup.sh installed (Arm GNU toolchain
# 14.2, Pico SDK 2.2.0, prebuilt pioasm and picotool on Windows). Set PRESTO_DEPS first to use
# another folder laid out the same way.
: "${PRESTO_DEPS:=C:/Dev/rp2350-bbc-micro-deps}"
export PRESTO_DEPS
export PICO_SDK_PATH=$PRESTO_DEPS/pico-sdk
export PICO_TOOLCHAIN_PATH=$(ls -d "$PRESTO_DEPS"/arm-gnu-toolchain-* | head -1)
export PATH=$(cygpath -u "$PICO_TOOLCHAIN_PATH" 2>/dev/null || echo "$PICO_TOOLCHAIN_PATH")/bin:$PATH
PRESTO_CMAKE_ARGS="-DPICO_SDK_PATH=$PICO_SDK_PATH -DCMAKE_BUILD_TYPE=Release"
for t in pioasm picotool; do
    [ -f "$PRESTO_DEPS/$t/${t}Config.cmake" ] && PRESTO_CMAKE_ARGS="$PRESTO_CMAKE_ARGS -D${t}_DIR=$PRESTO_DEPS/$t"
done
export PRESTO_CMAKE_ARGS
