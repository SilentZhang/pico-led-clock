# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion RISCV_ZCB_RPI_2_2_0_3)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD waveshare_rp2350_one CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(led_visual_clock C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(led_visual_clock
    main.c
    net.c
    rndis_reports.c
    usb_descriptors.c
    dhserver.c
    dnserver.c
)

pico_generate_pio_header(led_visual_clock ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

target_include_directories(led_visual_clock PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(led_visual_clock
    pico_stdlib
    pico_lwip
    pico_lwip_nosys
    tinyusb_device
    tinyusb_board
    hardware_pio
    pico_time
    pico_async_context_threadsafe_background
    m
)

pico_enable_stdio_usb(led_visual_clock 0)
pico_enable_stdio_uart(led_visual_clock 0)

pico_add_extra_outputs(led_visual_clock)
