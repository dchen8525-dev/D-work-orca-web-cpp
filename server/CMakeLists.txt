# The hub is a real network service from P3 on: beast for HTTP/WS, OpenSSL for
# scrypt password hashing, all on the distro/vcpkg packages already declared in
# Dependencies.cmake. No Qt: the hub is headless on every platform.
if(NOT ORCA_HAVE_OPENSSL)
    message(FATAL_ERROR
        "orca-server requires OpenSSL (scrypt password hashing). "
        "Install it via scripts/setup-deps.ps1 / scripts/setup-deps.sh.")
endif()
if(NOT ORCA_BEAST_INCLUDE_DIR)
    message(FATAL_ERROR
        "orca-server requires Boost.Beast headers (boost/beast.hpp). "
        "Install it via scripts/setup-deps.ps1 / scripts/setup-deps.sh.")
endif()

find_package(Threads REQUIRED)

add_executable(orca-server
    main.cpp
    src/server_config.cpp
    src/session_store.cpp
    src/hub_core.cpp
    src/http_app.cpp
    src/server_runner.cpp)

target_include_directories(orca-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ORCA_BEAST_INCLUDE_DIR})

target_link_libraries(orca-server PRIVATE
    orca_common
    orca_protocol
    nlohmann_json::nlohmann_json
    Threads::Threads)

if(WIN32)
    # Boost.Asio needs the Windows version macro before it includes winsocks.
    target_compile_definitions(orca-server PRIVATE _WIN32_WINNT=0x0A00)
endif()

if(ORCA_SIZE_GATE)
    add_custom_command(TARGET orca-server POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DORCA_TARGET_FILE=$<TARGET_FILE:orca-server>
            -DORCA_MAX_BYTES=8388608
            -P ${CMAKE_SOURCE_DIR}/cmake/SizeReport.cmake
        COMMENT "Checking orca-server against the 8 MiB size budget"
        VERBATIM)
endif()
