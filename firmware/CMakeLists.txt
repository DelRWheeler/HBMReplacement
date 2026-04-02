cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(fit7_loadcell C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(fit7_loadcell
    src/main.c
    src/ring_buffer.c
    src/fit7_protocol.c
    src/config_store.c
    pico/hal_pico.c
)

target_include_directories(fit7_loadcell PRIVATE
    src
    config
)

target_link_libraries(fit7_loadcell
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_uart
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_timer
)

# Enable USB output for debug printf, disable UART0 stdio
# (UART1 is used for RS485 comms)
pico_enable_stdio_usb(fit7_loadcell 1)
pico_enable_stdio_uart(fit7_loadcell 0)

# Generate UF2 output
pico_add_extra_outputs(fit7_loadcell)
