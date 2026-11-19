# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Pico W (RP2040 + CYW43439). Without this the wireless chip and LED do not work.
set(PICO_BOARD pico_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

# Generate compile_commands.json for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(skisim_client C CXX ASM)

pico_sdk_init()

# Code shared with the server (protocol, lwIP config, later the IMU and filter)
set(SKI_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR}/shared)

add_executable(skisim_client
    src/main.c
    src/net_client.c
    ${SKI_SHARED_DIR}/mpu6050.c
    ${SKI_SHARED_DIR}/tilt_filter.c
)

target_include_directories(skisim_client PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${SKI_SHARED_DIR}          # also where lwIP finds lwipopts.h
)

target_link_libraries(skisim_client
    pico_stdlib
    pico_bootsel_via_double_reset
    pico_cyw43_arch_lwip_threadsafe_background
    pico_multicore
    hardware_i2c
)

# Serial output on both USB and UART0 (GP0 TX / GP1 RX, 115200 baud)
pico_enable_stdio_usb(skisim_client 1)
pico_enable_stdio_uart(skisim_client 1)

pico_add_extra_outputs(skisim_client)

# ------------------------------------------------------------------------
# Standalone hardware check: LED, WiFi scan, I2C scan, raw MPU6050 readout.
# Flash build/skisim_hw_check.uf2 to test the client board on its own.
add_executable(skisim_hw_check test/hw_check.c)
target_include_directories(skisim_hw_check PRIVATE ${SKI_SHARED_DIR})
target_link_libraries(skisim_hw_check
    pico_stdlib
    pico_bootsel_via_double_reset
    pico_cyw43_arch_none
    hardware_i2c
)
pico_enable_stdio_usb(skisim_hw_check 1)
pico_enable_stdio_uart(skisim_hw_check 1)
pico_add_extra_outputs(skisim_hw_check)
