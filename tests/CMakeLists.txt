include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # match the MSVC runtime of our targets
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(vwb_tests
    gpio_test.cpp)
target_link_libraries(vwb_tests PRIVATE vwb_model GTest::gtest_main)
vwb_warnings(vwb_tests)

include(GoogleTest)
gtest_discover_tests(vwb_tests)
