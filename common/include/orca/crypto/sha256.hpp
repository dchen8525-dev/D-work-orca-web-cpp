#pragma once

#include <orca/crypto/bytes.hpp>

namespace orca::crypto {

// FIPS 180-4 SHA-256. Implemented locally so P1 does not link OpenSSL:
// the installed vcpkg OpenSSL is x64-windows (dynamic CRT) and would clash
// with the project's /MT static runtime.
Hash256 sha256(const std::uint8_t* data, std::size_t length);
Hash256 sha256(const Bytes& data);
Hash256 sha256(std::string_view data);

}  // namespace orca::crypto
