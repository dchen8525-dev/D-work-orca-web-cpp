add_library(orca_protocol STATIC
    src/transcript.cpp
    src/canonical_json.cpp
    src/frames.cpp
    src/capabilities.cpp
    src/seal_frame.cpp
    src/e2ee_handshake.cpp)

target_include_directories(orca_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(orca_protocol PUBLIC orca_common nlohmann_json::nlohmann_json)

# P1-c: libsodium-backed primitives (X25519 priming, XSalsa20-Poly1305) and the
# counter-managed E2EE session on top of the pure-C++ slices above (the key
# schedule already lives in orca::crypto::deriveE2eeSchedule). Optional so a
# tree without libsodium still builds every P1-b slice; ORCA_REQUIRE_NETWORK_DEPS
# makes its absence fatal for delivered builds.
if(ORCA_HAVE_SODIUM)
    target_sources(orca_protocol PRIVATE
        src/box_crypto.cpp
        src/e2ee_session.cpp)
    target_include_directories(orca_protocol SYSTEM PUBLIC ${ORCA_SODIUM_INCLUDE_DIR})
    target_link_libraries(orca_protocol PUBLIC ${ORCA_SODIUM_LIBRARY})
    target_compile_definitions(orca_protocol PUBLIC ORCA_HAVE_SODIUM=1)
endif()
