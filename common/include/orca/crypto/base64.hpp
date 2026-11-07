#pragma once

#include <orca/crypto/bytes.hpp>

namespace orca::crypto {

// Standard base64 with padding. Decode is strict: it rejects non-canonical
// input by re-encoding and comparing, mirroring the TypeScript client's
// decodeBase64OrNull round-trip check.
std::string base64Encode(const std::uint8_t* data, std::size_t length);
std::string base64Encode(const Bytes& value);
bool base64Decode(std::string_view text, Bytes& out);

}  // namespace orca::crypto
