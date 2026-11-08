#include <orca/protocol/box_crypto.hpp>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sodium.h>

namespace orca::protocol {

using orca::crypto::Bytes;

namespace {

void ensureSodium() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        // sodium_init: 0 = already up, 1 = freshly initialised, -1 = failure.
        // Without working CSPRNG initialisation no crypto call is safe to run.
        if (sodium_init() < 0) {
            std::fputs("orca: sodium_init() failed\n", stderr);
            std::abort();
        }
    });
}

// Fixed key/nonce sizes of the nacl box family.
constexpr std::size_t kKeySize = 32;
constexpr std::size_t kNonceSize = 24;
constexpr std::size_t kMacSize = 16;

}  // namespace

std::optional<Bytes> randomBytes(std::size_t count) {
    ensureSodium();
    Bytes out(count);
    if (count > 0) randombytes_buf(out.data(), count);
    return out;
}

std::optional<BoxKeyPair> boxKeypair() {
    ensureSodium();
    BoxKeyPair pair;
    pair.publicKey.resize(kKeySize);
    pair.secretKey.resize(kKeySize);
    if (crypto_box_keypair(pair.publicKey.data(), pair.secretKey.data()) != 0) {
        return std::nullopt;
    }
    return pair;
}

std::optional<Bytes> boxScalarMult(const Bytes& secretKey, const Bytes& publicKey) {
    if (secretKey.size() != kKeySize || publicKey.size() != kKeySize) return std::nullopt;
    ensureSodium();
    Bytes out(kKeySize);
    if (crypto_scalarmult(out.data(), secretKey.data(), publicKey.data()) != 0) return std::nullopt;
    return out;
}

std::optional<Bytes> boxBeforenm(const Bytes& publicKey, const Bytes& secretKey) {
    if (publicKey.size() != kKeySize || secretKey.size() != kKeySize) return std::nullopt;
    ensureSodium();
    Bytes out(kKeySize);
    // Argument order matches nacl.box.before(peerPublicKey, ownSecretKey).
    if (crypto_box_beforenm(out.data(), publicKey.data(), secretKey.data()) != 0) {
        return std::nullopt;
    }
    return out;
}

std::optional<Bytes> secretboxSeal(const Bytes& plaintext, const Bytes& nonce, const Bytes& key) {
    if (nonce.size() != kNonceSize || key.size() != kKeySize) return std::nullopt;
    ensureSodium();
    Bytes out(kMacSize + plaintext.size());
    if (crypto_secretbox_easy(out.data(), plaintext.data(), plaintext.size(), nonce.data(),
                              key.data()) != 0) {
        return std::nullopt;
    }
    return out;
}

std::optional<Bytes> secretboxOpen(const Bytes& ciphertext, const Bytes& nonce, const Bytes& key) {
    if (nonce.size() != kNonceSize || key.size() != kKeySize) return std::nullopt;
    if (ciphertext.size() < kMacSize) return std::nullopt;
    ensureSodium();
    Bytes out(ciphertext.size() - kMacSize);
    if (crypto_secretbox_open_easy(out.data(), ciphertext.data(), ciphertext.size(), nonce.data(),
                                   key.data()) != 0) {
        return std::nullopt;
    }
    return out;
}

}  // namespace orca::protocol
