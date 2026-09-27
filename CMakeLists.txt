cmake_minimum_required(VERSION 3.25)
project(virtual_workbench CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VWB_BUILD_TESTS "Build the unit tests (fetches GoogleTest)" ON)
option(VWB_BUILD_UVSC_SPIKE "Build the Phase 0 UVSC spike (needs Keil AN198)" OFF)

function(vwb_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# Production model: LPC17xx peripherals and the MCB1700 board. See docs/phase1/.
add_library(vwb_model STATIC
    src/lpc17xx/gpio.cpp
    src/lpc17xx/lpc1768.cpp)
target_include_directories(vwb_model PUBLIC src)
vwb_warnings(vwb_model)

add_executable(vwb src/cli/main.cpp)
target_link_libraries(vwb PRIVATE vwb_model)
vwb_warnings(vwb)

if(VWB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Phase 0 spike (throwaway; see docs/phase0/). Its UVSC declarations are generated
# from a local copy of Keil's AN198 (apnt_198.zip or an extracted UVSC_C.h); see
# third_party/uvsc/PROVENANCE.md.
if(VWB_BUILD_UVSC_SPIKE)
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
