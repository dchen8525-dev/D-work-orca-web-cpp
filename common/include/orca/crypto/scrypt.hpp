#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <orca/crypto/bytes.hpp>

namespace orca::crypto {

// Password hashing compatible with the original Bun implementation
// (apps/server/src/passwords.ts): "scrypt$N$r$p$salt$hash" with base64 parts,
// N=16384 R=8 P=1 and a 32-byte key. OpenSSL EVP_PBE_scrypt under the hood.
//
// verifyPassword parses that format and compares in constant time, so hashes
// written by the TypeScript server remain valid after the C++ cutover.

std::string hashPassword(std::string_view password);

bool verifyPassword(std::string_view password, std::string_view encoded);

// Exposed for tests: the canonical parameter set of the encoded format.
struct ScryptParams {
    std::uint64_t n = 16384;
    std::uint64_t r = 8;
    std::uint64_t p = 1;
    std::size_t keyLength = 32;
};

// Parses "scrypt$N$r$p$salt$hash"; returns nullopt on any malformed input.
std::optional<ScryptParams> parseScryptEncoded(std::string_view encoded);

}  // namespace orca::crypto
