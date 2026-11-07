#pragma once

#include <orca/crypto/bytes.hpp>

namespace orca::crypto {

// RFC 5869 HKDF-SHA256. Matches Node's hkdfSync('sha256', ikm, salt, info, length).
// Orca E2EE v2 expands 96 bytes into two 32-byte direction keys and a 32-byte session id.
Bytes hkdfSha256(const Bytes& ikm, const Bytes& salt, const Bytes& info, std::size_t length);

struct E2eeKeySchedule {
    Bytes mobileToDesktopKey;
    Bytes desktopToMobileKey;
    Bytes sessionId;
    Hash256 transcriptHash;
};

E2eeKeySchedule deriveE2eeSchedule(const Bytes& sharedSecret,
                                   const Bytes& clientNonce,
                                   const Bytes& hostNonce,
                                   const Bytes& transcript);

}  // namespace orca::crypto
