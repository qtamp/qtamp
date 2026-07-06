# qtamp unit tests. Framework-free: each test is a small executable that
# returns non-zero on failure, registered with CTest. Run with `ctest` in
# the build dir, or the aggregate `qtamp_tests` target.

# Pure-logic units that carry no Qt dependency build and run everywhere,
# including as part of the WebAssembly toolchain image's smoke pass.
add_executable(wavreader_test wavreader_test.cpp)
target_include_directories(wavreader_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME wavreader COMMAND wavreader_test)

# Networked-player protocol core (docs/OKF-remote.md): pure logic over
# Qt Core, no network, no GUI.
add_executable(remotestate_test remotestate_test.cpp
    ${CMAKE_SOURCE_DIR}/src/remotestate.cpp)
target_include_directories(remotestate_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(remotestate_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
add_test(NAME remotestate COMMAND remotestate_test)

add_executable(ssereader_test ssereader_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ssereader.cpp)
target_include_directories(ssereader_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ssereader_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
add_test(NAME ssereader COMMAND ssereader_test)

add_custom_target(qtamp_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS wavreader_test remotestate_test ssereader_test)
