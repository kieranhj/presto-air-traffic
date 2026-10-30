cmake_minimum_required(VERSION 3.13)

# Presto air traffic display (README.md, PLAN.md):
#   . scripts/env.sh && cmake -S . -B build -G Ninja && ninja -C build
#   -> build/presto_air_traffic.uf2

set(PICO_BOARD pimoroni_presto CACHE STRING "Board")
set(PICO_PLATFORM rp2350 CACHE STRING "Platform")
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

include(pico_sdk_import.cmake)
project(presto_air_traffic C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(SRC ${CMAKE_CURRENT_LIST_DIR}/src)

add_executable(presto_air_traffic
        ${SRC}/main.c
        ${SRC}/feed.c
        ${SRC}/gfx.c
        ${SRC}/net.c
        ${SRC}/palette.c
        ${SRC}/radar.c
        ${SRC}/ui.c
        ${SRC}/routes.c
        ${SRC}/roots.c
        ${SRC}/crash.c
        ${SRC}/font_data.c
        ${SRC}/map_data.c
        ${SRC}/drivers/st7701.c
        ${SRC}/drivers/ft6236.c
        ${SRC}/drivers/psram.c
        )
pico_generate_pio_header(presto_air_traffic ${SRC}/drivers/st7701_parallel.pio)
pico_generate_pio_header(presto_air_traffic ${SRC}/drivers/st7701_timing.pio)
pico_generate_pio_header(presto_air_traffic ${SRC}/drivers/st7701_palette.pio)
target_include_directories(presto_air_traffic PRIVATE ${SRC} ${SRC}/drivers)
target_compile_options(presto_air_traffic PRIVATE -Wall -Wextra)
target_compile_definitions(presto_air_traffic PRIVATE
        CYW43_HOST_NAME="presto-air-traffic"
        PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0)
target_link_libraries(presto_air_traffic PRIVATE
        pico_stdlib pico_multicore
        hardware_pio hardware_dma hardware_spi hardware_pwm hardware_i2c hardware_xip_cache hardware_watchdog
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_http
        pico_lwip_mbedtls pico_mbedtls)

pico_enable_stdio_usb(presto_air_traffic 1)
pico_enable_stdio_uart(presto_air_traffic 0)
pico_add_extra_outputs(presto_air_traffic)

# -DPSRAM_RXDELAY=n: force the PSRAM's read delay (testing; normally set from the clock)
set(PSRAM_RXDELAY "" CACHE STRING "Force the PSRAM read delay (testing)")
if (PSRAM_RXDELAY)
    target_compile_definitions(presto_air_traffic PRIVATE PSRAM_RXDELAY=${PSRAM_RXDELAY})
endif()
