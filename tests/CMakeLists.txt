include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # match the MSVC runtime of our targets
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(latasim_tests
    gpio_test.cpp
    lpc1768_test.cpp
    lpc1768_narrow_test.cpp
    pconp_test.cpp
    gpio_external_input_test.cpp
    mcb1700_test.cpp
    board_inputs_test.cpp
    keil_board_led_test.cpp
    convergence_test.cpp
    host_c_api_test.cpp
    host_registers_test.cpp
    host/gpio_client.c
    gpio_demo_test.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/gpio_demo.cpp)
target_link_libraries(latasim_tests PRIVATE latasim_host GTest::gtest_main)
target_include_directories(latasim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
latasim_warnings(latasim_tests)

include(GoogleTest)
gtest_discover_tests(latasim_tests)

# Keil's MCB1700 board-support drivers, compiled unchanged as C from the installed
# packs and run against Latasim (docs/phase2/host-firmware.md). Keil sources are
# never copied into this repository, so this target is built only when the packs
# are present.
set(LATASIM_KEIL_PACKS_DIR "$ENV{LOCALAPPDATA}/Arm/Packs" CACHE PATH "Keil/CMSIS pack root")
set(LATASIM_KEIL_DFP "${LATASIM_KEIL_PACKS_DIR}/Keil/LPC1700_DFP/2.6.0")
set(LATASIM_KEIL_BOARD_API "${LATASIM_KEIL_PACKS_DIR}/Keil/MDK-Middleware/7.13.0/Board")
set(LATASIM_KEIL_MCB1700 "${LATASIM_KEIL_DFP}/Boards/Keil/MCB1700/Common")
if(EXISTS "${LATASIM_KEIL_MCB1700}/LED_MCB1700.c" AND EXISTS "${LATASIM_KEIL_BOARD_API}/Board_LED.h")
    set(keil_board_drivers
        "${LATASIM_KEIL_MCB1700}/LED_MCB1700.c"
        "${LATASIM_KEIL_MCB1700}/Joystick_MCB1700.c"
        "${LATASIM_KEIL_MCB1700}/Buttons_MCB1700.c")
    set_source_files_properties(${keil_board_drivers} PROPERTIES LANGUAGE C)
    add_executable(latasim_keil_firmware_tests keil_firmware_test.cpp ${keil_board_drivers})
    target_include_directories(latasim_keil_firmware_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/host/device   # host LPC17xx.h, ahead of anything else
        "${LATASIM_KEIL_DFP}/RTE_Driver"        # Keil's own GPIO_LPC17xx.h, PIN_LPC17xx.h
        "${LATASIM_KEIL_BOARD_API}")            # Keil's own Board_*.h
    target_compile_definitions(latasim_keil_firmware_tests PRIVATE LPC175x_6x)
    target_link_libraries(latasim_keil_firmware_tests PRIVATE latasim_host GTest::gtest_main)
    # Our warning policy applies to our file, not to the vendor sources.
    set_source_files_properties(keil_firmware_test.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/permissive-;/utf-8")
    gtest_discover_tests(latasim_keil_firmware_tests)

    # Keil's register-level GPIO driver, compiled unchanged as C++ so its register
    # expressions bind to the host register proxies (docs/phase2/host-registers.md).
    set(keil_gpio_driver "${LATASIM_KEIL_DFP}/RTE_Driver/GPIO_LPC17xx.c")
    set_source_files_properties(${keil_gpio_driver} PROPERTIES LANGUAGE CXX)
    add_executable(latasim_keil_register_tests keil_register_driver_test.cpp ${keil_gpio_driver})
    target_include_directories(latasim_keil_register_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        "${LATASIM_KEIL_DFP}/RTE_Driver")
    target_compile_definitions(latasim_keil_register_tests PRIVATE LPC175x_6x)
    target_link_libraries(latasim_keil_register_tests PRIVATE latasim_host GTest::gtest_main)
    set_source_files_properties(keil_register_driver_test.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX;/permissive-;/utf-8")
    gtest_discover_tests(latasim_keil_register_tests)
else()
    message(STATUS "Keil packs not found under ${LATASIM_KEIL_PACKS_DIR}: Keil firmware tests not built")
endif()
