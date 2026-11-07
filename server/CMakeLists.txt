add_executable(orca-server main.cpp)
target_link_libraries(orca-server PRIVATE orca_common orca_protocol)

if(ORCA_SIZE_GATE)
    add_custom_command(TARGET orca-server POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DORCA_TARGET_FILE=$<TARGET_FILE:orca-server>
            -DORCA_MAX_BYTES=8388608
            -P ${CMAKE_SOURCE_DIR}/cmake/SizeReport.cmake
        COMMENT "Checking orca-server against the 8 MiB size budget"
        VERBATIM)
endif()
