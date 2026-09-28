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
    trace_test.cpp
    virtual_time_test.cpp
    systick_test.cpp
    nvic_test.cpp
    timer_test.cpp
    timer_firmware_test.cpp
    adc_test.cpp
    eint_test.cpp
    glcd_test.cpp
    ssp_test.cpp
    host/ssp_glcd.cpp
    rtos_kernel_test.cpp
    usb_device_test.cpp
    extensibility_test.cpp
    host/eint0_counter.cpp
    host/timer0_blink.cpp
    scenario.cpp
    scenario_test.cpp
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
target_link_libraries(latasim_tests PRIVATE latasim_host latasim_devices GTest::gtest_main)
target_include_directories(latasim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
latasim_warnings(latasim_tests)

include(GoogleTest)
gtest_discover_tests(latasim_tests)

# Tests that run Keil's own sources, built only when the packs are installed.
if(TARGET latasim_keil_board_drivers)
    add_executable(latasim_keil_firmware_tests
        keil_firmware_test.cpp
        keil_timed_firmware_test.cpp
        keil_scenario_test.cpp
        keil_adc_test.cpp
        scenario.cpp
        firmware_demo_test.cpp
        ${PROJECT_SOURCE_DIR}/src/cli/firmware_demo.cpp)
    target_include_directories(latasim_keil_firmware_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(latasim_keil_firmware_tests PRIVATE latasim_keil_blinky_ulp GTest::gtest_main)
    # The GLCD driver needs the CMSIS Driver headers as well.
    if(TARGET latasim_keil_glcd)
        target_sources(latasim_keil_firmware_tests PRIVATE
            keil_glcd_test.cpp keil_integration_test.cpp host/eint0_counter.cpp)
        target_link_libraries(latasim_keil_firmware_tests PRIVATE latasim_keil_glcd)
    endif()
    # USB audio needs the CMSIS Driver headers (latasim_keil_usb).
    if(TARGET latasim_keil_usb)
        target_sources(latasim_keil_firmware_tests PRIVATE usb_audio_test.cpp)
        target_link_libraries(latasim_keil_firmware_tests PRIVATE latasim_keil_usb latasim_devices)
    endif()
    if(TARGET latasim_workbench)
        target_sources(latasim_keil_firmware_tests PRIVATE workbench_test.cpp)
        target_link_libraries(latasim_keil_firmware_tests PRIVATE latasim_workbench)
    endif()
    # RTOS firmware needs RTX's cmsis_os.h from the CMSIS pack.
    if(TARGET latasim_rtos_workloads)
        target_sources(latasim_keil_firmware_tests PRIVATE rtos_workload_test.cpp)
        target_link_libraries(latasim_keil_firmware_tests PRIVATE latasim_rtos_workloads)
    endif()
    latasim_warnings(latasim_keil_firmware_tests)
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
endif()

# The Qt workbench, offscreen (QT_QPA_PLATFORM=offscreen), with Qt's DLLs on PATH.
if(TARGET latasim_ui)
    add_executable(latasim_ui_tests qt/workbench_ui_test.cpp)
    set_target_properties(latasim_ui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(latasim_ui_tests PRIVATE latasim_ui Qt6::Test)
    latasim_warnings(latasim_ui_tests)
    add_test(NAME WorkbenchUi COMMAND latasim_ui_tests)
    set_tests_properties(WorkbenchUi PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${LATASIM_QT_BIN}")
endif()
