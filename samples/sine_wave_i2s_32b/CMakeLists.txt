cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

set(project_name "sine_wave_i2s_32b" C CXX ASM)
project(${project_name})
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(../../src/pico_audio_32b pico_audio_32b)
add_subdirectory(../.. pico_audio_i2s_32b)

set(bin_name ${PROJECT_NAME})
add_executable(${PROJECT_NAME}
    main.cpp
    ringbuf.c
    project_audio.cpp
    tcp_server.c
)

target_include_directories(${bin_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
)

pico_enable_stdio_usb(${bin_name} 1)
pico_enable_stdio_uart(${bin_name} 1)

target_link_libraries(${bin_name} PRIVATE
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_audio_32b
    pico_audio_i2s_32b
    lwip
    lwip_mdns
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_AUDIO_I2S_DMA_CHANNEL=4
)

pico_add_extra_outputs(${bin_name})
