#pragma once

#include <orca/crypto/bytes.hpp>

#include <optional>

namespace orca::protocol {

// Thin libsodium bindings for the E2EE v2 primitives that no pure-C++ slice
// could cover (P1-c). All functions validate input sizes and return nullopt
// instead of throwing; libsodium itself is initialised once, thread-safely.

// Raw X25519: crypto_scalarmult. n = own secret key, p = peer public key.
std::optional<orca::crypto::Bytes> boxScalarMult(const orca::crypto::Bytes& secretKey,
                                                 const orca::crypto::Bytes& publicKey);

// The nacl box priming step: crypto_box_beforenm (scalarmult followed by
// HSalsa20). Mirrors nacl.box.before(peerPublicKey, ownSecretKey); its output
// is the HKDF input shared secret of E2EE v2.
std::optional<orca::crypto::Bytes> boxBeforenm(const orca::crypto::Bytes& publicKey,
                                               const orca::crypto::Bytes& secretKey);

// XSalsa20-Poly1305. Output layout = MAC(16) || ciphertext, identical to
// nacl.secretbox and crypto_secretbox_easy. Both take a 24-byte nonce and a
// 32-byte key.
std::optional<orca::crypto::Bytes> secretboxSeal(const orca::crypto::Bytes& plaintext,
                                                 const orca::crypto::Bytes& nonce,
                                                 const orca::crypto::Bytes& key);
std::optional<orca::crypto::Bytes> secretboxOpen(const orca::crypto::Bytes& ciphertext,
                                                 const orca::crypto::Bytes& nonce,
                                                 const orca::crypto::Bytes& key);

}  // namespace orca::protocol
