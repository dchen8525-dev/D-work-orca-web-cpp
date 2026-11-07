#pragma once

#include <orca/crypto/bytes.hpp>
#include <orca/protocol/versions.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace orca::protocol {

// Plaintext E2EE v2 handshake frames and the validateReady() gate, mirrored
// from D:/work/orca-web/src/gateway/orca/e2ee-v2-client.ts.
//
// Wire order (all WS *text* frames):
//   -> e2ee_hello         (plaintext JSON)
//   <- e2ee_ready         (plaintext JSON; must be strictly validated)
//   -> e2ee_auth          (SEALED, carries transcriptHashB64 + deviceToken)
//   <- e2ee_authenticated(SEALED)
//
// Only the JSON BODIES live here. Sealing them needs crypto_secretbox_easy
// (libsodium, later in P1-b) on top of seal_frame.hpp's byte layout.
//
// NOTE: nlohmann::json sorts object keys while JSON.stringify preserves
// insertion order. That difference is harmless: Orca parses these frames as
// JSON, and the transcript is built from named fields, never from frame text.

struct E2eeContext {
    std::string protocol = kE2eeProtocol;
    std::string initiator = "mobile";
    std::string responder = "desktop";
    std::string transport = "direct";  // "direct" | "relay"
    // Absent => the key is OMITTED, not set to "". sameContext() compares
    // undefined === undefined, so an empty string is a different value.
    std::optional<std::string> relayHostId;
};

nlohmann::json makeE2eeContextJson(const E2eeContext& context);

// Returns nullopt unless both fixed inputs are exactly 32 bytes.
std::optional<nlohmann::json> makeE2eeHello(const orca::crypto::Bytes& clientPublicKey,
                                            const orca::crypto::Bytes& clientNonce,
                                            const E2eeContext& context);

nlohmann::json makeE2eeAuth(const std::string& transcriptHashB64,
                            const std::string& deviceToken);
bool isE2eeAuthenticated(const nlohmann::json& reply,
                         const std::string& transcriptHashB64);

std::string transcriptHashB64(const orca::crypto::Hash256& transcriptHash);

// Field-by-field context equality. relayHostId presence is significant.
bool sameContext(const nlohmann::json& left, const nlohmann::json& right);

struct ReadyResult {
    orca::crypto::Bytes hostPublicKey;  // 32 bytes
    orca::crypto::Bytes hostNonce;      // 32 bytes
};

// The full validateReady() chain:
//   type/v, clientNonceB64 echo, selection.framing + payloadKinds,
//   context equality, strict base64 round-trip + 32-byte lengths,
//   and a CONSTANT-TIME comparison of the desktop key against the pinned key.
// Returns nullopt on any failure. pinnedHostPublicKey must be 32 bytes.
std::optional<ReadyResult> acceptE2eeReady(const nlohmann::json& ready,
                                           const nlohmann::json& hello,
                                           const orca::crypto::Bytes& pinnedHostPublicKey);

}  // namespace orca::protocol
