#pragma once

#include <orca/crypto/bytes.hpp>
#include <orca/crypto/hkdf.hpp>
#include <orca/protocol/seal_frame.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace orca::protocol {

// The key schedule lives in orca::crypto (pure C++, hkdf.hpp); the session is
// the protocol-layer consumer of it.
using orca::crypto::E2eeKeySchedule;

// Single-frame seal/open at an explicit counter. E2eeSession drives these with
// its own counters; golden-vector tests call them directly to pin exact
// counters without driving 2^64 seals.
std::optional<orca::crypto::Bytes> sealFrameAt(const E2eeKeySchedule& schedule, Direction direction,
                                               PayloadKind kind, std::uint64_t counter,
                                               const orca::crypto::Bytes& plaintext);
std::optional<orca::crypto::Bytes> openFrameAt(const E2eeKeySchedule& schedule, Direction direction,
                                               PayloadKind kind, std::uint64_t counter,
                                               const orca::crypto::Bytes& frame);

// C++ mirror of E2eeV2Client in D:/work/orca-web/src/gateway/orca/e2ee-v2-client.ts:
// seals as mobile-to-desktop with mobileToDesktopKey, opens as desktop-to-mobile
// with desktopToMobileKey, one independent counter per direction.
//
// Failure semantics match the TS client: any open() failure returns nullopt and
// leaves the counters untouched, so one lost/reordered/tampered frame makes the
// caller tear the connection down and re-handshake. Counters refuse to wrap
// past 2^64-1: the seal/open that would pass the maximum fails and the session
// becomes not-ready.
class E2eeSession {
public:
    static std::optional<E2eeSession> create(const E2eeKeySchedule& schedule);

    bool ready() const noexcept { return ready_; }
    std::uint64_t outboundCounter() const noexcept { return outbound_; }
    std::uint64_t inboundCounter() const noexcept { return inbound_; }

    std::optional<std::string> sealText(const std::string& plaintext);
    std::optional<std::string> openText(const std::string& frameB64);
    std::optional<orca::crypto::Bytes> seal(const orca::crypto::Bytes& plaintext, PayloadKind kind);
    std::optional<orca::crypto::Bytes> open(const orca::crypto::Bytes& frame, PayloadKind kind);

private:
    E2eeKeySchedule schedule_{};
    SessionId sessionId_{};
    bool ready_ = false;
    std::uint64_t outbound_ = 0;
    std::uint64_t inbound_ = 0;
};

}  // namespace orca::protocol
