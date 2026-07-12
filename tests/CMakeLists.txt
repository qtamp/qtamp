# qtamp unit tests. Framework-free: each test is a small executable that
# returns non-zero on failure, registered with CTest. Run with `ctest` in
# the build dir, or the aggregate `qtamp_tests` target.
# (The remote-family and FakeHost tests live in deps/qtWasabi/tests.)

# Pure-logic units that carry no Qt dependency build and run everywhere,
# including as part of the WebAssembly toolchain image's smoke pass.
add_executable(wavreader_test wavreader_test.cpp)
target_include_directories(wavreader_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME wavreader COMMAND wavreader_test)

# Backend-mode integration test: drives `qtamp --backend` over HTTP with
# curl. Requires the built qtamp binary; skipped implicitly if curl is
# absent (the script fails loudly, which CTest reports).
add_test(NAME backend
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/remote/backend_test.sh
            $<TARGET_FILE_DIR:qtamp>)

# Full local sync loop: `qtamp --backend` + a `qtamp --connect` head,
# convergence asserted through the head's --probe. No TS/pylon/browser.
add_test(NAME sync
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/remote/sync_test.sh
            $<TARGET_FILE_DIR:qtamp>)

add_custom_target(qtamp_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS wavreader_test qtamp)
