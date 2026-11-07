add_library(orca_common STATIC
    src/version.cpp
    src/log.cpp
    src/args.cpp
    src/platform/paths.cpp
    src/crypto/bytes.cpp
    src/crypto/sha256.cpp
    src/crypto/hmac.cpp
    src/crypto/hkdf.cpp
    src/crypto/base64.cpp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/orca/version.hpp
    @ONLY)

target_include_directories(orca_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

# hkdf.cpp reads the E2EE label constants from protocol/include (headers only,
# so this does not create a link cycle between the two libraries).
target_include_directories(orca_common PRIVATE
    ${CMAKE_SOURCE_DIR}/protocol/include)

if(WIN32)
    target_link_libraries(orca_common PUBLIC shell32)
endif()
