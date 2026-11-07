#pragma once

#include <orca/crypto/bytes.hpp>

#include <string>

namespace orca::protocol {

// The exact hello/ready material hashed into the E2EE v2 transcript.
// All fixed values come from src/gateway/orca/e2ee-v2-client.ts; transport and
// relayHostId are the only variable parts.
struct TranscriptInput {
    orca::crypto::Bytes clientPublicKey;  // 32
    orca::crypto::Bytes clientNonce;      // 32
    orca::crypto::Bytes hostPublicKey;    // 32 (pinned)
    orca::crypto::Bytes hostNonce;        // 32
    std::string transport = "direct";     // "direct" | "relay"
    std::string relayHostId;              // "" when absent
};

// 24 fields, each encoded as [u32 nameLen][name][u32 valueLen][value].
orca::crypto::Bytes encodeTranscript(const TranscriptInput& input);

}  // namespace orca::protocol
