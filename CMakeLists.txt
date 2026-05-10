cmake_minimum_required(VERSION 3.16)
project(Qtamp VERSION 0.0.1 LANGUAGES CXX
        DESCRIPTION "Qt-native music player and reference embedder for qtWasabi"
        HOMEPAGE_URL "https://qtamp.org")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Try Qt6 first, fall back to Qt5
find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGL OpenGLWidgets)
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
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets OpenGL)
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
    if(QTWASABI_FOUND OR TARGET WasabiQT::WasabiQT OR TARGET wasabiqt)
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

add_executable(qtamp
    src/main.cpp
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

target_include_directories(qtamp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(qtamp
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::MultimediaWidgets
    Qt${QT_VERSION_MAJOR}::OpenGL
)
# Qt6 splits OpenGLWidgets into its own module; Qt5 includes it in Widgets
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(qtamp Qt6::OpenGLWidgets)
endif()


# qtWasabi link target — define WINAMP_HAVE_WASABIQT so the
# pre-existing #ifdef sites in src/main.cpp activate the SkinView
# bring-up path.  Real WasabiQt::Host implementation lands in M5.
if(QTAMP_USE_QTWASABI)
    foreach(_target WasabiQT::WasabiQT wasabiqt)
        if(TARGET ${_target})
            target_link_libraries(qtamp ${_target})
            target_compile_definitions(qtamp PRIVATE WINAMP_HAVE_WASABIQT)
            break()
        endif()
    endforeach()
endif()

# Set RPATH so the binary finds bundled libs in /usr/lib/qtamp/
set_target_properties(qtamp PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib/qtamp"
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH_USE_LINK_DIRECTORIES TRUE
)

# Conditionally link projectM if available
if(PROJECTM_LIB)
    target_link_libraries(qtamp ${PROJECTM_LIB})
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
    target_link_libraries(qtamp Qt6::DBus)
    target_compile_definitions(qtamp PRIVATE QT_DBUS_LIB)
elseif(QT_VERSION_MAJOR EQUAL 5 AND Qt5DBus_FOUND)
    target_link_libraries(qtamp Qt5::DBus)
    target_compile_definitions(qtamp PRIVATE QT_DBUS_LIB)
endif()

install(TARGETS qtamp RUNTIME DESTINATION bin)

# Skin / language / icon resources lived under skins/, lang/, and
# packaging/ in the upstream Winamp port.  Those paths originated
# in the WCL-licensed Llama Group source release and were stripped
# from Qtamp's git history; restoring them is a future milestone
# (Qtamp will ship its own MIT-licensed icon set + .desktop file
# when the packaging pass lands).

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

set(CPACK_GENERATOR "TGZ")
include(CPack)
