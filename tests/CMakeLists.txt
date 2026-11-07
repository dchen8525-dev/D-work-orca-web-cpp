add_executable(orca_smoke_tests unit/smoke_test.cpp)
target_link_libraries(orca_smoke_tests PRIVATE orca_common orca_protocol)
# Version consistency gate: a tag build injects its ref_name into version.hpp.
# The test asserts BOTH the injected string and that it agrees with the
# project() version, so a stale -DORCA_BUILD_VERSION left in CMakeCache.txt
# cannot silently pass while the sources claim a different version.
target_compile_definitions(orca_smoke_tests
    PRIVATE ORCA_EXPECTED_VERSION="${ORCA_BUILD_VERSION}"
            ORCA_PROJECT_VERSION="${PROJECT_VERSION}")

add_executable(orca_crypto_tests unit/crypto_test.cpp)
target_include_directories(orca_crypto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(orca_crypto_tests PRIVATE orca_common orca_protocol)

add_executable(orca_protocol_tests unit/protocol_test.cpp)
target_include_directories(orca_protocol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(orca_protocol_tests PRIVATE orca_common orca_protocol)

# Gateway config/pairing/status logic is plain C++ (no Qt), so it stays under
# test even when static Qt has not been built yet.
add_executable(orca_gateway_core_tests
    unit/gateway_core_test.cpp
    ${CMAKE_SOURCE_DIR}/gateway/ui/config_store.cpp
    ${CMAKE_SOURCE_DIR}/gateway/ui/pairing.cpp
    ${CMAKE_SOURCE_DIR}/gateway/ui/status_model.cpp)
target_include_directories(orca_gateway_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/gateway/ui)
target_link_libraries(orca_gateway_core_tests PRIVATE
    orca_common nlohmann_json::nlohmann_json)

add_test(NAME smoke COMMAND orca_smoke_tests)
add_test(NAME crypto COMMAND orca_crypto_tests)
add_test(NAME gateway_core COMMAND orca_gateway_core_tests)
add_test(NAME protocol COMMAND orca_protocol_tests
    ${CMAKE_SOURCE_DIR}/tests/vectors/p1-vectors.json)

# P1-c: libsodium-backed E2EE v2 sealing, only buildable when sodium is found.
if(ORCA_HAVE_SODIUM)
    add_executable(orca_e2ee_session_tests unit/e2ee_session_test.cpp)
    target_include_directories(orca_e2ee_session_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(orca_e2ee_session_tests PRIVATE orca_protocol)
    add_test(NAME e2ee_session COMMAND orca_e2ee_session_tests
        ${CMAKE_SOURCE_DIR}/tests/vectors/p1c-vectors.json
        ${CMAKE_SOURCE_DIR}/tests/vectors/p1-vectors.json)
endif()

add_test(NAME server_version COMMAND orca-server --version)
set_tests_properties(server_version PROPERTIES
    PASS_REGULAR_EXPRESSION "orca-server ")

add_test(NAME gateway_version COMMAND orca-gateway --version)
set_tests_properties(gateway_version PROPERTIES
    PASS_REGULAR_EXPRESSION "orca-gateway ")

add_test(NAME server_unknown_flag COMMAND orca-server --bogus)
set_tests_properties(server_unknown_flag PROPERTIES
    WILL_FAIL TRUE)
