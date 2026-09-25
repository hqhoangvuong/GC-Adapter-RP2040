cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

# Clone boards ship with assorted flash chips. The generic 03h boot stage 2
# works with any SPI NOR flash, so the probe boots even when the default
# W25Q080 one would not.
set(PICO_DEFAULT_BOOT_STAGE2 boot2_generic_03h)

include(${CMAKE_CURRENT_LIST_DIR}/../../FW/pico_sdk_import.cmake)

project(rp2040_zero_probe C CXX ASM)

pico_sdk_init()

add_executable(rp2040_zero_probe probe.c)

pico_generate_pio_header(rp2040_zero_probe ${CMAKE_CURRENT_LIST_DIR}/../../FW/pio/ws2812.pio)

target_compile_definitions(rp2040_zero_probe PRIVATE
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_link_libraries(rp2040_zero_probe
    pico_stdlib
    pico_bootrom
    pico_unique_id
    hardware_pio
    hardware_flash
    hardware_clocks
    hardware_sync
)

pico_enable_stdio_usb(rp2040_zero_probe 1)
pico_enable_stdio_uart(rp2040_zero_probe 0)

pico_add_extra_outputs(rp2040_zero_probe)
