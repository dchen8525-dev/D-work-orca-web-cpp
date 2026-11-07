#include <orca/crypto/hkdf.hpp>

#include <orca/crypto/hmac.hpp>
#include <orca/crypto/sha256.hpp>
#include <orca/protocol/versions.hpp>

namespace orca::crypto {

Bytes hkdfSha256(const Bytes& ikm, const Bytes& salt, const Bytes& info, std::size_t length) {
    const Hash256 prkHash = hmacSha256(salt, ikm);
    const Bytes prk(prkHash.begin(), prkHash.end());

    Bytes okm;
    Bytes previous;
    std::uint8_t counter = 1;
    while (okm.size() < length) {
        Bytes input = previous;
        appendRaw(input, info);
        input.push_back(counter);
        const Hash256 block = hmacSha256(prk, input);
        previous.assign(block.begin(), block.end());
        appendRaw(okm, previous);
        ++counter;
    }
    okm.resize(length);
    return okm;
}

E2eeKeySchedule deriveE2eeSchedule(const Bytes& sharedSecret,
                                   const Bytes& clientNonce,
                                   const Bytes& hostNonce,
                                   const Bytes& transcript) {
    const Hash256 transcriptHash = sha256(transcript);

    Bytes saltInput;
    appendRaw(saltInput, orca::protocol::kSaltLabel, orca::protocol::kSaltLabelSize);
    appendRaw(saltInput, clientNonce);
    appendRaw(saltInput, hostNonce);
    const Hash256 saltHash = sha256(saltInput);
    const Bytes salt(saltHash.begin(), saltHash.end());

    Bytes info;
    appendRaw(info, orca::protocol::kInfoLabel, orca::protocol::kInfoLabelSize);
    appendRaw(info, transcriptHash.data(), transcriptHash.size());

    const Bytes expanded = hkdfSha256(sharedSecret, salt, info, 96);
    E2eeKeySchedule schedule;
    schedule.mobileToDesktopKey.assign(expanded.begin(), expanded.begin() + 32);
    schedule.desktopToMobileKey.assign(expanded.begin() + 32, expanded.begin() + 64);
    schedule.sessionId.assign(expanded.begin() + 64, expanded.begin() + 96);
    schedule.transcriptHash = transcriptHash;
    return schedule;
}

}  // namespace orca::crypto
