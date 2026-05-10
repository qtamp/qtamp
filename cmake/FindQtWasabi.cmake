# SPDX-License-Identifier: MIT
# FindQtWasabi.cmake — locate the qtWasabi source tree.
#
# Resolution order:
#   1. -DQTWASABI_DIR=/path passed on the cmake command line
#   2. $QTWASABI_DIR environment variable
#   3. ${CMAKE_CURRENT_SOURCE_DIR}/deps/qtWasabi
#      (populated by scripts/fetch-deps.sh)
#   4. find_package(qtWasabi) — when qtWasabi eventually ships
#      a CMake config package
#
# The first hit wins.  add_subdirectory()'s the source tree so qtamp
# links against qtWasabi's library target directly without an
# install step.

if(NOT DEFINED QTWASABI_DIR)
    if(DEFINED ENV{QTWASABI_DIR})
        set(QTWASABI_DIR "$ENV{QTWASABI_DIR}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/qtWasabi/CMakeLists.txt")
        set(QTWASABI_DIR "${CMAKE_SOURCE_DIR}/deps/qtWasabi")
    endif()
endif()

if(QTWASABI_DIR AND EXISTS "${QTWASABI_DIR}/CMakeLists.txt")
    message(STATUS "Using qtWasabi source tree at ${QTWASABI_DIR}")
    add_subdirectory("${QTWASABI_DIR}" "${CMAKE_BINARY_DIR}/qtWasabi" EXCLUDE_FROM_ALL)
    set(QTWASABI_FOUND TRUE)
else()
    find_package(qtWasabi CONFIG QUIET)
endif()
