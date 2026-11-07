add_executable(orca_smoke_tests unit/smoke_test.cpp)
target_link_libraries(orca_smoke_tests PRIVATE orca_common orca_protocol)

add_executable(orca_crypto_tests unit/crypto_test.cpp)
target_include_directories(orca_crypto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(orca_crypto_tests PRIVATE orca_common orca_protocol)

add_executable(orca_protocol_tests unit/protocol_test.cpp)
target_include_directories(orca_protocol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(orca_protocol_tests PRIVATE orca_common orca_protocol)

add_test(NAME smoke COMMAND orca_smoke_tests)
add_test(NAME crypto COMMAND orca_crypto_tests)
add_test(NAME protocol COMMAND orca_protocol_tests
    ${CMAKE_SOURCE_DIR}/tests/vectors/p1-vectors.json)

add_test(NAME server_version COMMAND orca-server --version)
set_tests_properties(server_version PROPERTIES
    PASS_REGULAR_EXPRESSION "orca-server ")

add_test(NAME gateway_version COMMAND orca-gateway --version)
set_tests_properties(gateway_version PROPERTIES
    PASS_REGULAR_EXPRESSION "orca-gateway ")

add_test(NAME server_unknown_flag COMMAND orca-server --bogus)
set_tests_properties(server_unknown_flag PROPERTIES
    WILL_FAIL TRUE)
