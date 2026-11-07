#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace orca::crypto {

using Bytes = std::vector<std::uint8_t>;
using Hash256 = std::array<std::uint8_t, 32>;

// Big-endian byte helpers. The E2EE v2 transcript, frame header and nonce all
// use big-endian u32/u64; getting one of these wrong is silent on the wire.
inline void appendU32BE(Bytes& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

inline void appendU64BE(Bytes& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

inline void appendRaw(Bytes& out, const std::uint8_t* data, std::size_t length) {
    out.insert(out.end(), data, data + length);
}

// C string literals (the E2EE labels) decay to const char*; keep the byte
// pattern intact, including the trailing NUL byte in those labels.
inline void appendRaw(Bytes& out, const char* data, std::size_t length) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + length);
}

inline void appendRaw(Bytes& out, const Bytes& value) {
    out.insert(out.end(), value.begin(), value.end());
}

inline void appendRaw(Bytes& out, std::string_view value) {
    out.insert(out.end(), value.begin(), value.end());
}

// One transcript field: [u32 nameLen][name][u32 valueLen][value].
inline void appendField(Bytes& out, std::string_view name, const Bytes& value) {
    appendU32BE(out, static_cast<std::uint32_t>(name.size()));
    appendRaw(out, name);
    appendU32BE(out, static_cast<std::uint32_t>(value.size()));
    appendRaw(out, value);
}

inline Bytes stringList(const std::vector<std::string>& values) {
    Bytes out;
    appendU32BE(out, static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        appendU32BE(out, static_cast<std::uint32_t>(value.size()));
        appendRaw(out, value);
    }
    return out;
}

inline Bytes numberList(const std::vector<std::uint32_t>& values) {
    Bytes out;
    appendU32BE(out, static_cast<std::uint32_t>(values.size()));
    for (const auto value : values) appendU32BE(out, value);
    return out;
}

inline Bytes fieldValue(std::string_view value) {
    Bytes out;
    appendRaw(out, value);
    return out;
}

inline Bytes fieldValue(std::uint32_t value) {
    Bytes out;
    appendU32BE(out, value);
    return out;
}

std::string hexEncode(const std::uint8_t* data, std::size_t length);
std::string hexEncode(const Bytes& value);
std::string hexEncode(const Hash256& value);
bool hexDecode(std::string_view text, Bytes& out);

bool constantTimeEqual(const Bytes& left, const Bytes& right);

}  // namespace orca::crypto
