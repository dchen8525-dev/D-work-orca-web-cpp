#include <orca/protocol/canonical_json.hpp>

#include <orca/crypto/bytes.hpp>
#include <orca/crypto/sha256.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace orca::protocol {

namespace {

std::string stringifyNumber(const nlohmann::json& value) {
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return value.dump();
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) return "null";
    // JS JSON.stringify(1.0) is "1", nlohmann dumps "1.0".
    if (number == std::floor(number) && std::abs(number) < 1e21 &&
        number >= -9.22e18 && number <= 9.22e18) {
        return std::to_string(static_cast<std::int64_t>(number));
    }
    return value.dump();
}

}  // namespace

std::string canonicalize(const nlohmann::json& value) {
    if (value.is_null() || value.is_discarded()) return "null";
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) return stringifyNumber(value);
    if (value.is_string()) return nlohmann::json(value.get<std::string>()).dump();

    if (value.is_array()) {
        std::string out = "[";
        bool first = true;
        for (const auto& item : value) {
            if (!first) out += ',';
            first = false;
            out += canonicalize(item);
        }
        out += ']';
        return out;
    }

    if (value.is_object()) {
        std::vector<std::pair<std::string, std::string>> entries;
        entries.reserve(value.size());
        for (auto it = value.begin(); it != value.end(); ++it) {
            entries.emplace_back(it.key(), canonicalize(it.value()));
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& left, const auto& right) { return left.first < right.first; });
        std::string out = "{";
        bool first = true;
        for (const auto& entry : entries) {
            if (!first) out += ',';
            first = false;
            out += nlohmann::json(entry.first).dump();
            out += ':';
            out += entry.second;
        }
        out += '}';
        return out;
    }
    return "null";
}

std::string payloadFingerprint(const std::string& method,
                               const std::string& sessionId,
                               const nlohmann::json& fields) {
    nlohmann::json root = nlohmann::json::object();
    root["method"] = method;
    root["sessionId"] = sessionId;
    root["fields"] = fields;
    const std::string canonical = canonicalize(root);
    const auto digest = orca::crypto::sha256(
        reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
    return orca::crypto::hexEncode(digest);
}

}  // namespace orca::protocol
