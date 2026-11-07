# The home gateway is a Qt Widgets application: one process owns the Qt event
# loop (main thread) and the network runtime (worker thread). There is no
# loopback HTTP admin page and no self-update.
#
# ORCA_HAVE_QT=OFF (static Qt not installed yet) falls back to the P0 CLI
# skeleton so server and tests remain buildable. CI/release set ORCA_REQUIRE_QT=ON
# so this fallback can never be packaged by mistake.

# Headers carrying Q_OBJECT must be listed so AUTOMOC scans them.
set(ORCA_GATEWAY_UI_HEADERS
    ui/config_store.hpp
    ui/pairing.hpp
    ui/status_model.hpp
    ui/main_window.hpp)

set(ORCA_GATEWAY_UI_SOURCES
    ${ORCA_GATEWAY_UI_HEADERS}
    ui/config_store.cpp
    ui/pairing.cpp
    ui/status_model.cpp
    ui/main_window.cpp)

if(ORCA_HAVE_QT)
    add_executable(orca-gateway
        main.cpp
        ${ORCA_GATEWAY_UI_SOURCES})

    target_include_directories(orca-gateway PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
    target_link_libraries(orca-gateway PRIVATE
        orca_common
        orca_protocol
        nlohmann_json::nlohmann_json
        Qt6::Widgets)
    set_target_properties(orca-gateway PROPERTIES AUTOMOC ON)

    if(WIN32)
        # Console subsystem on purpose: --version / --help must reach stdout so
        # CI can verify the injected build version. The console window is hidden
        # as soon as the Qt window is shown (main.cpp); the P6 startup VBS hides
        # it from the first frame anyway.
        target_compile_definitions(orca-gateway PRIVATE ORCA_WINDOWS_GUI=1)
    endif()

    if(ORCA_QT_STATIC AND WIN32)
        target_compile_definitions(orca-gateway PRIVATE ORCA_QT_STATIC=1)
        # A static Qt build does not auto-load platform plugins from DLLs;
        # QWindowsIntegrationPlugin must be linked and imported in main.cpp.
        if(TARGET Qt6::QWindowsIntegrationPlugin)
            target_link_libraries(orca-gateway PRIVATE Qt6::QWindowsIntegrationPlugin)
        endif()
        # Static Qt6 Widgets + OpenSSL/libsodium is estimated at 12-20 MiB.
        # 24 MiB is a provisional ceiling; tighten it after the first real build.
        set(ORCA_GATEWAY_MAX_BYTES 25165824)
    else()
        # Dynamic Qt: the DLLs live beside the exe, so the exe itself stays small.
        set(ORCA_GATEWAY_MAX_BYTES 8388608)
    endif()
else()
    add_executable(orca-gateway main.cpp)
    target_link_libraries(orca-gateway PRIVATE orca_common orca_protocol)
    target_compile_definitions(orca-gateway PRIVATE ORCA_NO_QT=1)
    set(ORCA_GATEWAY_MAX_BYTES 8388608)
    message(WARNING
        "[orca] Qt6 Widgets not found: orca-gateway is the CLI skeleton only. "
        "Run scripts/setup-deps.ps1 for static Qt, or pass -DORCA_QT_STATIC=OFF.")
endif()

if(ORCA_SIZE_GATE)
    add_custom_command(TARGET orca-gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DORCA_TARGET_FILE=$<TARGET_FILE:orca-gateway>
            -DORCA_MAX_BYTES=${ORCA_GATEWAY_MAX_BYTES}
            -P ${CMAKE_SOURCE_DIR}/cmake/SizeReport.cmake
        COMMENT "Checking orca-gateway against its size budget"
        VERBATIM)
endif()
