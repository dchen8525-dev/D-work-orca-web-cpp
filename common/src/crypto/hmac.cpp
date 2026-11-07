#include <orca/crypto/hmac.hpp>

#include <orca/crypto/sha256.hpp>

namespace orca::crypto {

Hash256 hmacSha256(const Bytes& key, const Bytes& message) {
    constexpr std::size_t kBlockSize = 64;
    Bytes effectiveKey = key;
    if (effectiveKey.size() > kBlockSize) {
        const Hash256 digest = sha256(effectiveKey);
        effectiveKey.assign(digest.begin(), digest.end());
    }
    effectiveKey.resize(kBlockSize, 0x00);

    Bytes inner(kBlockSize, 0x36);
    Bytes outer(kBlockSize, 0x5c);
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        inner[i] ^= effectiveKey[i];
        outer[i] ^= effectiveKey[i];
    }
    appendRaw(inner, message);
    const Hash256 innerHash = sha256(inner);
    appendRaw(outer, innerHash.data(), innerHash.size());
    return sha256(outer);
}

}  // namespace orca::crypto
