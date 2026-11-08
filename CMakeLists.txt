cmake_minimum_required(VERSION 3.25)

project(orca_cpp
    VERSION 0.0.1
    DESCRIPTION "C++ rewrite of orca-web: hub, home gateway and a zero-build web client"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global language / runtime settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options (declared before the CRT block below reads ORCA_QT_STATIC)
# ---------------------------------------------------------------------------
option(ORCA_BUILD_SERVER  "Build orca-server (VPS hub)" ON)
option(ORCA_BUILD_GATEWAY "Build orca-gateway (home machine)" ON)
option(ORCA_BUILD_TESTS   "Build unit tests" ON)
# P1+ turns this ON. When ON, a missing Boost/libsodium is a configure error
# instead of a reported warning, so a broken build never reaches packaging.
option(ORCA_REQUIRE_NETWORK_DEPS "Require Boost and libsodium at configure time" OFF)
option(ORCA_SIZE_GATE "Fail the build when a binary exceeds its size budget" ON)
# The gateway's configuration UI is a Qt Widgets window (no loopback HTTP page).
option(ORCA_BUILD_GATEWAY_UI "Build the Qt configuration window into orca-gateway" ON)
# Delivered config: static Qt + /MT -> one self-contained exe. OFF uses a dynamic
# Qt install + /MD and exists only to verify the UI before static Qt is built.
option(ORCA_QT_STATIC "Link Qt statically with the static MSVC CRT" ON)
option(ORCA_REQUIRE_QT "Fail configure when Qt6 Widgets is missing" OFF)

# A self-contained release exe must not depend on the VC++ redistributable.
# The delivered configuration uses static Qt + static CRT (/MT) on Windows.
# ORCA_QT_STATIC=OFF switches the whole tree to /MD so the UI can be compiled
# and smoke-tested against a dynamic Qt install (used for local verification).
if(MSVC)
    if(ORCA_QT_STATIC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

if(NOT DEFINED ORCA_BUILD_VERSION OR ORCA_BUILD_VERSION STREQUAL "")
    set(ORCA_BUILD_VERSION "${PROJECT_VERSION}-dev")
endif()
message(STATUS "orca-cpp build version: ${ORCA_BUILD_VERSION}")

include(cmake/CompilerFlags.cmake)
include(cmake/Dependencies.cmake)

# Live tests live in tests/ (a sibling directory of server/ and gateway/).
# Their `find_package(Threads)` only defines Threads::Threads in that subtree,
# so resolve it once at top level where every target can see it.
find_package(Threads REQUIRED)

# P1+ builds wire frames and canonical JSON. nlohmann-json is header-only and
# already installed in vcpkg / provided by nlohmann-json3-dev on Linux; it is
# the one dependency the project cannot compile without.
if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR
        "nlohmann-json not found. Pass the vcpkg toolchain "
        "(-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake) on Windows, "
        "or install nlohmann-json3-dev on Linux. scripts/build.ps1 does this by default.")
endif()

enable_testing()

add_subdirectory(common)
add_subdirectory(protocol)

if(ORCA_BUILD_SERVER)
    add_subdirectory(server)
endif()
if(ORCA_BUILD_GATEWAY)
    add_subdirectory(gateway)
endif()
if(ORCA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

orca_report_dependencies()
