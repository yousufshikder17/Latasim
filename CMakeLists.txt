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
    src/lpc17xx/ssp.cpp
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

    # The board's ADC_MCB1700.c unchanged, compiled as C++ through a wrapper that
    # includes it, since it accesses registers directly (docs/phase4/overview.md).
    add_library(latasim_keil_adc STATIC src/firmware/adc_mcb1700.cpp)
    target_include_directories(latasim_keil_adc PRIVATE
        "${LATASIM_KEIL_MCB1700}"               # ADC_MCB1700.c
        "${LATASIM_KEIL_DFP}/RTE_Driver")       # PIN_LPC17xx.h
    target_link_libraries(latasim_keil_adc PUBLIC latasim_keil_board_drivers)

    # Keil's Blinky_ULp timed example: its IRQ.c (SysTick_Handler) unchanged, plus
    # Latasim's host port of Blinky.c, over the ADC driver.
    set(keil_blinky_ulp_irq "${LATASIM_KEIL_DFP}/Boards/Keil/MCB1700/Blinky_ULp/IRQ.c")
    set_source_files_properties(${keil_blinky_ulp_irq} PROPERTIES LANGUAGE C)
    add_library(latasim_keil_blinky_ulp STATIC ${keil_blinky_ulp_irq} src/firmware/blinky_ulp.c)
    target_link_libraries(latasim_keil_blinky_ulp PUBLIC latasim_keil_adc)

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
        add_library(latasim_workbench STATIC src/workbench/session.cpp src/workbench/scenarios.cpp src/workbench/views.cpp)
        target_link_libraries(latasim_workbench PUBLIC latasim_media latasim_rtos_workloads latasim_devices)
        latasim_warnings(latasim_workbench)
    endif()
    # Latasim's warning policy for Latasim's file only, not Keil's.
    set_source_files_properties(src/firmware/blinky_ulp.c PROPERTIES COMPILE_OPTIONS "/W4;/WX;/utf-8")
else()
    message(STATUS "Keil packs not found under ${LATASIM_KEIL_PACKS_DIR}: "
                   "Keil firmware tests and `latasim firmware-demo` not built")
endif()

# Host-compiled firmware from outside this repository (docs/external-firmware.md):
# its C sources compiled as C++ against the host LPC17xx.h, with firmware_shim.h
# force-included; its main() becomes `entry` (C linkage) for a workbench scenario,
# and `<entry>_bind_handlers(mcu)` binds the CMSIS interrupt handlers it defines.
# BOARD_DRIVERS names Keil MCB1700 board drivers it uses (ADC, LED, Joystick,
# Buttons), which Latasim builds from the packs: their Board_*.h headers are seen
# with C linkage and their libraries are linked. Its own warnings are its own:
# Latasim's warning policy is not applied.
set(LATASIM_FIRMWARE_NOP_CYCLES 10 CACHE STRING "Core cycles one __NOP() takes in host-compiled external firmware")
function(latasim_add_host_firmware target entry)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "BOARD_DRIVERS;SOURCES")
    # Each source is compiled through a generated wrapper that includes it inside
    # the firmware's own namespace, so that firmware can share an executable.
    set(wrappers "")
    set(n 0)
    foreach(source IN LISTS arg_SOURCES)
        get_filename_component(source "${source}" ABSOLUTE)
        get_filename_component(stem "${source}" NAME_WE)
        math(EXPR n "${n} + 1")
        set(wrapper "${CMAKE_CURRENT_BINARY_DIR}/${target}_src/${n}_${stem}.cpp")
        file(WRITE "${wrapper}.new" "// Generated by latasim_add_host_firmware: a source of ${entry}, in its namespace.\n"
            "namespace ${entry}_firmware {\n#include \"${source}\"\n}\n")
        file(COPY_FILE "${wrapper}.new" "${wrapper}" ONLY_IF_DIFFERENT)
        list(APPEND wrappers "${wrapper}")
    endforeach()
    set_source_files_properties(${wrappers} PROPERTIES
        COMPILE_OPTIONS "/FI${PROJECT_SOURCE_DIR}/src/host/firmware_shim.h;/utf-8")
    # The exceptions Latasim models, by CMSIS handler name.
    set(handlers
        SysTick_Handler kSysTickIrq TIMER0_IRQHandler kTimer0Irq TIMER1_IRQHandler kTimer0Irq+1
        TIMER2_IRQHandler kTimer0Irq+2 TIMER3_IRQHandler kTimer0Irq+3 EINT0_IRQHandler kEint0Irq
        ADC_IRQHandler kAdcIrq USB_IRQHandler kUsbIrq)
    set(board_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}_board")
    set(driver_libs "")
    set(driver_handlers "")
    foreach(driver IN LISTS arg_BOARD_DRIVERS)
        if(driver STREQUAL "ADC")
            list(APPEND driver_libs latasim_keil_adc)
            list(APPEND driver_handlers ADC_IRQHandler)
        elseif(driver MATCHES "^(LED|Joystick|Buttons)$")
            list(APPEND driver_libs latasim_keil_board_drivers)
        else()
            message(FATAL_ERROR "Unknown board driver ${driver} (Latasim builds ADC, LED, Joystick, Buttons)")
        endif()
        if(NOT TARGET latasim_keil_board_drivers)
            message(FATAL_ERROR "Board driver ${driver} needs the Keil packs")
        endif()
        file(WRITE "${board_dir}/Board_${driver}.h.new"
            "/* Generated by latasim_add_host_firmware: Keil's Board_${driver}.h with C linkage. */\n"
            "#pragma once\nextern \"C\" {\n#include \"${LATASIM_KEIL_BOARD_API}/Board_${driver}.h\"\n}\n")
        file(COPY_FILE "${board_dir}/Board_${driver}.h.new" "${board_dir}/Board_${driver}.h" ONLY_IF_DIFFERENT)
    endforeach()
    set(LATASIM_ENTRY ${entry})
    set(LATASIM_HANDLER_DECLS "")
    set(LATASIM_HANDLER_BINDS "")
    while(handlers)
        list(POP_FRONT handlers name irq)
        string(APPEND LATASIM_HANDLER_DECLS "extern \"C\" void ${entry}_${name}(void);\n"
            "#pragma comment(linker, \"/alternatename:${entry}_${name}=${entry}_absent_handler\")\n")
        set(fallback nullptr)
        if(name IN_LIST driver_handlers)
            string(APPEND LATASIM_HANDLER_DECLS "extern \"C\" void ${name}(void);  // the board driver's\n")
            set(fallback ${name})
        endif()
        string(APPEND LATASIM_HANDLER_BINDS
            "    bind(latasim::lpc17xx::${irq}, ${entry}_${name}, ${fallback});\n")
    endwhile()
    configure_file("${PROJECT_SOURCE_DIR}/src/host/firmware_handlers.cpp.in" "${target}_handlers.cpp" @ONLY)

    add_library(${target} STATIC ${wrappers} "${CMAKE_CURRENT_BINARY_DIR}/${target}_handlers.cpp")
    target_include_directories(${target} BEFORE PRIVATE "${PROJECT_SOURCE_DIR}/src/host/device" "${board_dir}")
    target_compile_definitions(${target} PRIVATE
        LATASIM_FIRMWARE_MAIN=${entry} LATASIM_FIRMWARE_NAMESPACE=${entry}_firmware
        LATASIM_FIRMWARE_FPUTC=${entry}_fputc
        LATASIM_NOP_CYCLES=${LATASIM_FIRMWARE_NOP_CYCLES})
    target_link_libraries(${target} PUBLIC latasim_host ${driver_libs})
    # The handler table tells an absent handler by its address; incremental linking
    # gives the placeholder and the names resolved to it different thunks.
    target_link_options(${target} INTERFACE /INCREMENTAL:NO)
endfunction()

# External firmware folders, each built into the workbench as a scenario of its
# own, all in one executable. A folder's sources are the .c files at its top level
# (or LATASIM_USER_FIRMWARE_SOURCES, with a single folder), and its adapter is
# local/adapters/<folder name>.cmake if there is one (or LATASIM_USER_FIRMWARE_ADAPTER,
# with a single folder). Folder sources are never written.
set(LATASIM_USER_FIRMWARE_DIR "" CACHE STRING "External firmware folders (;-separated) to run as workbench scenarios")
set(LATASIM_USER_FIRMWARE_SOURCES "" CACHE STRING "Sources relative to a single folder (default: its top-level *.c)")
set(LATASIM_USER_FIRMWARE_ADAPTER "" CACHE STRING
    "Adapter script for a single folder (default: local/adapters/<folder name>.cmake if present)")
set(LATASIM_USER_FIRMWARE_BOARD_DRIVERS "" CACHE STRING
    "Keil board drivers the firmware uses: ADC, LED, Joystick, Buttons (an adapter may set this too)")

# For the adapter script: in the external firmware's `file` (relative to its
# folder), replace the exact text `old`, which must occur exactly once, with `new`.
# The original stays untouched: the adapted copy is written to the build tree
# (with #line pointing diagnostics at the original) and compiled instead. A
# replacement that stops matching the original is a configure error.
function(latasim_adapt_source file old new)
    set(original "${LATASIM_ADAPTING_DIR}/${file}")
    set(work "${LATASIM_ADAPTED_DIR}/${file}.new")
    get_property(adapted GLOBAL PROPERTY LATASIM_ADAPTED)
    if(file IN_LIST adapted)
        file(READ "${work}" text)
    else()
        file(READ "${original}" text)
        set(text "#line 1 \"${original}\"\n${text}")
        set_property(GLOBAL APPEND PROPERTY LATASIM_ADAPTED "${file}")
        set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    endif()
    string(FIND "${text}" "${old}" first)
    string(FIND "${text}" "${old}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "Adapter: \"${old}\" does not occur exactly once in ${original}")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    file(WRITE "${work}" "${text}")
endfunction()

if(LATASIM_USER_FIRMWARE_DIR)
    if(NOT TARGET latasim_workbench)
        message(FATAL_ERROR "LATASIM_USER_FIRMWARE_DIR needs the workbench session layer, which needs the Keil packs")
    endif()
    list(LENGTH LATASIM_USER_FIRMWARE_DIR folder_count)
    if(folder_count GREATER 1 AND (LATASIM_USER_FIRMWARE_SOURCES OR LATASIM_USER_FIRMWARE_ADAPTER))
        message(FATAL_ERROR "LATASIM_USER_FIRMWARE_SOURCES and _ADAPTER apply to a single folder; "
                            "with several, give each folder its own local/adapters/<folder name>.cmake")
    endif()
    set(registry "")
    set(names "")
    set(index 0)
    foreach(folder IN LISTS LATASIM_USER_FIRMWARE_DIR)
        math(EXPR index "${index} + 1")
        get_filename_component(user_name "${folder}" NAME)
        if(user_name IN_LIST names)
            message(FATAL_ERROR "Two external firmware folders are both named ${user_name}")
        endif()
        list(APPEND names "${user_name}")
        set(entry latasim_user${index}_main)
        if(LATASIM_USER_FIRMWARE_SOURCES)
            list(TRANSFORM LATASIM_USER_FIRMWARE_SOURCES PREPEND "${folder}/" OUTPUT_VARIABLE user_sources)
        else()
            file(GLOB user_sources CONFIGURE_DEPENDS "${folder}/*.c")
        endif()
        if(NOT user_sources)
            message(FATAL_ERROR "No firmware sources in ${folder}")
        endif()
        # The folder's adapter: the one given (relative paths from this repository),
        # else the git-ignored local/adapters/<folder name>.cmake if there is one.
        # Settings an adapter makes apply to its folder only.
        unset(LATASIM_USER_FIRMWARE_BOARD_DRIVERS)
        set(adapter "${LATASIM_USER_FIRMWARE_ADAPTER}")
        if(NOT adapter AND EXISTS "${PROJECT_SOURCE_DIR}/local/adapters/${user_name}.cmake")
            set(adapter "local/adapters/${user_name}.cmake")
        endif()
        if(adapter)
            get_filename_component(adapter "${adapter}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            message(STATUS "External firmware ${user_name}: adapter ${adapter}")
            set(LATASIM_ADAPTING_DIR "${folder}")
            set(LATASIM_ADAPTED_DIR "${CMAKE_BINARY_DIR}/user_firmware/${user_name}")
            set_property(GLOBAL PROPERTY LATASIM_ADAPTED "")
            include("${adapter}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${adapter}")
            get_property(adapted GLOBAL PROPERTY LATASIM_ADAPTED)
            foreach(file IN LISTS adapted)
                list(FIND user_sources "${folder}/${file}" at)
                if(at EQUAL -1)
                    message(FATAL_ERROR "Adapter: ${file} is not one of the sources of ${user_name}")
                endif()
                set(copy "${LATASIM_ADAPTED_DIR}/${file}")
                file(COPY_FILE "${copy}.new" "${copy}" ONLY_IF_DIFFERENT)  # unchanged: no rebuild
                list(REMOVE_AT user_sources ${at})
                list(APPEND user_sources "${copy}")
            endforeach()
        endif()
        message(STATUS "External firmware ${user_name}: ${user_sources}")
        latasim_add_host_firmware(latasim_user${index}_firmware ${entry}
            BOARD_DRIVERS ${LATASIM_USER_FIRMWARE_BOARD_DRIVERS} SOURCES ${user_sources})
        # Adapted copies still include the firmware's headers from its folder.
        target_include_directories(latasim_user${index}_firmware PRIVATE "${folder}")
        target_link_libraries(latasim_workbench PUBLIC latasim_user${index}_firmware)
        string(APPEND registry "LATASIM_EXTERNAL_FIRMWARE(${entry}, \"${user_name}\", \"${folder}\")\n")
    endforeach()
    # The scenarios to register (workbench/scenarios.cpp).
    file(WRITE "${CMAKE_BINARY_DIR}/user_firmware/external_scenarios.inc.new" "${registry}")
    file(COPY_FILE "${CMAKE_BINARY_DIR}/user_firmware/external_scenarios.inc.new"
                   "${CMAKE_BINARY_DIR}/user_firmware/external_scenarios.inc" ONLY_IF_DIFFERENT)
    target_include_directories(latasim_workbench PRIVATE "${CMAKE_BINARY_DIR}/user_firmware")
    target_compile_definitions(latasim_workbench PRIVATE LATASIM_HAVE_EXTERNAL_FIRMWARE)
endif()

# The Qt 6 desktop workbench (docs/phase5/desktop.md), optional: the model, the CLI
# and the tests build without Qt. Point CMake at Qt with CMAKE_PREFIX_PATH, e.g.
# -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64.
option(LATASIM_BUILD_QT "Build the Qt desktop workbench (needs Qt 6 and the Keil packs)" ON)
if(LATASIM_BUILD_QT AND TARGET latasim_workbench)
    find_package(Qt6 QUIET COMPONENTS Widgets Multimedia Test)
    if(Qt6_FOUND)
        add_library(latasim_ui STATIC
            src/ui/qt/board_panel.cpp src/ui/qt/glcd_view.cpp src/ui/qt/trace_panel.cpp
            src/ui/qt/rtos_panel.cpp src/ui/qt/inspect_panels.cpp src/ui/qt/main_window.cpp)
        set_target_properties(latasim_ui PROPERTIES AUTOMOC ON)
        target_link_libraries(latasim_ui PUBLIC latasim_workbench Qt6::Widgets Qt6::Multimedia)
        # The RTOS API has members named "signals": no Qt keyword macros.
        target_compile_definitions(latasim_ui PUBLIC QT_NO_KEYWORDS)
        latasim_warnings(latasim_ui)
        add_executable(latasim_workbench_app WIN32 src/ui/qt/main.cpp)
        set_target_properties(latasim_workbench_app PROPERTIES OUTPUT_NAME latasim-workbench)
        target_link_libraries(latasim_workbench_app PRIVATE latasim_ui)
        latasim_warnings(latasim_workbench_app)
        get_target_property(LATASIM_QMAKE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(LATASIM_QT_BIN "${LATASIM_QMAKE}" DIRECTORY)
        set(LATASIM_QT_BIN "${LATASIM_QT_BIN}" CACHE INTERNAL "")
        # Copy the Qt runtime next to the executable (build output only).
        add_custom_command(TARGET latasim_workbench_app POST_BUILD
            COMMAND "${LATASIM_QT_BIN}/windeployqt.exe" --no-translations --no-compiler-runtime
                    --verbose 0 "$<TARGET_FILE:latasim_workbench_app>"
            VERBATIM)
    else()
        message(STATUS "Qt 6 not found: the desktop workbench is not built (set CMAKE_PREFIX_PATH)")
    endif()
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
