cmake_minimum_required(VERSION 3.16)
project(Qtamp VERSION 0.0.1 LANGUAGES CXX
        DESCRIPTION "Qt-native music player and reference embedder for qtWasabi"
        HOMEPAGE_URL "https://qtamp.org")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Try Qt6 first, fall back to Qt5
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGL OpenGLWidgets Quick Qml Network)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Building with Qt6")
    find_package(Qt6 COMPONENTS DBus QUIET)
    if(Qt6DBus_FOUND)
        message(STATUS "Qt6 DBus found — MPRIS2 media player integration enabled")
    else()
        message(STATUS "Qt6 DBus not found — MPRIS2 disabled (media keys won't work)")
    endif()
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGL Network)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "Building with Qt5 (${Qt5Core_VERSION})")
    if(Qt5Core_VERSION VERSION_LESS "5.12")
        message(FATAL_ERROR "Qt 5.12 or later is required")
    endif()
    find_package(Qt5 COMPONENTS DBus QUIET)
    if(Qt5DBus_FOUND)
        message(STATUS "Qt5 DBus found — MPRIS2 media player integration enabled")
    else()
        message(STATUS "Qt5 DBus not found — MPRIS2 disabled (media keys won't work)")
    endif()
endif()

# qtWasabi — open-source Wasabi/Maki Modern skin engine.
# Optional until M5 (first skinned window).  Run scripts/fetch-deps.sh
# to clone qtWasabi into deps/qtWasabi, then configure with
# -DQTAMP_USE_QTWASABI=ON.
option(QTAMP_USE_QTWASABI "Link qtamp against qtWasabi (Modern skin engine)" OFF)
if(QTAMP_USE_QTWASABI)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQtWasabi.cmake)
    if(QTWASABI_FOUND OR TARGET qtWasabi::qtWasabi OR TARGET qtwasabi)
        message(STATUS "qtWasabi available — Modern skin support enabled")
    else()
        message(WARNING "QTAMP_USE_QTWASABI=ON but qtWasabi not found; run scripts/fetch-deps.sh")
    endif()
endif()

# projectM — Milkdrop-compatible visualization engine (optional)
# Install: sudo apt-get install libprojectm-dev projectm-data
find_library(PROJECTM_LIB projectM)
if(PROJECTM_LIB)
    message(STATUS "libprojectM found — MilkDrop visualizations enabled")
else()
    message(STATUS "libprojectM not found — MilkDrop visualizations disabled")
endif()

# QTAMP_MILKDROP — build the in-AVS-drawer MilkDrop visualizer
# overlay against a VENDORED projectM v4 statically linked into the
# qtamp executable.  We don't use the system libprojectM (Fedora 43
# only packages v3.1.12, whose incomplete HLSL→GLSL translator makes
# many MilkDrop 2.x presets render as solid white).  Bundling v4 as
# a git submodule under deps/projectm and linking it static keeps
# qtamp self-contained, avoids dragging projectM into qtWasabi (which
# stays GL-free), and gets us the much-improved v4 shader pipeline +
# playlist API.
option(QTAMP_MILKDROP "Build the in-AVS-drawer MilkDrop visualizer overlay (vendored projectM v4 static)" OFF)
if(QTAMP_MILKDROP)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/projectm/CMakeLists.txt")
        message(FATAL_ERROR
            "QTAMP_MILKDROP=ON but deps/projectm submodule not "
            "checked out.  Run:  git submodule update --init --recursive")
    endif()
    # projectM v4 build knobs — static libs, only the renderer +
    # playlist modules, no SDL test UI / tests / docs.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(ENABLE_PLAYLIST   ON  CACHE BOOL "" FORCE)
    set(ENABLE_SDL_UI     OFF CACHE BOOL "" FORCE)
    set(ENABLE_DEBUG_PRESETS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING     OFF CACHE BOOL "" FORCE)
    set(BUILD_DOCS        OFF CACHE BOOL "" FORCE)
    set(ENABLE_SYSTEM_GLM OFF CACHE BOOL "" FORCE)
    set(ENABLE_SYSTEM_PROJECTM_EVAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(deps/projectm EXCLUDE_FROM_ALL)
endif()

# ── DuckDB — Media Library index store (DuckDB + Parquet) ───────────
# The Media Library scans the music folder into a DuckDB table persisted
# as Parquet.  DuckDB ships no distro package, so a prebuilt C library is
# vendored under deps/duckdb by scripts/fetch-duckdb.sh.  When absent the
# index compiles to a no-op (empty library) so the build still succeeds.
set(QTAMP_DUCKDB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/duckdb")
if(EXISTS "${QTAMP_DUCKDB_DIR}/libduckdb.so")
    set(QTAMP_DUCKDB_LIB "${QTAMP_DUCKDB_DIR}/libduckdb.so")
elseif(EXISTS "${QTAMP_DUCKDB_DIR}/libduckdb.dylib")
    set(QTAMP_DUCKDB_LIB "${QTAMP_DUCKDB_DIR}/libduckdb.dylib")
endif()
if(QTAMP_DUCKDB_LIB)
    message(STATUS "DuckDB found (${QTAMP_DUCKDB_LIB}) — Media Library index enabled")
else()
    message(STATUS "DuckDB not found (run scripts/fetch-duckdb.sh) — Media Library index disabled")
endif()

set(QTAMP_SOURCES
    src/main.cpp
    src/playerhost.h
    src/audiometa.cpp
    src/medialibraryindex.cpp
    src/eq10dsp.cpp
    src/skinutils.cpp
    src/translator.cpp
    src/bookmarkmanager.cpp
    src/recentfilesmanager.cpp
    src/winampbitmaps.cpp
    src/eqpresets.cpp
    src/modernskinengine.cpp
    src/dialogs.cpp
    src/videowindow.cpp
    src/milkdropwindow.cpp
    src/mpris2.cpp
    src/medialibrarywindow.cpp
    src/playlistwindow.cpp
    src/equalizerwindow.cpp
    src/winampwindow.cpp
)
if(QTAMP_MILKDROP)
    list(APPEND QTAMP_SOURCES src/MilkdropItem.cpp)
endif()

# ── WebAssembly demo player ─────────────────────────────────────────
# QTAMP_WASM builds the in-browser hero player for qtamp.org: the
# QTAMP-branded WinampModernPP skin and a public-domain demo track are
# baked into the binary as Qt resources (wasm/qtamp_wasm.qrc), and the
# player boots straight into that skin.  DuckDB, projectM and DBus are
# off in this configuration.  Built with qt-cmake against the Qt for
# WebAssembly toolchain (see github.com/qtamp/qtamp-wasm-builder).
option(QTAMP_WASM "Build the WebAssembly demo player (bundled skin + demo track)" OFF)
# QTAMP_WASM_REMOTE builds the networked browser HEAD instead: the same
# engine + skin, but no local audio — it boots straight into RemoteHost
# (--connect / the page's ?graphql= query param) and mirrors a remote
# `qtamp --backend`.  The demo track stays out of the qrc; audio plays
# on the backend, the head only renders and sends commands.
option(QTAMP_WASM_REMOTE "Build the WebAssembly remote head (synced UI, no local audio)" OFF)
if(QTAMP_WASM_REMOTE AND NOT QTAMP_WASM)
    set(QTAMP_WASM ON CACHE BOOL "implied by QTAMP_WASM_REMOTE" FORCE)
endif()
if(QTAMP_WASM)
    set(CMAKE_AUTORCC ON)
    if(QTAMP_WASM_REMOTE)
        list(APPEND QTAMP_SOURCES
             ${CMAKE_CURRENT_SOURCE_DIR}/wasm/qtamp_wasm_remote.qrc)
    else()
        list(APPEND QTAMP_SOURCES
             ${CMAKE_CURRENT_SOURCE_DIR}/wasm/qtamp_wasm.qrc)
    endif()
endif()

if(QTAMP_WASM AND QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(qtamp ${QTAMP_SOURCES})
else()
    add_executable(qtamp ${QTAMP_SOURCES})
endif()

if(QTAMP_WASM)
    target_compile_definitions(qtamp PRIVATE QTAMP_WASM)
    if(QTAMP_WASM_REMOTE)
        target_compile_definitions(qtamp PRIVATE QTAMP_REMOTE_ONLY)
    endif()
    # Grow the heap on demand; the skin + decoded demo PCM need headroom.
    target_link_options(qtamp PRIVATE -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB)
    # The Maki dispatch is signature-exact since the (class,method)
    # keystone: callDLF invokes every binding at its registered arity,
    # so EMULATE_FUNCTION_POINTER_CASTS is gone — the class of "function
    # signature mismatch" traps it existed for (including the shade-mode
    # switch script) no longer happens.  Dropping it also removes the
    # WASM_BIGINT=0 rewrite the emulation forced, so Qt's default i64
    # ABI applies again.
    #
    # ASYNCIFY (the classic route to a synchronous QDialog::exec() in
    # the browser) is deliberately NOT enabled: it instruments nearly
    # every function and pushed the -Oz binary from ~24 MiB to 37 MiB,
    # past Cloudflare Pages' 25 MiB per-file limit.  The hero player has
    # no modal dialogs on its critical path, so the size cost is not
    # worth it; if a future feature needs sync suspension, scope it with
    # ASYNCIFY_ONLY or move to JSPI rather than instrumenting the world.
    # Static Qt cannot load QML module plugins at runtime, and the window
    # QML lives in a C++ string the import scanner cannot see. Shell.qml
    # declares the imports (QtQuick, QtQuick.Window) so
    # qt_import_qml_plugins bakes those plugins into the binary.
    qt_add_qml_module(qtamp
        URI QtampShell
        VERSION 1.0
        QML_FILES wasm/Shell.qml
    )
    qt_import_qml_plugins(qtamp)
endif()

if(QTAMP_MILKDROP)
    target_compile_definitions(qtamp PRIVATE QTAMP_WITH_MILKDROP)
    # Static link: projectM v4's renderer (`projectM`) + playlist
    # (`projectM_playlist`) targets added by the deps/projectm
    # subdirectory.  Both export public include dirs via
    # target_include_directories(PUBLIC ...).
    target_link_libraries(qtamp PRIVATE projectM projectM_playlist)
endif()

target_include_directories(qtamp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# DuckDB link + include (when vendored).  BUILD_RPATH points the dev
# binary at deps/duckdb; for install the .so is bundled into lib/qtamp
# which the existing INSTALL_RPATH ($ORIGIN/../lib/qtamp) resolves.
if(QTAMP_DUCKDB_LIB)
    target_compile_definitions(qtamp PRIVATE QTAMP_HAVE_DUCKDB)
    target_include_directories(qtamp PRIVATE ${QTAMP_DUCKDB_DIR})
    target_link_libraries(qtamp PRIVATE ${QTAMP_DUCKDB_LIB})
    set_property(TARGET qtamp APPEND PROPERTY BUILD_RPATH "${QTAMP_DUCKDB_DIR}")
    install(FILES ${QTAMP_DUCKDB_LIB} DESTINATION lib/qtamp)
endif()

target_link_libraries(qtamp PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::MultimediaWidgets
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::Network
)
# Qt6 splits OpenGLWidgets into its own module; Qt5 includes it in Widgets.
# Qt::Quick + Qt::Qml drive the --qml-renderer SkinQuickItem preview.
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(qtamp PRIVATE Qt6::OpenGLWidgets Qt6::Quick Qt6::Qml)
endif()


# qtWasabi link target — define WINAMP_HAVE_WASABIQT so the
# pre-existing #ifdef sites in src/main.cpp activate the SkinView
# bring-up path.  Real qtWasabi::Host implementation lands in M5.
if(QTAMP_USE_QTWASABI)
    # Static engine on WebAssembly: Emscripten shared objects embed their
    # Qt dependencies, which then collide with the executable's static Qt
    # at final link (duplicate QImage/QPainter symbols).
    if(QTAMP_WASM AND TARGET qtwasabi_static)
        target_link_libraries(qtamp PRIVATE qtwasabi_static)
        target_compile_definitions(qtamp PRIVATE WINAMP_HAVE_WASABIQT)
    else()
        foreach(_target qtWasabi::qtWasabi qtwasabi)
            if(TARGET ${_target})
                target_link_libraries(qtamp PRIVATE ${_target})
                target_compile_definitions(qtamp PRIVATE WINAMP_HAVE_WASABIQT)
                break()
            endif()
        endforeach()
    endif()
    # The remote client family + FakeHost (qtWasabi::remote, V3).
    if(TARGET qtwasabi_remote)
        target_link_libraries(qtamp PRIVATE qtwasabi_remote)
    endif()
    # The head framework (qtWasabi::head, V5).
    if(TARGET qtwasabi_head)
        target_link_libraries(qtamp PRIVATE qtwasabi_head)
    endif()
    # The player-protocol sidecar (qtWasabi::serve, V4; needs grpc++).
    if(TARGET qtwasabi_serve)
        target_link_libraries(qtamp PRIVATE qtwasabi_serve)
        target_compile_definitions(qtamp PRIVATE QTAMP_HAVE_SIDECAR)
    endif()
endif()

# Set RPATH so the binary finds bundled libs in /usr/lib/qtamp/
set_target_properties(qtamp PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib/qtamp"
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH_USE_LINK_DIRECTORIES TRUE
)

# Conditionally link projectM if available
if(PROJECTM_LIB)
    target_link_libraries(qtamp PRIVATE ${PROJECTM_LIB})
    target_compile_definitions(qtamp PRIVATE HAVE_PROJECTM)

    # Bundle libprojectM and its non-standard dependencies
    # so target systems don't need libprojectm-dev installed
    # Dynamically discover library paths (works across Debian 11 .so.3 and Ubuntu .so.2)
    get_filename_component(PROJECTM_DIR "${PROJECTM_LIB}" DIRECTORY)
    file(GLOB PROJECTM_SO_FILES "${PROJECTM_DIR}/libprojectM.so*")
    file(GLOB FTGL_SO_FILES "/usr/lib/libftgl.so*" "/usr/lib/x86_64-linux-gnu/libftgl.so*")
    file(GLOB GLEW_SO_FILES "/usr/lib/libGLEW.so*" "/usr/lib/x86_64-linux-gnu/libGLEW.so*")
    if(PROJECTM_SO_FILES)
        install(FILES ${PROJECTM_SO_FILES} DESTINATION lib/qtamp)
    endif()
    if(FTGL_SO_FILES)
        install(FILES ${FTGL_SO_FILES} DESTINATION lib/qtamp)
    endif()
    if(GLEW_SO_FILES)
        install(FILES ${GLEW_SO_FILES} DESTINATION lib/qtamp)
    endif()

    # Bundle projectM presets
    if(EXISTS /usr/share/projectM/presets)
        install(DIRECTORY /usr/share/projectM/presets
                DESTINATION share/qtamp/projectM)
    elseif(EXISTS /usr/share/projectm/presets)
        install(DIRECTORY /usr/share/projectm/presets
                DESTINATION share/qtamp/projectM)
    endif()
    if(EXISTS /usr/share/projectM/config.inp)
        install(FILES /usr/share/projectM/config.inp
                DESTINATION share/qtamp/projectM)
    elseif(EXISTS /usr/share/projectm/config.inp)
        install(FILES /usr/share/projectm/config.inp
                DESTINATION share/qtamp/projectM)
    endif()
endif()

# Conditionally link Qt DBus if available
if(QT_VERSION_MAJOR EQUAL 6 AND Qt6DBus_FOUND)
    target_link_libraries(qtamp PRIVATE Qt6::DBus)
    target_compile_definitions(qtamp PRIVATE QT_DBUS_LIB)
elseif(QT_VERSION_MAJOR EQUAL 5 AND Qt5DBus_FOUND)
    target_link_libraries(qtamp PRIVATE Qt5::DBus)
    target_compile_definitions(qtamp PRIVATE QT_DBUS_LIB)
endif()

install(TARGETS qtamp RUNTIME DESTINATION bin)

# Bundle the qtWasabi engine library next to the binary; the qtamp
# RPATH ($ORIGIN/../lib/qtamp) resolves it from there.  Target-based so
# it picks up the freshly-built .so (with its version symlinks) on a
# clean build, not just when a prebuilt copy happens to exist.
if(TARGET qtwasabi)
    install(TARGETS qtwasabi LIBRARY DESTINATION lib/qtamp)
endif()

# ── Desktop integration ─────────────────────────────────────────────
# Freedesktop .desktop launcher (start menu + "Open With" for audio)
# and the qtamp icon at several sizes.  Generic Exec=`qtamp %U`: with no
# --modern-skin, qtamp opens the saved skin, falling back to
# ~/.winamp/skins/Winamp Modern, so the menu entry lands on the modern
# UI without a hardcoded path.
install(FILES packaging/linux/qtamp.desktop DESTINATION share/applications)
install(FILES packaging/linux/icons/qtamp-256.png
        DESTINATION share/icons/hicolor/256x256/apps RENAME qtamp.png)
install(FILES packaging/linux/icons/qtamp-128.png
        DESTINATION share/icons/hicolor/128x128/apps RENAME qtamp.png)
install(FILES packaging/linux/icons/qtamp-48.png
        DESTINATION share/icons/hicolor/48x48/apps RENAME qtamp.png)

# ──────────────────────────────────────────────
# CPack — generate .tar.gz
# ──────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "qtamp")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt-native music player and reference embedder for qtWasabi")
set(CPACK_PACKAGE_DESCRIPTION "Qtamp is a Qt6-native music player and the reference embedder for qtWasabi, the open-source continuation of Wasabi (Winamp's Modern skin engine). Plays MP3 / FLAC / OGG / Opus / WAV / MIDI; supports Modern Winamp skins via qtWasabi.")
set(CPACK_PACKAGE_VENDOR "Florian Kleber")
set(CPACK_PACKAGE_CONTACT "kleber@snek.at")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://qtamp.org")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Florian Kleber <kleber@snek.at>")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")

# ── Tests ───────────────────────────────────────────────────────────
# QTAMP_TESTS builds the framework-free unit tests (ctest). On by default
# for a normal desktop configure, off for the wasm player (the tests run
# natively in the toolchain image's smoke pass instead).
option(QTAMP_TESTS "Build qtamp unit tests" ON)
if(QTAMP_TESTS AND NOT QTAMP_WASM)
    enable_testing()
    add_subdirectory(tests)
endif()

set(CPACK_GENERATOR "TGZ")
include(CPack)
