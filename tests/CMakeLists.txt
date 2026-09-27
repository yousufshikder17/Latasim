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
    gpio_external_input_test.cpp
    mcb1700_test.cpp
    board_inputs_test.cpp
    keil_board_led_test.cpp
    convergence_test.cpp
    gpio_demo_test.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/gpio_demo.cpp)
target_link_libraries(latasim_tests PRIVATE latasim_model GTest::gtest_main)
latasim_warnings(latasim_tests)

include(GoogleTest)
gtest_discover_tests(latasim_tests)
