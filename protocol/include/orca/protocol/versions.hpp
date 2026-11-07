#pragma once

#include <cstddef>

// Wire constants mirrored from the TypeScript implementation
// (src/shared/*.ts and src/gateway/orca/e2ee-v2-client.ts).
// These are facts of the protocol, not configuration: P1 must match them
// byte for byte. See docs/PROTOCOL.md.

namespace orca::protocol {

inline constexpr int kLinkProtocolVersion = 1;  // gateway <-> hub
inline constexpr int kWebProtocolVersion = 1;   // browser <-> hub
inline constexpr int kE2eeFrameVersion = 2;     // FRAME_VERSION in sealing

inline constexpr int kSessionIdLength = 32;
inline constexpr int kNonceLength = 24;
inline constexpr int kHeaderLength = kSessionIdLength + 2 + 8;  // + direction + kind + uint64 counter
inline constexpr int kSecretboxMacLength = 16;

inline constexpr char kE2eeProtocol[] = "orca-mobile-e2ee";
inline constexpr char kTranscriptDomain[] = "orca-mobile-e2ee/v2/transcript";

// Node's TextEncoder encodes the trailing NUL into the label bytes. Keep the
// labels as char arrays plus explicit sizes so P1 cannot accidentally drop it
// via std::string(const char*) truncation.
inline constexpr char kSaltLabel[] = "orca-mobile-e2ee/v2/salt\0";
inline constexpr char kInfoLabel[] = "orca-mobile-e2ee/v2/session\0";
inline constexpr std::size_t kSaltLabelSize = sizeof(kSaltLabel) - 1;
inline constexpr std::size_t kInfoLabelSize = sizeof(kInfoLabel) - 1;

inline constexpr char kLinkHelloMethod[] = "hello";

}  // namespace orca::protocol
