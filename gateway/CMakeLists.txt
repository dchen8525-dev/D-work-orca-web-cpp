add_executable(orca-gateway main.cpp)
target_link_libraries(orca-gateway PRIVATE orca_common orca_protocol)

if(ORCA_SIZE_GATE)
    add_custom_command(TARGET orca-gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DORCA_TARGET_FILE=$<TARGET_FILE:orca-gateway>
            -DORCA_MAX_BYTES=8388608
            -P ${CMAKE_SOURCE_DIR}/cmake/SizeReport.cmake
        COMMENT "Checking orca-gateway against the 8 MiB size budget"
        VERBATIM)
endif()
