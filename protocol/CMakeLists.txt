add_library(orca_protocol STATIC
    src/transcript.cpp
    src/canonical_json.cpp
    src/frames.cpp
    src/capabilities.cpp
    src/seal_frame.cpp
    src/e2ee_handshake.cpp)

target_include_directories(orca_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(orca_protocol PUBLIC orca_common nlohmann_json::nlohmann_json)
