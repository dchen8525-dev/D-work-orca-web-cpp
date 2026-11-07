#include <orca/crypto/bytes.hpp>

namespace orca::crypto {

namespace {

constexpr char kHex[] = "0123456789abcdef";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::string hexEncode(const std::uint8_t* data, std::size_t length) {
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

std::string hexEncode(const Bytes& value) {
    return hexEncode(value.data(), value.size());
}

std::string hexEncode(const Hash256& value) {
    return hexEncode(value.data(), value.size());
}

bool hexDecode(std::string_view text, Bytes& out) {
    out.clear();
    if (text.size() % 2 != 0) return false;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hexValue(text[i]);
        const int low = hexValue(text[i + 1]);
        if (high < 0 || low < 0) return false;
        out.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return true;
}

bool constantTimeEqual(const Bytes& left, const Bytes& right) {
    if (left.size() != right.size()) return false;
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<std::uint8_t>(left[i] ^ right[i]);
    }
    return difference == 0;
}

}  // namespace orca::crypto
