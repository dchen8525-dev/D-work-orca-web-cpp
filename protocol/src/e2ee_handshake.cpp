#include <orca/protocol/e2ee_handshake.hpp>

#include <orca/crypto/base64.hpp>

#include <utility>

namespace orca::protocol {

using orca::crypto::Bytes;
using orca::crypto::Hash256;

namespace {

bool isNumberTwo(const nlohmann::json& value) {
    return value.is_number() && value == 2;
}

std::optional<std::string> stringField(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) return std::nullopt;
    const auto& value = object.at(key);
    if (!value.is_string()) return std::nullopt;
    return value.get<std::string>();
}

// Mirrors Array.prototype.join(','). The TypeScript check is
//   selection.payloadKinds.join() !== 'text,binary'
// so the exact join semantics matter (null elements become empty strings).
std::string arrayJoin(const nlohmann::json& array) {
    std::string out;
    bool first = true;
    for (const auto& element : array) {
        if (!first) out += ',';
        first = false;
        if (element.is_null()) continue;
        if (element.is_string()) {
            out += element.get<std::string>();
        } else if (element.is_boolean()) {
            out += element.get<bool>() ? "true" : "false";
        } else {
            out += element.dump();
        }
    }
    return out;
}

// decodeFixedBase64() + decodeBase64OrNull(): strict base64 round-trip and an
// exact length. A non-canonical encoding is rejected before any key comparison.
bool strictFixedBase64(const nlohmann::json& value, std::size_t length, Bytes& out) {
    if (!value.is_string()) return false;
    Bytes decoded;
    if (!orca::crypto::base64Decode(value.get<std::string>(), decoded)) return false;
    if (decoded.size() != length) return false;
    out = std::move(decoded);
    return true;
}

}  // namespace

nlohmann::json makeE2eeContextJson(const E2eeContext& context) {
    nlohmann::json out = {
        {"protocol", context.protocol},
        {"initiator", context.initiator},
        {"responder", context.responder},
        {"transport", context.transport},
    };
    if (context.relayHostId) out["relayHostId"] = *context.relayHostId;
    return out;
}

std::optional<nlohmann::json> makeE2eeHello(const Bytes& clientPublicKey,
                                            const Bytes& clientNonce,
                                            const E2eeContext& context) {
    if (clientPublicKey.size() != 32 || clientNonce.size() != 32) return std::nullopt;

    nlohmann::json capabilities = nlohmann::json::object();
    capabilities["framing"] = nlohmann::json::array();
    capabilities["framing"].push_back(kE2eeFrameVersion);
    capabilities["payloadKinds"] = nlohmann::json::array();
    capabilities["payloadKinds"].push_back("text");
    capabilities["payloadKinds"].push_back("binary");

    return nlohmann::json{
        {"type", "e2ee_hello"},
        {"v", kE2eeFrameVersion},
        {"clientPublicKeyB64", orca::crypto::base64Encode(clientPublicKey)},
        {"clientNonceB64", orca::crypto::base64Encode(clientNonce)},
        {"capabilities", std::move(capabilities)},
        {"context", makeE2eeContextJson(context)},
    };
}

nlohmann::json makeE2eeAuth(const std::string& transcriptHash,
                            const std::string& deviceToken) {
    return nlohmann::json{
        {"type", "e2ee_auth"},
        {"v", kE2eeFrameVersion},
        {"transcriptHashB64", transcriptHash},
        {"deviceToken", deviceToken},
    };
}

bool isE2eeAuthenticated(const nlohmann::json& reply,
                         const std::string& transcriptHash) {
    if (!reply.is_object()) return false;
    const auto type = stringField(reply, "type");
    if (!type || *type != "e2ee_authenticated") return false;
    if (!reply.contains("v") || !isNumberTwo(reply.at("v"))) return false;
    const auto echoed = stringField(reply, "transcriptHashB64");
    return echoed && *echoed == transcriptHash;
}

std::string transcriptHashB64(const Hash256& hash) {
    return orca::crypto::base64Encode(hash.data(), hash.size());
}

bool sameContext(const nlohmann::json& left, const nlohmann::json& right) {
    if (!left.is_object() || !right.is_object()) return false;
    for (const char* key : {"protocol", "initiator", "responder", "transport"}) {
        if (!left.contains(key) || !right.contains(key)) return false;
        if (left.at(key) != right.at(key)) return false;
    }
    // undefined === undefined, but undefined !== ''. Presence is significant.
    const bool hasLeft = left.contains("relayHostId");
    const bool hasRight = right.contains("relayHostId");
    if (hasLeft != hasRight) return false;
    if (hasLeft && left.at("relayHostId") != right.at("relayHostId")) return false;
    return true;
}

std::optional<ReadyResult> acceptE2eeReady(const nlohmann::json& ready,
                                           const nlohmann::json& hello,
                                           const Bytes& pinnedHostPublicKey) {
    if (pinnedHostPublicKey.size() != 32) return std::nullopt;
    if (!ready.is_object() || !hello.is_object()) return std::nullopt;

    const auto type = stringField(ready, "type");
    if (!type || *type != "e2ee_ready") return std::nullopt;
    if (!ready.contains("v") || !isNumberTwo(ready.at("v"))) return std::nullopt;

    // String equality, exactly as validateReady() does it (not a decoded compare).
    const auto echo = stringField(ready, "clientNonceB64");
    const auto helloNonce = stringField(hello, "clientNonceB64");
    if (!echo || !helloNonce || *echo != *helloNonce) return std::nullopt;

    if (!ready.contains("selection") || !ready.at("selection").is_object()) return std::nullopt;
    const auto& selection = ready.at("selection");
    if (!selection.contains("framing") || !isNumberTwo(selection.at("framing"))) {
        return std::nullopt;
    }
    if (!selection.contains("payloadKinds") || !selection.at("payloadKinds").is_array() ||
        arrayJoin(selection.at("payloadKinds")) != "text,binary") {
        return std::nullopt;
    }

    if (!ready.contains("context") || !hello.contains("context")) return std::nullopt;
    if (!sameContext(ready.at("context"), hello.at("context"))) return std::nullopt;

    if (!ready.contains("desktopPublicKeyB64") || !ready.contains("desktopNonceB64")) {
        return std::nullopt;
    }
    ReadyResult result;
    if (!strictFixedBase64(ready.at("desktopPublicKeyB64"), 32, result.hostPublicKey)) {
        return std::nullopt;
    }
    if (!strictFixedBase64(ready.at("desktopNonceB64"), 32, result.hostNonce)) {
        return std::nullopt;
    }
    // Pinned host key must match in constant time: this is the MITM defence.
    if (!orca::crypto::constantTimeEqual(result.hostPublicKey, pinnedHostPublicKey)) {
        return std::nullopt;
    }
    return result;
}

}  // namespace orca::protocol
