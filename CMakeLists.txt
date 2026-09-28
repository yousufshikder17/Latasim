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

# The RTOS kernel model (docs/phase5/rtos.md): generic, no microcontroller in it.
add_library(latasim_rtos STATIC src/rtos/fiber.cpp src/rtos/kernel.cpp)
target_include_directories(latasim_rtos PUBLIC src)
latasim_warnings(latasim_rtos)

# Reusable device models outside any one microcontroller or board (the virtual USB
# audio host).
add_library(latasim_devices STATIC src/devices/usb_audio_host.cpp)
target_include_directories(latasim_devices PUBLIC src)
latasim_warnings(latasim_devices)

# Production model: LPC17xx peripherals and the MCB1700 board. See docs/phase1/.
add_library(latasim_model STATIC
    src/lpc17xx/gpio.cpp
    src/lpc17xx/lpc1768.cpp
    src/lpc17xx/systick.cpp
    src/lpc17xx/nvic.cpp
    src/lpc17xx/timer.cpp
    src/lpc17xx/adc.cpp
    src/lpc17xx/eint.cpp
    src/lpc17xx/keil_gpio_driver.cpp
    src/lpc17xx/rtos_port.cpp
    src/lpc17xx/usb_device.cpp
    src/boards/mcb1700/board.cpp
    src/boards/mcb1700/glcd.cpp
    src/boards/mcb1700/keil_board_led.cpp
    src/trace/trace.cpp)
target_include_directories(latasim_model PUBLIC src)
target_link_libraries(latasim_model PUBLIC latasim_rtos)
latasim_warnings(latasim_model)

# Host-firmware bridge: C-linkage Keil driver functions and the board binding.
add_library(latasim_host STATIC
    src/host/binding.cpp
    src/host/keil_rte_gpio.cpp
    src/host/registers.cpp
    src/host/cmsis_system.cpp
    src/host/rtos_binding.cpp)
target_link_libraries(latasim_host PUBLIC latasim_model)
# The host LPC17xx.h, for firmware compiled against the host.
target_include_directories(latasim_host PUBLIC src/host/device)
latasim_warnings(latasim_host)

set(LATASIM_KEIL_PACKS_DIR "$ENV{LOCALAPPDATA}/Arm/Packs" CACHE PATH "Keil/CMSIS pack root")

# CMSIS-RTOS v1 for host firmware, over the RTOS kernel model (docs/phase5/rtos.md),
# compiled against RTX 4's cmsis_os.h from the installed CMSIS pack.
set(LATASIM_CMSIS_RTOS1 "${LATASIM_KEIL_PACKS_DIR}/ARM/CMSIS/5.8.0/CMSIS/RTOS/RTX/INC")
if(EXISTS "${LATASIM_CMSIS_RTOS1}/cmsis_os.h")
    add_library(latasim_cmsis_rtos1 STATIC src/host/cmsis_rtos1.cpp)
    target_include_directories(latasim_cmsis_rtos1 PUBLIC "${LATASIM_CMSIS_RTOS1}")
    target_link_libraries(latasim_cmsis_rtos1 PUBLIC latasim_host)
    latasim_warnings(latasim_cmsis_rtos1)
else()
    message(STATUS "CMSIS-RTOS v1 header not found under ${LATASIM_CMSIS_RTOS1}: RTOS firmware not built")
endif()

# Keil's MCB1700 board-support drivers, compiled unchanged as C from the installed
# packs (docs/phase2/host-firmware.md). Keil sources are never copied into this
# repository, so everything that needs them is built only when the packs exist.
set(LATASIM_KEIL_DFP "${LATASIM_KEIL_PACKS_DIR}/Keil/LPC1700_DFP/2.6.0")
set(LATASIM_KEIL_BOARD_API "${LATASIM_KEIL_PACKS_DIR}/Keil/MDK-Middleware/7.13.0/Board")
set(LATASIM_KEIL_MCB1700 "${LATASIM_KEIL_DFP}/Boards/Keil/MCB1700/Common")
if(EXISTS "${LATASIM_KEIL_MCB1700}/LED_MCB1700.c" AND EXISTS "${LATASIM_KEIL_BOARD_API}/Board_LED.h")
    set(keil_board_drivers
        "${LATASIM_KEIL_MCB1700}/LED_MCB1700.c"
        "${LATASIM_KEIL_MCB1700}/Joystick_MCB1700.c"
        "${LATASIM_KEIL_MCB1700}/Buttons_MCB1700.c")
    set_source_files_properties(${keil_board_drivers} PROPERTIES LANGUAGE C)
    # Vendor code: Latasim's warning policy is not applied.
    add_library(latasim_keil_board_drivers STATIC ${keil_board_drivers})
    target_include_directories(latasim_keil_board_drivers
        PUBLIC "${LATASIM_KEIL_BOARD_API}"          # Keil's own Board_*.h
        PRIVATE "${LATASIM_KEIL_DFP}/RTE_Driver")   # Keil's own GPIO_LPC17xx.h, PIN_LPC17xx.h
    target_compile_definitions(latasim_keil_board_drivers PRIVATE LPC175x_6x)
    # latasim_host supplies the GPIO_*/PIN_* functions and the host LPC17xx.h.
    target_link_libraries(latasim_keil_board_drivers PUBLIC latasim_host)

    # Keil's Blinky_ULp timed example: its IRQ.c (SysTick_Handler) and the board's
    # ADC_MCB1700.c unchanged, plus Latasim's host port of Blinky.c
    # (docs/phase4/overview.md). The ADC driver is compiled as C++ through a wrapper
    # that includes it, since it accesses registers directly.
    set(keil_blinky_ulp_irq "${LATASIM_KEIL_DFP}/Boards/Keil/MCB1700/Blinky_ULp/IRQ.c")
    set_source_files_properties(${keil_blinky_ulp_irq} PROPERTIES LANGUAGE C)
    add_library(latasim_keil_blinky_ulp STATIC
        ${keil_blinky_ulp_irq} src/firmware/blinky_ulp.c src/firmware/adc_mcb1700.cpp)
    target_include_directories(latasim_keil_blinky_ulp PRIVATE
        "${LATASIM_KEIL_MCB1700}"               # ADC_MCB1700.c
        "${LATASIM_KEIL_DFP}/RTE_Driver")       # PIN_LPC17xx.h
    target_link_libraries(latasim_keil_blinky_ulp PUBLIC latasim_keil_board_drivers)

    # Keil's MCB1700 GLCD driver and fonts, unchanged as C, over the host Driver_SPI1
    # (docs/phase4/overview.md). Driver_SPI.h comes from the ARM CMSIS pack.
    set(LATASIM_CMSIS_DRIVER "${LATASIM_KEIL_PACKS_DIR}/ARM/CMSIS/5.8.0/CMSIS/Driver/Include")
    if(EXISTS "${LATASIM_CMSIS_DRIVER}/Driver_SPI.h")
        set(keil_glcd "${LATASIM_KEIL_MCB1700}/GLCD_MCB1700.c" "${LATASIM_KEIL_MCB1700}/GLCD_Fonts.c")
        set_source_files_properties(${keil_glcd} PROPERTIES LANGUAGE C)
        add_library(latasim_keil_glcd STATIC ${keil_glcd} src/firmware/spi1_host.cpp)
        target_include_directories(latasim_keil_glcd
            PUBLIC "${LATASIM_KEIL_MCB1700}"            # GLCD_Config.h
            PRIVATE "${LATASIM_KEIL_DFP}/RTE_Driver" "${LATASIM_CMSIS_DRIVER}")
        target_link_libraries(latasim_keil_glcd PUBLIC latasim_keil_board_drivers)
        # GLCD_DrawChar and GLCD_DrawBitmap cast pointers through uint32_t, which is
        # lossless on the 32-bit target but not on a 64-bit host. Executables that
        # link the driver keep their whole address space below 2 GB instead of
        # changing the source.
        if(MSVC)
            target_link_options(latasim_keil_glcd INTERFACE /LARGEADDRESSAWARE:NO)
        endif()

        # USB audio (docs/phase5/usb-audio.md): Keil's USB device driver and its
        # OTG/pin companion, and the board's DAC driver, unchanged from the pack,
        # with Latasim's representative speaker firmware. The pin configuration is the
        # pack's USB Audio example's RTE_Device.h; RTE_Components.h is Latasim's.
        set(keil_usb_otg "${LATASIM_KEIL_DFP}/RTE_Driver/OTG_LPC17xx.c")
        set_source_files_properties(${keil_usb_otg} PROPERTIES LANGUAGE C COMPILE_DEFINITIONS LPC175x_6x)
        add_library(latasim_keil_usb STATIC
            ${keil_usb_otg}
            src/firmware/usb/usbd_lpc17xx.cpp
            src/firmware/dac_mcb1700.cpp
            src/firmware/usb/usb_speaker.cpp)
        target_include_directories(latasim_keil_usb
            PUBLIC src/firmware/usb
            PRIVATE src/firmware/usb/rte
                    "${LATASIM_KEIL_DFP}/Boards/Keil/MCB1700/Middleware/USB/Device/Audio/RTE/Device/LPC1768"
                    "${LATASIM_KEIL_DFP}/RTE_Driver" "${LATASIM_CMSIS_DRIVER}" "${LATASIM_KEIL_MCB1700}")
        target_link_libraries(latasim_keil_usb PUBLIC latasim_keil_board_drivers)
        set_source_files_properties(src/firmware/usb/usb_speaker.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/permissive-;/utf-8")
    endif()
    # Representative CMSIS-RTOS v1 firmware (docs/phase5/rtos.md): Latasim's own C,
    # against RTX 4's cmsis_os.h and Keil's LED board API.
    if(TARGET latasim_cmsis_rtos1)
        add_library(latasim_rtos_workloads STATIC
            src/firmware/rtos/basic.c src/firmware/rtos/sync.c src/firmware/rtos/realtime.c)
        target_include_directories(latasim_rtos_workloads PUBLIC src/firmware/rtos)
        target_link_libraries(latasim_rtos_workloads PUBLIC latasim_cmsis_rtos1 latasim_keil_board_drivers)
        latasim_warnings(latasim_rtos_workloads)
    endif()
    # The media-center application and the workbench session layer (built-in
    # scenarios) need every firmware piece above.
    if(TARGET latasim_rtos_workloads AND TARGET latasim_keil_usb)
        add_library(latasim_media STATIC src/firmware/media/media_center.cpp)
        target_include_directories(latasim_media PUBLIC src/firmware/media)
        target_link_libraries(latasim_media PUBLIC
            latasim_cmsis_rtos1 latasim_keil_glcd latasim_keil_usb latasim_keil_blinky_ulp)
        latasim_warnings(latasim_media)
        add_library(latasim_workbench STATIC src/workbench/session.cpp src/workbench/scenarios.cpp)
        target_link_libraries(latasim_workbench PUBLIC latasim_media latasim_rtos_workloads latasim_devices)
        latasim_warnings(latasim_workbench)
    endif()
    # Latasim's warning policy for Latasim's file only, not Keil's.
    set_source_files_properties(src/firmware/blinky_ulp.c PROPERTIES COMPILE_OPTIONS "/W4;/WX;/utf-8")
else()
    message(STATUS "Keil packs not found under ${LATASIM_KEIL_PACKS_DIR}: "
                   "Keil firmware tests and `latasim firmware-demo` not built")
endif()

add_executable(latasim src/cli/main.cpp src/cli/gpio_demo.cpp)
target_link_libraries(latasim PRIVATE latasim_model)
if(TARGET latasim_keil_board_drivers)
    target_sources(latasim PRIVATE src/cli/firmware_demo.cpp)
    target_link_libraries(latasim PRIVATE latasim_keil_blinky_ulp)
    target_compile_definitions(latasim PRIVATE LATASIM_HAVE_KEIL_BOARD_DRIVERS)
endif()
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
