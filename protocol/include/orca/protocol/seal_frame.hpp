#pragma once

#include <orca/crypto/bytes.hpp>
#include <orca/protocol/versions.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace orca::protocol {

// Byte geometry of a sealed E2EE v2 frame, mirrored from encodeHeader /
// encodeNonce / writeUint64 in
//   D:/work/orca-web/src/gateway/orca/e2ee-v2-client.ts
//
// This module deliberately does NO cryptography: it only describes bytes, so
// it stays byte-tested without touching libsodium. Sealing (crypto_secretbox_easy)
// lives in e2ee_session.hpp on top of this layout; the header/nonce contract
// is shared by both.
//
//   frame     = nonce(24) || secretbox(header(42) || plaintext)
//   header    = sessionId(32) || direction(1) || kind(1) || uint64BE(counter)
//   nonce     = sessionId[0:12] || 0x02 || direction || kind || 0x00
//                              || uint64BE(counter)
//
// Both endpoints keep INDEPENDENT counters for inbound and outbound. A frame
// whose nonce does not exactly equal encodeNonce(expected) is dropped, so a
// single lost/reordered frame invalidates the whole connection.

enum class Direction : std::uint8_t {
    kMobileToDesktop = 0,
    kDesktopToMobile = 1,
};

enum class PayloadKind : std::uint8_t {
    kText = 0,
    kBinary = 1,
};

// Fixed-size by type: a 32-byte session id cannot be malformed here. Convert
// once from the HKDF output with toSessionId() and keep it for the connection.
using SessionId = std::array<std::uint8_t, kSessionIdLength>;
std::optional<SessionId> toSessionId(const orca::crypto::Bytes& value);

struct FrameDescriptor {
    SessionId sessionId{};
    Direction direction = Direction::kMobileToDesktop;
    PayloadKind kind = PayloadKind::kText;
    std::uint64_t counter = 0;
};

orca::crypto::Bytes encodeHeader(const FrameDescriptor& descriptor);  // 42 bytes
orca::crypto::Bytes encodeNonce(const FrameDescriptor& descriptor);   // 24 bytes

// Reverse the header for parity checks / diagnostics. Returns nullopt unless
// exactly kHeaderLength bytes are supplied and direction/kind are 0 or 1.
std::optional<FrameDescriptor> decodeHeader(const std::uint8_t* data, std::size_t length);

inline constexpr std::size_t kMinSealedFrameLength =
    static_cast<std::size_t>(kNonceLength) + kSecretboxMacLength + kHeaderLength;  // 82

// Mirrors open()'s first guard: reject short frames before touching crypto.
bool hasValidFrameShape(const orca::crypto::Bytes& frame);

// Constant-time comparisons used BEFORE and AFTER secretbox.open:
//   - the first kNonceLength bytes of a frame must equal the expected nonce;
//   - the first kHeaderLength bytes of the opened plaintext must equal the
//     expected header (this is what actually binds the counter into the AEAD).
bool nonceMatches(const orca::crypto::Bytes& frame, const FrameDescriptor& expected);
bool headerMatches(const orca::crypto::Bytes& plaintext, const FrameDescriptor& expected);

// The TypeScript client uses BigInt and throws in writeUint64 once the counter
// passes 2^64-1. With a uint64 we must refuse the increment instead of
// wrapping: returning false means "tear down this connection and re-handshake".
inline bool advanceCounter(std::uint64_t& counter) noexcept {
    if (counter == std::numeric_limits<std::uint64_t>::max()) return false;
    ++counter;
    return true;
}
inline constexpr std::uint64_t kMaxCounter = std::numeric_limits<std::uint64_t>::max();

}  // namespace orca::protocol
