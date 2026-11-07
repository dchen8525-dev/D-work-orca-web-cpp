#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace orca::protocol {

// Byte-for-byte equivalent of apps/web/src/envelope.ts canonicalize():
// sorted keys, undefined dropped, compact JSON. The browser computes the
// mutation fingerprint; C++ only needs this for parity vectors and local tools.
//
// Known limits (documented, not silently handled): JS compares object keys by
// UTF-16 code unit while std::string compares UTF-8 bytes, and JSON.stringify
// formats very large / exponential numbers differently. Real envelope fields
// are ASCII keys with string/int/bool/null values, where both agree.
std::string canonicalize(const nlohmann::json& value);

std::string payloadFingerprint(const std::string& method,
                               const std::string& sessionId,
                               const nlohmann::json& fields);

}  // namespace orca::protocol
