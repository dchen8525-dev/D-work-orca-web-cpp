#include <orca/protocol/e2ee_session.hpp>

#include <orca/crypto/base64.hpp>
#include <orca/protocol/box_crypto.hpp>
#include <orca/protocol/versions.hpp>

#include <algorithm>

namespace orca::protocol {

using orca::crypto::Bytes;

namespace {

constexpr std::size_t kKeySize = 32;

bool scheduleUsable(const E2eeKeySchedule& schedule) {
    return schedule.mobileToDesktopKey.size() == kKeySize &&
           schedule.desktopToMobileKey.size() == kKeySize && schedule.sessionId.size() == kKeySize;
}

const Bytes& keyFor(const E2eeKeySchedule& schedule, Direction direction) {
    return direction == Direction::kMobileToDesktop ? schedule.mobileToDesktopKey
                                                    : schedule.desktopToMobileKey;
}

}  // namespace

std::optional<Bytes> sealFrameAt(const E2eeKeySchedule& schedule, Direction direction,
                                 PayloadKind kind, std::uint64_t counter,
                                 const Bytes& plaintext) {
    if (!scheduleUsable(schedule)) return std::nullopt;
    const auto sessionId = toSessionId(schedule.sessionId);
    if (!sessionId) return std::nullopt;

    FrameDescriptor descriptor{*sessionId, direction, kind, counter};
    const Bytes nonce = encodeNonce(descriptor);
    Bytes body = encodeHeader(descriptor);
    body.insert(body.end(), plaintext.begin(), plaintext.end());

    const auto boxed = secretboxSeal(body, nonce, keyFor(schedule, direction));
    if (!boxed) return std::nullopt;

    Bytes frame;
    frame.reserve(nonce.size() + boxed->size());
    frame.insert(frame.end(), nonce.begin(), nonce.end());
    frame.insert(frame.end(), boxed->begin(), boxed->end());
    return frame;
}

std::optional<Bytes> openFrameAt(const E2eeKeySchedule& schedule, Direction direction,
                                 PayloadKind kind, std::uint64_t counter, const Bytes& frame) {
    if (!scheduleUsable(schedule) || !hasValidFrameShape(frame)) return std::nullopt;
    const auto sessionId = toSessionId(schedule.sessionId);
    if (!sessionId) return std::nullopt;

    const FrameDescriptor expected{*sessionId, direction, kind, counter};
    // The nonce prefix must equal the recomputed expected nonce byte for byte;
    // this is what binds the counter sequence to the ciphertext.
    if (!nonceMatches(frame, expected)) return std::nullopt;

    const Bytes nonce = encodeNonce(expected);
    const Bytes ciphertext(frame.begin() + static_cast<std::size_t>(kNonceLength), frame.end());
    const auto opened = secretboxOpen(ciphertext, nonce, keyFor(schedule, direction));
    if (!opened) return std::nullopt;
    if (opened->size() < static_cast<std::size_t>(kHeaderLength)) return std::nullopt;
    // The opened header must equal the expected header: binds sessionId,
    // direction, kind and counter into the authenticated payload.
    if (!headerMatches(*opened, expected)) return std::nullopt;

    return Bytes(opened->begin() + static_cast<std::size_t>(kHeaderLength), opened->end());
}

std::optional<E2eeSession> E2eeSession::create(const E2eeKeySchedule& schedule) {
    if (!scheduleUsable(schedule)) return std::nullopt;
    const auto sessionId = toSessionId(schedule.sessionId);
    if (!sessionId) return std::nullopt;

    E2eeSession session;
    session.schedule_ = schedule;
    session.sessionId_ = *sessionId;
    session.ready_ = true;
    return session;
}

std::optional<Bytes> E2eeSession::seal(const Bytes& plaintext, PayloadKind kind) {
    if (!ready_) return std::nullopt;
    auto frame = sealFrameAt(schedule_, Direction::kMobileToDesktop, kind, outbound_, plaintext);
    if (!frame) return std::nullopt;
    // Sealing at kMaxCounter is legal (the counter is encodable), but nothing
    // may follow: the next seal/open fails and the connection re-handshakes.
    if (!advanceCounter(outbound_)) ready_ = false;
    return frame;
}

std::optional<Bytes> E2eeSession::open(const Bytes& frame, PayloadKind kind) {
    if (!ready_) return std::nullopt;
    auto payload = openFrameAt(schedule_, Direction::kDesktopToMobile, kind, inbound_, frame);
    if (!payload) return std::nullopt;
    if (!advanceCounter(inbound_)) ready_ = false;
    return payload;
}

std::optional<std::string> E2eeSession::sealText(const std::string& plaintext) {
    const Bytes bytes(plaintext.begin(), plaintext.end());
    const auto frame = seal(bytes, PayloadKind::kText);
    if (!frame) return std::nullopt;
    return orca::crypto::base64Encode(*frame);
}

std::optional<std::string> E2eeSession::openText(const std::string& frameB64) {
    Bytes frame;
    if (!orca::crypto::base64Decode(frameB64, frame)) return std::nullopt;
    const auto payload = open(frame, PayloadKind::kText);
    if (!payload) return std::nullopt;
    return std::string(payload->begin(), payload->end());
}

}  // namespace orca::protocol
