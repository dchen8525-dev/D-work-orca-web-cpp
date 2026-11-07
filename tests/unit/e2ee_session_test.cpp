// P1-c golden tests: the libsodium-backed E2EE v2 slice must reproduce
// tweetnacl output byte for byte. Vectors come from
// tools/export-p1c-vectors.mjs (independent JS implementation running the
// original project's tweetnacl); p1-vectors.json supplies the transcript for
// the key-schedule checks so the two vector files stay provably consistent.
//
//   e2ee_session_test <p1c-vectors.json> <p1-vectors.json>

#include <orca/crypto/base64.hpp>
#include <orca/crypto/bytes.hpp>
#include <orca/crypto/hkdf.hpp>
#include <orca/protocol/box_crypto.hpp>
#include <orca/protocol/e2ee_session.hpp>
#include <orca/protocol/seal_frame.hpp>
#include <orca/protocol/versions.hpp>

#include "test_util.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace orca::crypto;
using namespace orca::protocol;

namespace {

Bytes fromHex(const std::string& text) {
    Bytes out;
    if (!hexDecode(text, out)) throw std::runtime_error("bad hex in vectors: " + text);
    return out;
}

const nlohmann::json& frameCase(const nlohmann::json& vectors, const char* name) {
    return vectors.at("frames").at(name);
}

void testBox(const nlohmann::json& vectors) {
    const auto& box = vectors.at("box");
    const Bytes aliceSk = fromHex(box.at("aliceSecretKeyHex").get<std::string>());
    const Bytes bobSk = fromHex(box.at("bobSecretKeyHex").get<std::string>());
    const Bytes bobPk = fromHex(box.at("bobPublicKeyHex").get<std::string>());

    const auto scalar = boxScalarMult(aliceSk, bobPk);
    test::check(scalar.has_value(), "boxScalarMult accepts a valid pair");
    if (scalar) test::checkBytes(*scalar, box.at("scalarMultHex").get<std::string>(), "raw X25519 scalarmult");

    const auto primed = boxBeforenm(bobPk, aliceSk);
    test::check(primed.has_value(), "boxBeforenm accepts a valid pair");
    if (primed) test::checkBytes(*primed, box.at("beforenmHex").get<std::string>(), "crypto_box_beforenm (HSalsa20 priming)");

    test::check(!boxScalarMult(Bytes(31, 0), bobPk).has_value(), "boxScalarMult rejects a 31-byte key");
    test::check(!boxBeforenm(bobPk, Bytes(33, 0)).has_value(), "boxBeforenm rejects a 33-byte key");
}

void testSecretbox(const nlohmann::json& vectors) {
    const auto& sb = vectors.at("secretbox");
    const Bytes key = fromHex(sb.at("keyHex").get<std::string>());
    const Bytes nonce = fromHex(sb.at("nonceHex").get<std::string>());
    const Bytes plaintext = fromHex(sb.at("plaintextHex").get<std::string>());
    const Bytes ciphertext = fromHex(sb.at("ciphertextHex").get<std::string>());

    const auto sealed = secretboxSeal(plaintext, nonce, key);
    test::check(sealed.has_value(), "secretboxSeal accepts valid input");
    if (sealed) test::checkBytes(*sealed, sb.at("ciphertextHex").get<std::string>(), "secretbox seal (MAC-prefixed, matches nacl.secretbox)");

    const auto opened = secretboxOpen(ciphertext, nonce, key);
    test::check(opened.has_value(), "secretboxOpen accepts a valid ciphertext");
    if (opened) test::checkBytes(*opened, sb.at("plaintextHex").get<std::string>(), "secretbox open round-trip");

    Bytes tampered = ciphertext;
    tampered[tampered.size() - 1] ^= 0x01;
    test::check(!secretboxOpen(tampered, nonce, key).has_value(), "secretboxOpen rejects a tampered ciphertext");
    Bytes tamperedTag = ciphertext;
    tamperedTag[0] ^= 0x80;
    test::check(!secretboxOpen(tamperedTag, nonce, key).has_value(), "secretboxOpen rejects a tampered MAC");
    test::check(!secretboxSeal(plaintext, Bytes(23, 0), key).has_value(), "secretboxSeal rejects a 23-byte nonce");
    test::check(!secretboxOpen(ciphertext, nonce, Bytes(31, 0)).has_value(), "secretboxOpen rejects a 31-byte key");
}

void testSchedule(const nlohmann::json& p1c, const nlohmann::json& p1) {
    const auto& schedule = p1c.at("schedule");
    const Bytes sharedSecret = fromHex(schedule.at("sharedSecretHex").get<std::string>());
    const Bytes clientNonce = fromHex(schedule.at("clientNonceHex").get<std::string>());
    const Bytes hostNonce = fromHex(schedule.at("hostNonceHex").get<std::string>());
    const Bytes transcript = fromHex(p1.at("transcriptHex").get<std::string>());

    const E2eeKeySchedule derived = deriveE2eeSchedule(sharedSecret, clientNonce, hostNonce, transcript);
    test::checkBytes(derived.mobileToDesktopKey, schedule.at("mobileToDesktopKeyHex").get<std::string>(), "schedule mobileToDesktopKey");
    test::checkBytes(derived.desktopToMobileKey, schedule.at("desktopToMobileKeyHex").get<std::string>(), "schedule desktopToMobileKey");
    test::checkBytes(derived.sessionId, schedule.at("sessionIdHex").get<std::string>(), "schedule sessionId");
    {
        Bytes hashBytes(derived.transcriptHash.begin(), derived.transcriptHash.end());
        test::checkBytes(hashBytes, schedule.at("transcriptHashHex").get<std::string>(), "schedule transcriptHash");
    }

    // Swapped nonces must not reproduce the same schedule.
    const E2eeKeySchedule swapped =
        deriveE2eeSchedule(sharedSecret, hostNonce, clientNonce, transcript);
    test::check(swapped.mobileToDesktopKey != derived.mobileToDesktopKey,
                "schedule depends on the nonce order");
}

void testFrames(const nlohmann::json& p1c, const E2eeKeySchedule& schedule) {
    for (const char* name : {"m2dTextCounter0", "m2dTextCounter1", "m2dBinarySparse", "d2mTextCounter0", "d2mTextCounter5"}) {
        const auto& frame = frameCase(p1c, name);
        const auto direction = static_cast<Direction>(frame.at("direction").get<int>());
        const auto kind = static_cast<PayloadKind>(frame.at("kind").get<int>());
        const Bytes plaintext = fromHex(frame.at("plaintextHex").get<std::string>());
        const Bytes expectedFrame = fromHex(frame.at("frameHex").get<std::string>());

        const Bytes expected = fromHex(frame.at("counterHex").get<std::string>());
        std::uint64_t counter = 0;
        for (const auto byte : expected) counter = (counter << 8) | byte;

        const auto sealed = sealFrameAt(schedule, direction, kind, counter, plaintext);
        test::check(sealed.has_value(), std::string("sealFrameAt ") + name);
        if (sealed) test::checkBytes(*sealed, frame.at("frameHex").get<std::string>(), std::string("sealed frame ") + name);

        const auto opened = openFrameAt(schedule, direction, kind, counter, expectedFrame);
        test::check(opened.has_value(), std::string("openFrameAt ") + name);
        if (opened) test::checkBytes(*opened, frame.at("plaintextHex").get<std::string>(), std::string("opened payload ") + name);
    }

    // Cross-checks: wrong counter, wrong direction, wrong session key side.
    const Bytes d2mFrame = fromHex(frameCase(p1c, "d2mTextCounter5").at("frameHex").get<std::string>());
    test::check(!openFrameAt(schedule, Direction::kDesktopToMobile, PayloadKind::kText, 4, d2mFrame).has_value(),
                "openFrameAt rejects a stale counter");
    test::check(!openFrameAt(schedule, Direction::kMobileToDesktop, PayloadKind::kText, 5, d2mFrame).has_value(),
                "openFrameAt rejects the wrong direction");
    const Bytes m2dFrame = fromHex(frameCase(p1c, "m2dTextCounter0").at("frameHex").get<std::string>());
    Bytes flipped = m2dFrame;
    flipped[flipped.size() - 1] ^= 0x01;
    test::check(!openFrameAt(schedule, Direction::kMobileToDesktop, PayloadKind::kText, 0, flipped).has_value(),
                "openFrameAt rejects a tampered ciphertext");
    test::check(!openFrameAt(schedule, Direction::kMobileToDesktop, PayloadKind::kText, 0, Bytes(40, 0)).has_value(),
                "openFrameAt rejects a short frame");
}

void testSession(const nlohmann::json& p1c, const E2eeKeySchedule& schedule) {
    auto session = E2eeSession::create(schedule);
    test::check(session.has_value(), "E2eeSession::create accepts a valid schedule");
    if (!session) return;

    // Seal sequence must match the vectors counter for counter.
    const auto& frame0 = frameCase(p1c, "m2dTextCounter0");
    const auto& frame1 = frameCase(p1c, "m2dTextCounter1");
    const Bytes plaintext0 = fromHex(frame0.at("plaintextHex").get<std::string>());
    const auto sealed0 = session->sealText(std::string(plaintext0.begin(), plaintext0.end()));
    test::check(sealed0.has_value(), "sealText #0 succeeds");
    if (sealed0) {
        test::check(*sealed0 == orca::crypto::base64Encode(fromHex(frame0.at("frameHex").get<std::string>())),
                    "sealText #0 matches the golden frame");
    }
    test::check(session->outboundCounter() == 1, "outbound counter advanced to 1");

    // The second seal must use counter 1 (the vector's second plaintext).
    const Bytes plaintext1 = fromHex(frame1.at("plaintextHex").get<std::string>());
    const auto sealed1 = session->seal(plaintext1, PayloadKind::kText);
    test::check(sealed1.has_value(), "seal #1 succeeds");
    if (sealed1) test::checkBytes(*sealed1, frame1.at("frameHex").get<std::string>(), "seal #1 matches the golden frame");

    // Open the golden d2m frame, then replay it: the second open must fail and
    // leave the session usable for the next inbound frame.
    const auto& d2m0 = frameCase(p1c, "d2mTextCounter0");
    const std::string d2m0B64 = orca::crypto::base64Encode(fromHex(d2m0.at("frameHex").get<std::string>()));
    const auto opened = session->openText(d2m0B64);
    test::check(opened.has_value(), "openText accepts the golden d2m frame");
    if (opened) test::check(*opened == "orca-cpp p1c open vector 0", "openText payload matches the vector");
    test::check(session->inboundCounter() == 1, "inbound counter advanced to 1");
    test::check(!session->openText(d2m0B64).has_value(), "replaying a frame is rejected");
    test::check(session->inboundCounter() == 1, "replay does not advance the inbound counter");

    // A frame from a later counter cannot be opened out of order.
    const auto& d2m5 = frameCase(p1c, "d2mTextCounter5");
    test::check(!session->openText(orca::crypto::base64Encode(fromHex(d2m5.at("frameHex").get<std::string>()))).has_value(),
                "an out-of-order future frame is rejected");
    test::check(session->inboundCounter() == 1, "out-of-order rejection leaves the counter untouched");

    // Tampering with any frame byte must not open and must not desync.
    const Bytes tampered = fromHex(d2m0.at("frameHex").get<std::string>());
    test::check(!session->openText(orca::crypto::base64Encode(tampered)).has_value(),
                "a tampered frame is rejected");

    // Binary payloads ride the same counters with kind=1.
    const auto& bin = frameCase(p1c, "m2dBinarySparse");
    // The session's outbound counter is now 2; the sparse vector used an
    // explicit counter, so only the explicit API can reach it.
    test::check(session->outboundCounter() == 2, "outbound counter is 2 after two seals");
    Bytes binPlaintext = fromHex(bin.at("plaintextHex").get<std::string>());
    const auto binaryFrame = sealFrameAt(schedule, Direction::kMobileToDesktop, PayloadKind::kBinary,
                                         0x0102030405060708ULL, binPlaintext);
    test::check(binaryFrame.has_value(), "sealFrameAt binary sparse counter");
    if (binaryFrame) {
        test::checkBytes(*binaryFrame, bin.at("frameHex").get<std::string>(), "binary frame matches the golden bytes");
    }
}

void testCounterArithmetic() {
    std::uint64_t counter = 0;
    test::check(advanceCounter(counter), "advanceCounter advances from 0");
    test::check(counter == 1, "advanceCounter produced 1");
    counter = kMaxCounter;
    test::check(!advanceCounter(counter), "advanceCounter refuses to wrap past 2^64-1");
    test::check(counter == kMaxCounter, "the maximum counter is preserved on refusal");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: e2ee_session_test <p1c-vectors.json> <p1-vectors.json>\n";
        return 2;
    }
    try {
        nlohmann::json p1c;
        {
            std::ifstream in(argv[1]);
            if (!in) throw std::runtime_error(std::string("cannot open ") + argv[1]);
            in >> p1c;
        }
        nlohmann::json p1;
        {
            std::ifstream in(argv[2]);
            if (!in) throw std::runtime_error(std::string("cannot open ") + argv[2]);
            in >> p1;
        }

        testBox(p1c);
        testSecretbox(p1c);
        testSchedule(p1c, p1);

        const auto& scheduleJson = p1c.at("schedule");
        E2eeKeySchedule schedule;
        schedule.mobileToDesktopKey = fromHex(scheduleJson.at("mobileToDesktopKeyHex").get<std::string>());
        schedule.desktopToMobileKey = fromHex(scheduleJson.at("desktopToMobileKeyHex").get<std::string>());
        schedule.sessionId = fromHex(scheduleJson.at("sessionIdHex").get<std::string>());
        {
            const auto hash = fromHex(scheduleJson.at("transcriptHashHex").get<std::string>());
            std::copy(hash.begin(), hash.end(), schedule.transcriptHash.begin());
        }

        testFrames(p1c, schedule);
        testSession(p1c, schedule);
        testCounterArithmetic();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: exception: " << error.what() << '\n';
        return 1;
    }
    return test::finish("e2ee_session_test");
}
