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

# P3: hub-side logic. The scrypt gate needs OpenSSL (same requirement as the
# server target); the other two are dependency-free.
if(ORCA_HAVE_OPENSSL)
    add_executable(orca_server_passwords_tests unit/server_passwords_test.cpp)
    target_include_directories(orca_server_passwords_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(orca_server_passwords_tests PRIVATE orca_common)
    add_test(NAME server_passwords COMMAND orca_server_passwords_tests
        ${CMAKE_SOURCE_DIR}/tests/vectors/p3-vectors.json)
endif()

# The hub's routing core and session store are plain C++, so they stay under
# test even though they ship inside the orca-server target.
add_executable(orca_server_hub_core_tests
    unit/server_hub_core_test.cpp
    ${CMAKE_SOURCE_DIR}/server/src/hub_core.cpp)
target_include_directories(orca_server_hub_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/server/include)
target_link_libraries(orca_server_hub_core_tests PRIVATE
    orca_common orca_protocol nlohmann_json::nlohmann_json)
add_test(NAME server_hub_core COMMAND orca_server_hub_core_tests)

add_executable(orca_server_http_tests
    unit/server_http_test.cpp
    ${CMAKE_SOURCE_DIR}/server/src/http_app.cpp
    ${CMAKE_SOURCE_DIR}/server/src/hub_core.cpp
    ${CMAKE_SOURCE_DIR}/server/src/session_store.cpp)
target_include_directories(orca_server_http_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/server/include)
target_link_libraries(orca_server_http_tests PRIVATE
    orca_common orca_protocol nlohmann_json::nlohmann_json)
add_test(NAME server_http COMMAND orca_server_http_tests)

add_executable(orca_server_session_store_tests
    unit/server_session_store_test.cpp
    ${CMAKE_SOURCE_DIR}/server/src/session_store.cpp)
target_include_directories(orca_server_session_store_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/server/include)
target_link_libraries(orca_server_session_store_tests PRIVATE
    orca_common nlohmann_json::nlohmann_json)
add_test(NAME server_session_store COMMAND orca_server_session_store_tests)

# P4: gateway-side hub link. The pure core is dependency-free; the live test
# needs Beast and dials a loopback hub fixture over a real socket.
add_executable(orca_hub_link_core_tests
    unit/hub_link_core_test.cpp
    ${CMAKE_SOURCE_DIR}/gateway/net/hub_link_core.cpp)
target_include_directories(orca_hub_link_core_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/gateway/include)
target_link_libraries(orca_hub_link_core_tests PRIVATE orca_common nlohmann_json::nlohmann_json)
add_test(NAME hub_link_core COMMAND orca_hub_link_core_tests)

if(ORCA_BEAST_INCLUDE_DIR)
    add_executable(orca_hub_client_live_tests unit/hub_client_live_test.cpp
        ${CMAKE_SOURCE_DIR}/gateway/net/hub_link_core.cpp
        ${CMAKE_SOURCE_DIR}/gateway/net/hub_client.cpp
        ${CMAKE_SOURCE_DIR}/gateway/net/tls_roots.cpp)
    target_include_directories(orca_hub_client_live_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/unit
        ${CMAKE_SOURCE_DIR}/gateway/include
        ${ORCA_BEAST_INCLUDE_DIR})
    target_link_libraries(orca_hub_client_live_tests PRIVATE
        orca_common orca_protocol nlohmann_json::nlohmann_json OpenSSL::SSL Threads::Threads)
    if(WIN32)
        # CertOpenSystemStore in gateway/net/tls_roots.cpp.
        target_link_libraries(orca_hub_client_live_tests PRIVATE crypt32)
    endif()
    if(WIN32)
        target_compile_definitions(orca_hub_client_live_tests PRIVATE _WIN32_WINNT=0x0A00)
    endif()
    add_test(NAME hub_client_live COMMAND orca_hub_client_live_tests)
endif()

if(ORCA_BEAST_INCLUDE_DIR AND ORCA_HAVE_SODIUM)
    # P2: the real Orca bridge against a loopback fixture that speaks the E2EE
    # v2 handshake and sealed JSON-RPC.
    add_executable(orca_orca_bridge_live_tests unit/orca_bridge_live_test.cpp
        ${CMAKE_SOURCE_DIR}/gateway/net/orca_rpc_client.cpp
        ${CMAKE_SOURCE_DIR}/gateway/net/orca_bridge_impl.cpp
        ${CMAKE_SOURCE_DIR}/gateway/net/tls_roots.cpp)
    target_include_directories(orca_orca_bridge_live_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/unit
        ${CMAKE_SOURCE_DIR}/gateway/include
        ${ORCA_BEAST_INCLUDE_DIR})
    target_link_libraries(orca_orca_bridge_live_tests PRIVATE
        orca_common orca_protocol nlohmann_json::nlohmann_json OpenSSL::SSL Threads::Threads)
    if(WIN32)
        target_compile_definitions(orca_orca_bridge_live_tests PRIVATE _WIN32_WINNT=0x0A00)
        target_link_libraries(orca_orca_bridge_live_tests PRIVATE crypt32)
    endif()
    add_test(NAME orca_bridge_live COMMAND orca_orca_bridge_live_tests)
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
