# Dependency resolution for both hosts:
#   Windows -> vcpkg (x64-windows-static for the delivered static-Qt gateway)
#   Linux   -> distribution packages via apt (no Qt; server has no GUI)
#
# P0 targets link only the C++ standard library, so missing network libraries
# are reported, not fatal. P1+ sets ORCA_REQUIRE_NETWORK_DEPS=ON.
# Qt6 Widgets is Windows-only and only needed by ORCA_BUILD_GATEWAY_UI; set
# ORCA_REQUIRE_QT=ON in CI/release so a missing Qt never slips into packaging.

find_package(OpenSSL QUIET)
find_package(nlohmann_json CONFIG QUIET)

set(ORCA_HAVE_QT OFF)
if(ORCA_BUILD_GATEWAY_UI AND WIN32)
    # The static triplet installs no Qt6*Tools packages; the build resolves
    # host tools (moc, rcc) from the dynamic host triplet install, which can
    # be a different Qt patch version (e.g. host 6.11.2 for a 6.11.3 static
    # port). Qt's package version check rejects that cross-patch mix; moc and
    # rcc are compatible across patch releases, so relax the check here. It is
    # a no-op when host and target Qt versions already match (CI).
    set(QT_NO_PACKAGE_VERSION_CHECK ON)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND AND TARGET Qt6::Widgets)
        set(ORCA_HAVE_QT ON)
    endif()
endif()
if(NOT WIN32)
    # The VPS hub is headless; the Qt option is silently ignored on Linux so a
    # Linux build of the whole tree never tries to find a GUI toolkit.
    set(ORCA_HAVE_QT OFF)
endif()

# Boost.Beast is header-only; checking the header avoids CMake 4.x's removed
# FindBoost module (CMP0167) and works identically for vcpkg and apt installs.
find_path(ORCA_BEAST_INCLUDE_DIR
    NAMES boost/beast.hpp
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include)

find_path(ORCA_SODIUM_INCLUDE_DIR
    NAMES sodium.h
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include)
find_library(ORCA_SODIUM_LIBRARY
    NAMES sodium libsodium
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib)

set(ORCA_HAVE_OPENSSL OFF)
set(ORCA_HAVE_JSON OFF)
set(ORCA_HAVE_BOOST OFF)
set(ORCA_HAVE_SODIUM OFF)

if(OpenSSL_FOUND)
    set(ORCA_HAVE_OPENSSL ON)
endif()
if(nlohmann_json_FOUND)
    set(ORCA_HAVE_JSON ON)
endif()
if(ORCA_BEAST_INCLUDE_DIR)
    set(ORCA_HAVE_BOOST ON)
endif()
if(ORCA_SODIUM_INCLUDE_DIR AND ORCA_SODIUM_LIBRARY)
    set(ORCA_HAVE_SODIUM ON)
endif()

# Interface target that P1+ links. Defined here so every consumer sees the
# same include dirs / link flags on both platforms.
add_library(orca_network INTERFACE)
if(ORCA_HAVE_OPENSSL)
    target_link_libraries(orca_network INTERFACE OpenSSL::Crypto OpenSSL::SSL)
endif()
if(ORCA_HAVE_JSON)
    target_link_libraries(orca_network INTERFACE nlohmann_json::nlohmann_json)
endif()
if(ORCA_HAVE_BOOST)
    target_include_directories(orca_network INTERFACE ${ORCA_BEAST_INCLUDE_DIR})
    target_compile_definitions(orca_network INTERFACE ORCA_HAVE_BEAST=1)
endif()
if(ORCA_HAVE_SODIUM)
    target_include_directories(orca_network INTERFACE ${ORCA_SODIUM_INCLUDE_DIR})
    target_link_libraries(orca_network INTERFACE ${ORCA_SODIUM_LIBRARY})
    target_compile_definitions(orca_network INTERFACE ORCA_HAVE_SODIUM=1)
endif()
if(WIN32)
    target_link_libraries(orca_network INTERFACE ws2_32 crypt32)
endif()

function(orca_report_dependencies)
    message(STATUS "-------- orca-cpp dependency status --------")
    message(STATUS "  OpenSSL          : ${ORCA_HAVE_OPENSSL}")
    message(STATUS "  nlohmann-json    : ${ORCA_HAVE_JSON}")
    message(STATUS "  Boost (Beast)    : ${ORCA_HAVE_BOOST}")
    message(STATUS "  libsodium        : ${ORCA_HAVE_SODIUM}")
    message(STATUS "  Qt6 Widgets      : ${ORCA_HAVE_QT} (static=${ORCA_QT_STATIC}, require=${ORCA_REQUIRE_QT})")
    message(STATUS "  require network  : ${ORCA_REQUIRE_NETWORK_DEPS}")
    message(STATUS "-------------------------------------------")
    if(ORCA_REQUIRE_NETWORK_DEPS)
        if(NOT ORCA_HAVE_OPENSSL OR NOT ORCA_HAVE_JSON OR NOT ORCA_HAVE_BOOST OR NOT ORCA_HAVE_SODIUM)
            message(FATAL_ERROR
                "ORCA_REQUIRE_NETWORK_DEPS=ON but a network dependency is missing. "
                "Run scripts/setup-deps.ps1 (Windows) or scripts/setup-deps.sh (Linux).")
        endif()
    endif()
    if(ORCA_REQUIRE_QT AND ORCA_BUILD_GATEWAY_UI AND WIN32)
        if(NOT ORCA_HAVE_QT)
            message(FATAL_ERROR
                "ORCA_REQUIRE_QT=ON but Qt6 Widgets was not found. "
                "Run scripts/setup-deps.ps1 (x64-windows-static) to build static Qt, "
                "or pass -DORCA_QT_STATIC=OFF to verify against a dynamic Qt install.")
        endif()
    endif()
endfunction()
