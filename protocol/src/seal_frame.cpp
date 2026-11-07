#include <orca/protocol/seal_frame.hpp>

#include <algorithm>

namespace orca::protocol {

using orca::crypto::Bytes;

std::optional<SessionId> toSessionId(const Bytes& value) {
    if (value.size() != static_cast<std::size_t>(kSessionIdLength)) return std::nullopt;
    SessionId out{};
    std::copy(value.begin(), value.end(), out.begin());
    return out;
}

Bytes encodeHeader(const FrameDescriptor& descriptor) {
    Bytes out;
    out.reserve(static_cast<std::size_t>(kHeaderLength));
    orca::crypto::appendRaw(out, descriptor.sessionId.data(), descriptor.sessionId.size());
    out.push_back(static_cast<std::uint8_t>(descriptor.direction));
    out.push_back(static_cast<std::uint8_t>(descriptor.kind));
    orca::crypto::appendU64BE(out, descriptor.counter);
    return out;
}

Bytes encodeNonce(const FrameDescriptor& descriptor) {
    Bytes out;
    out.reserve(static_cast<std::size_t>(kNonceLength));
    // Only the first 12 of the 32 session-id bytes go into the nonce.
    orca::crypto::appendRaw(out, descriptor.sessionId.data(), 12);
    out.push_back(static_cast<std::uint8_t>(kE2eeFrameVersion));
    out.push_back(static_cast<std::uint8_t>(descriptor.direction));
    out.push_back(static_cast<std::uint8_t>(descriptor.kind));
    out.push_back(0);  // reserved byte, always zero in FRAME_VERSION 2
    orca::crypto::appendU64BE(out, descriptor.counter);
    return out;
}

std::optional<FrameDescriptor> decodeHeader(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length != static_cast<std::size_t>(kHeaderLength)) return std::nullopt;

    FrameDescriptor descriptor{};
    std::copy(data, data + kSessionIdLength, descriptor.sessionId.begin());
    const std::uint8_t direction = data[kSessionIdLength];
    const std::uint8_t kind = data[kSessionIdLength + 1];
    if (direction > 1 || kind > 1) return std::nullopt;
    descriptor.direction = static_cast<Direction>(direction);
    descriptor.kind = static_cast<PayloadKind>(kind);

    std::uint64_t counter = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        counter = (counter << 8) | data[kSessionIdLength + 2 + i];
    }
    descriptor.counter = counter;
    return descriptor;
}

bool hasValidFrameShape(const Bytes& frame) {
    return frame.size() >= kMinSealedFrameLength;
}

bool nonceMatches(const Bytes& frame, const FrameDescriptor& expected) {
    if (frame.size() < static_cast<std::size_t>(kNonceLength)) return false;
    const Bytes nonce = encodeNonce(expected);
    const Bytes prefix(frame.begin(), frame.begin() + kNonceLength);
    return orca::crypto::constantTimeEqual(prefix, nonce);
}

bool headerMatches(const Bytes& plaintext, const FrameDescriptor& expected) {
    if (plaintext.size() < static_cast<std::size_t>(kHeaderLength)) return false;
    const Bytes prefix(plaintext.begin(), plaintext.begin() + kHeaderLength);
    return orca::crypto::constantTimeEqual(prefix, encodeHeader(expected));
}

}  // namespace orca::protocol
