cmake_minimum_required(VERSION 3.25)
project(latasim C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LATASIM_BUILD_TESTS "Build the unit tests (fetches GoogleTest)" ON)
option(LATASIM_BUILD_UVSC_SPIKE "Build the Phase 0 UVSC spike (needs Keil AN198)" OFF)

function(latasim_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# Production model: LPC17xx peripherals and the MCB1700 board. See docs/phase1/.
add_library(latasim_model STATIC
    src/lpc17xx/gpio.cpp
    src/lpc17xx/lpc1768.cpp
    src/lpc17xx/keil_gpio_driver.cpp
    src/boards/mcb1700/board.cpp
    src/boards/mcb1700/keil_board_led.cpp
    src/trace/trace.cpp)
target_include_directories(latasim_model PUBLIC src)
latasim_warnings(latasim_model)

# Host-firmware bridge: C-linkage Keil driver functions and the board binding.
add_library(latasim_host STATIC
    src/host/binding.cpp
    src/host/keil_rte_gpio.cpp
    src/host/registers.cpp)
target_link_libraries(latasim_host PUBLIC latasim_model)
# The host LPC17xx.h, for firmware compiled against the host.
target_include_directories(latasim_host PUBLIC src/host/device)
latasim_warnings(latasim_host)

add_executable(latasim src/cli/main.cpp src/cli/gpio_demo.cpp)
target_link_libraries(latasim PRIVATE latasim_model)
latasim_warnings(latasim)

if(LATASIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Phase 0 spike (throwaway; see docs/phase0/). Its UVSC declarations are generated
# from a local copy of Keil's AN198 (apnt_198.zip or an extracted UVSC_C.h); see
# third_party/uvsc/PROVENANCE.md.
if(LATASIM_BUILD_UVSC_SPIKE)
    set(UVSC_SOURCE "${CMAKE_SOURCE_DIR}/third_party/uvsc/apnt_198.zip"
        CACHE FILEPATH "Keil AN198 apnt_198.zip, or an extracted UVSC_C.h")
    if(NOT EXISTS "${UVSC_SOURCE}")
        message(FATAL_ERROR "UVSC_SOURCE not found: ${UVSC_SOURCE}\n"
            "Download Keil AN198 as described in third_party/uvsc/PROVENANCE.md, "
            "or pass -DUVSC_SOURCE=<path to apnt_198.zip or UVSC_C.h>.")
    endif()

    set(UVSC_GENERATED "${CMAKE_BINARY_DIR}/generated/uvsc_min.h")
    add_custom_command(
        OUTPUT "${UVSC_GENERATED}"
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/gen-uvsc-min.ps1"
                -Source "${UVSC_SOURCE}" -Out "${UVSC_GENERATED}"
        DEPENDS "${UVSC_SOURCE}" "${CMAKE_SOURCE_DIR}/scripts/gen-uvsc-min.ps1"
        COMMENT "Generating uvsc_min.h from Keil UVSC_C.h")

    add_executable(uvsc_spike spikes/uvsc/uvsc_spike.cpp "${UVSC_GENERATED}")
    target_include_directories(uvsc_spike PRIVATE "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(uvsc_spike PRIVATE iphlpapi ws2_32)
endif()
