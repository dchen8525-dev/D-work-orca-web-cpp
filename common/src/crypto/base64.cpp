#include <orca/crypto/base64.hpp>

#include <algorithm>
#include <utility>

namespace orca::crypto {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int valueOf(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::uint8_t* data, std::size_t length) {
    std::string out;
    out.reserve(((length + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= length; i += 3) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                    static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3f]);
        out.push_back(kAlphabet[chunk & 0x3f]);
    }
    const std::size_t remaining = length - i;
    if (remaining == 1) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

std::string base64Encode(const Bytes& value) {
    return base64Encode(value.data(), value.size());
}

bool base64Decode(std::string_view text, Bytes& out) {
    out.clear();
    if (text.empty()) return true;
    if (text.size() % 4 != 0) return false;

    std::size_t padding = 0;
    while (padding < 2 && text[text.size() - 1 - padding] == '=') ++padding;

    Bytes decoded;
    decoded.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int values[4] = {-1, -1, -1, -1};
        for (int j = 0; j < 4; ++j) {
            const char c = text[i + j];
            if (c == '=') {
                if (!last || j < 4 - padding) return false;
                continue;
            }
            values[j] = valueOf(c);
            if (values[j] < 0) return false;
            if (last && padding > 0 && j >= 4 - padding) return false;
        }
        const std::uint32_t chunk = (static_cast<std::uint32_t>(values[0]) << 18) |
                                    (static_cast<std::uint32_t>(std::max(values[1], 0)) << 12) |
                                    (static_cast<std::uint32_t>(std::max(values[2], 0)) << 6) |
                                    static_cast<std::uint32_t>(std::max(values[3], 0));
        decoded.push_back(static_cast<std::uint8_t>((chunk >> 16) & 0xff));
        if (values[2] >= 0) decoded.push_back(static_cast<std::uint8_t>((chunk >> 8) & 0xff));
        if (values[3] >= 0) decoded.push_back(static_cast<std::uint8_t>(chunk & 0xff));
    }

    // Reject non-canonical encodings (extra padding bits, URL-safe alphabet,
    // whitespace): the TypeScript client checks encode(decode(x)) === x.
    if (base64Encode(decoded) != text) return false;
    out = std::move(decoded);
    return true;
}

}  // namespace orca::crypto
