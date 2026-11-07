#pragma once

#include <orca/crypto/bytes.hpp>

namespace orca::crypto {

// RFC 2104 HMAC-SHA256 (64-byte block).
Hash256 hmacSha256(const Bytes& key, const Bytes& message);

}  // namespace orca::crypto
