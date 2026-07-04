# qtamp unit tests. Framework-free: each test is a small executable that
# returns non-zero on failure, registered with CTest. Run with `ctest` in
# the build dir, or the aggregate `qtamp_tests` target.

# Pure-logic units that carry no Qt dependency build and run everywhere,
# including as part of the WebAssembly toolchain image's smoke pass.
add_executable(wavreader_test wavreader_test.cpp)
target_include_directories(wavreader_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME wavreader COMMAND wavreader_test)

add_custom_target(qtamp_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS wavreader_test)
