#include <orca/crypto/scrypt.hpp>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <charconv>
#include <stdexcept>
#include <vector>

namespace orca::crypto {

namespace {

constexpr ScryptParams kDefaultParams{};

bool scryptDerive(std::string_view password, const Bytes& salt, const ScryptParams& params,
                  Bytes& out) {
    out.assign(params.keyLength, 0);
    // EVP_PBE_scrypt takes uint64 parameters; overflow of N*r*p against the
    // memory limit returns 0 with an OpenSSL error queued.
    return EVP_PBE_scrypt(password.data(), password.size(), salt.data(), salt.size(),
                          params.n, params.r, params.p, /*maxmem=*/64ull * 1024 * 1024 * 1024,
                          out.data(), out.size()) == 1;
}

std::string base64StandardEncode(const std::uint8_t* data, std::size_t size) {
    // EVP_EncodeBlock emits padded standard base64 with newlines every 64 chars
    // only in EVP_EncodeUpdate; EncodeBlock writes no newlines.
    std::string out((size + 2) / 3 * 4, '\0');
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data,
                                        static_cast<int>(size));
    out.resize(static_cast<std::size_t>(written));
    return out;
}

bool base64StandardDecode(std::string_view text, Bytes& out) {
    out.clear();
    std::size_t trimmed = text.size();
    while (trimmed > 0 && (text[trimmed - 1] == '\n' || text[trimmed - 1] == '\r')) --trimmed;
    if (trimmed % 4 != 0) return false;
    out.resize(trimmed / 4 * 3);
    const int written = EVP_DecodeBlock(out.data(),
                                        reinterpret_cast<const unsigned char*>(text.data()),
                                        static_cast<int>(trimmed));
    if (written < 0) {
        out.clear();
        return false;
    }
    // EVP_DecodeBlock ignores trailing '=' padding; trim the implied zeros.
    std::size_t padding = 0;
    while (padding < trimmed && text[trimmed - 1 - padding] == '=') ++padding;
    out.resize(static_cast<std::size_t>(written) - padding);
    return true;
}

std::uint64_t parseU64(std::string_view text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return 0;
    return value;
}

}  // namespace

std::optional<ScryptParams> parseScryptEncoded(std::string_view encoded) {
    // scrypt$N$r$p$salt$hash — exactly six '$'-separated fields.
    std::array<std::string_view, 6> parts;
    std::size_t count = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= encoded.size(); ++i) {
        if (i == encoded.size() || encoded[i] == '$') {
            if (count == 6) return std::nullopt;
            parts[count++] = encoded.substr(begin, i - begin);
            begin = i + 1;
        }
    }
    if (count != 6 || parts[0] != "scrypt") return std::nullopt;

    ScryptParams params;
    params.n = parseU64(parts[1]);
    params.r = parseU64(parts[2]);
    params.p = parseU64(parts[3]);
    if (params.n == 0 || params.r == 0 || params.p == 0) return std::nullopt;
    Bytes hash;
    if (!base64StandardDecode(parts[5], hash) || hash.size() != params.keyLength) {
        return std::nullopt;
    }
    return params;
}

std::string hashPassword(std::string_view password) {
    Bytes salt(16);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed for password salt");
    }
    Bytes hash;
    if (!scryptDerive(password, salt, kDefaultParams, hash)) {
        throw std::runtime_error("EVP_PBE_scrypt failed");
    }
    std::string encoded = "scrypt$";
    encoded += std::to_string(kDefaultParams.n);
    encoded += '$';
    encoded += std::to_string(kDefaultParams.r);
    encoded += '$';
    encoded += std::to_string(kDefaultParams.p);
    encoded += '$';
    encoded += base64StandardEncode(salt.data(), salt.size());
    encoded += '$';
    encoded += base64StandardEncode(hash.data(), hash.size());
    return encoded;
}

bool verifyPassword(std::string_view password, std::string_view encoded) {
    const auto params = parseScryptEncoded(encoded);
    if (!params) return false;

    // Re-split without revalidating: salt and hash are parts 4 and 5.
    std::size_t dollar = 0;
    for (int field = 0; field < 4; ++field) {
        dollar = encoded.find('$', dollar + 1);
        if (dollar == std::string_view::npos) return false;
    }
    const std::size_t saltBegin = dollar + 1;
    const std::size_t hashBegin = encoded.find('$', saltBegin);
    if (hashBegin == std::string_view::npos) return false;

    Bytes salt;
    Bytes expected;
    if (!base64StandardDecode(encoded.substr(saltBegin, hashBegin - saltBegin), salt)) {
        return false;
    }
    if (!base64StandardDecode(encoded.substr(hashBegin + 1), expected)) return false;
    if (expected.size() != params->keyLength) return false;

    Bytes actual;
    if (!scryptDerive(password, salt, *params, actual)) return false;
    // Constant-time compare from bytes.hpp: a login path must not leak positions.
    return constantTimeEqual(actual, expected);
}

}  // namespace orca::crypto
