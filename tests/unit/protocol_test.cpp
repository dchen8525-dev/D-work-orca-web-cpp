#include <orca/crypto/base64.hpp>
#include <orca/crypto/bytes.hpp>
#include <orca/crypto/hkdf.hpp>
#include <orca/crypto/sha256.hpp>
#include <orca/protocol/canonical_json.hpp>
#include <orca/protocol/capabilities.hpp>
#include <orca/protocol/e2ee_handshake.hpp>
#include <orca/protocol/frames.hpp>
#include <orca/protocol/seal_frame.hpp>
#include <orca/protocol/transcript.hpp>
#include <orca/protocol/versions.hpp>

#include "test_util.hpp"

#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace orca::crypto;
using json = nlohmann::json;
using namespace orca::protocol;

namespace {

Bytes fromHex(const std::string& text) {
    Bytes out;
    if (!hexDecode(text, out)) throw std::runtime_error("bad hex in vectors: " + text);
    return out;
}

std::uint64_t u64FromHex(const std::string& text) {
    Bytes raw;
    if (!hexDecode(text, raw) || raw.size() != 8) {
        throw std::runtime_error("bad uint64 hex in vectors: " + text);
    }
    std::uint64_t value = 0;
    for (const auto byte : raw) value = (value << 8) | byte;
    return value;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: orca_protocol_tests <p1-vectors.json>\n";
        return 1;
    }
    std::ifstream stream(argv[1]);
    if (!stream) {
        std::cerr << "FAIL: cannot open vectors file: " << argv[1] << '\n';
        return 1;
    }
    const json v = json::parse(stream);

    TranscriptInput input;
    input.clientPublicKey = fromHex(v["input"]["clientPublicKeyHex"].get<std::string>());
    input.clientNonce = fromHex(v["input"]["clientNonceHex"].get<std::string>());
    input.hostPublicKey = fromHex(v["input"]["hostPublicKeyHex"].get<std::string>());
    input.hostNonce = fromHex(v["input"]["hostNonceHex"].get<std::string>());
    input.transport = v["input"]["transport"].get<std::string>();
    input.relayHostId = v["input"]["relayHostId"].get<std::string>();
    const Bytes sharedSecret = fromHex(v["input"]["sharedSecretHex"].get<std::string>());

    // 24-field transcript, byte for byte against the independent JS implementation.
    const Bytes transcript = encodeTranscript(input);
    test::checkBytes(transcript, v["transcriptHex"].get<std::string>(), "E2EE v2 transcript (24 fields)");
    const Hash256 transcriptHash = sha256(transcript);
    test::checkBytes(Bytes(transcriptHash.begin(), transcriptHash.end()),
                     v["transcriptHashHex"].get<std::string>(), "transcript hash");

    // salt / info are derived independently here (deriveE2eeSchedule does the
    // same internally, so these also localise a failure to a single step).
    Bytes saltInput;
    appendRaw(saltInput, kSaltLabel, kSaltLabelSize);
    appendRaw(saltInput, input.clientNonce);
    appendRaw(saltInput, input.hostNonce);
    const Hash256 saltHash = sha256(saltInput);
    const Bytes salt(saltHash.begin(), saltHash.end());
    test::checkBytes(salt, v["saltHex"].get<std::string>(), "HKDF salt (label keeps NUL)");

    Bytes info;
    appendRaw(info, kInfoLabel, kInfoLabelSize);
    appendRaw(info, transcriptHash.data(), transcriptHash.size());
    test::checkBytes(info, v["infoHex"].get<std::string>(), "HKDF info (label keeps NUL)");

    const Bytes expanded = hkdfSha256(sharedSecret, salt, info, 96);
    test::checkBytes(expanded, v["hkdfExpandedHex"].get<std::string>(), "HKDF 96-byte expansion");

    const auto schedule = deriveE2eeSchedule(sharedSecret, input.clientNonce,
                                             input.hostNonce, transcript);
    test::checkBytes(schedule.mobileToDesktopKey, v["mobileToDesktopKeyHex"].get<std::string>(),
                     "mobile-to-desktop key");
    test::checkBytes(schedule.desktopToMobileKey, v["desktopToMobileKeyHex"].get<std::string>(),
                     "desktop-to-mobile key");
    test::checkBytes(schedule.sessionId, v["sessionIdHex"].get<std::string>(), "session id");

    // canonicalize / fingerprint parity with apps/web/src/envelope.ts.
    const json canonicalInput = v["canonicalInput"];
    const std::string canonical = canonicalize(canonicalInput);
    test::check(canonical == v["canonical"].get<std::string>(), "canonical JSON vs JS");
    const std::string fingerprint = payloadFingerprint(
        canonicalInput["method"].get<std::string>(),
        canonicalInput["sessionId"].get<std::string>(),
        canonicalInput["fields"]);
    test::check(fingerprint == v["fingerprintHex"].get<std::string>(),
                "payload fingerprint vs JS");

    // Wire frame guards, classification and round-trip.
    test::check(!parseFrame("not json"), "parseFrame rejects invalid JSON");
    test::check(!parseFrame("[]"), "parseFrame rejects non-object");
    test::check(!parseFrame("{}"), "parseFrame rejects missing type");
    test::check(!parseFrame("{\"type\":7}"), "parseFrame rejects non-string type");

    const json request = makeRequest("r1", "project.list", json::object(), 30000);
    const auto parsedRequest = parseFrame(request.dump());
    test::check(parsedRequest && kindOf(*parsedRequest) == FrameKind::kRequest,
                "request frame classified");
    test::check(parsedRequest && frameId(*parsedRequest).value_or("") == "r1",
                "request id extracted");
    test::check(parsedRequest && frameMethod(*parsedRequest).value_or("") == "project.list",
                "request method extracted");
    test::check(parsedRequest && (*parsedRequest)["timeoutMs"] == 30000,
                "request timeoutMs preserved");
    test::check(isWebToHub(FrameKind::kRequest) && isHubToGateway(FrameKind::kRequest),
                "request is valid in both directions");
    test::check(!isHubToWeb(FrameKind::kRequest), "request is not a hub->web frame");

    const json noTimeout = makeRequest("r2", "worktree.ps", json::object());
    test::check(!noTimeout.contains("timeoutMs"), "optional timeoutMs omitted");
    test::check(noTimeout.contains("params") && noTimeout["params"].is_object(),
                "empty params kept as object");
    const json noParams = makeRequest("r3", "status.get", json());
    test::check(!noParams.contains("params"), "null params omitted");

    const json welcome = makeClientWelcome(true, "connected");
    test::check(welcome["v"] == kWebProtocolVersion && welcome["gatewayConnected"] == true,
                "client welcome shape");
    const json hello = makeHello("key", "home-pc", "connected");
    test::check(hello["v"] == kLinkProtocolVersion && hello["gatewayId"] == "home-pc",
                "gateway hello shape");

    const auto reset = parseFrame(makeStreamReset("s1").dump());
    test::check(reset && isHubToWeb(FrameKind::kStreamReset) &&
                    isGatewayToHub(FrameKind::kStreamReset),
                "stream-reset valid in both directions");
    test::check(makePing()["type"] == "ping" && makePong()["type"] == "pong",
                "ping/pong constructors");
    test::check(std::string(kindName(FrameKind::kRequestResult)) == "request-result",
                "kindName round-trip");

    // Mandatory per-socket capability replay: 17 entries, order is wire-visible.
    const json caps = v["clientCapabilities"];
    test::check(v["capabilitiesCount"].get<int>() == static_cast<int>(kClientCapabilityCount),
                "capability count agrees with the TypeScript source (17)");
    test::check(caps.is_array() && caps.size() == kClientCapabilityCount,
                "golden capability array has 17 entries");
    bool allCapsMatch = true;
    for (std::size_t i = 0; i < kClientCapabilityCount; ++i) {
        allCapsMatch = allCapsMatch &&
                       caps[i].get<std::string>() == std::string(kClientCapabilities[i]);
    }
    test::check(allCapsMatch, "all 17 capability strings match the golden vector in order");
    json capList = json::array();
    for (const auto capability : kClientCapabilities) capList.push_back(std::string(capability));
    test::check(hexEncode(sha256(capList.dump())) ==
                    v["capabilitiesSha256Hex"].get<std::string>(),
                "capability array digest vs independent JS implementation");
    test::check(makeClientCapabilitiesParams()["clientCapabilities"] == caps,
                "clientCapabilities params shape");
    const json capRequest = makeClientCapabilitiesRequest("cap-1", "device-token");
    test::check(capRequest["id"] == "cap-1" &&
                    capRequest["deviceToken"] == "device-token" &&
                    capRequest["method"] == kClientCapabilitiesMethod &&
                    capRequest["params"]["clientCapabilities"] == caps,
                "capability replay request shape (id/deviceToken/method/params)");

    // E2EE v2 sealed-frame byte geometry: pure bytes, no libsodium.
    static_assert(kHeaderLength == 42, "header length is a wire constant");
    static_assert(kNonceLength == 24, "nonce length is a wire constant");
    static_assert(kMinSealedFrameLength == 82, "minimum sealed frame length");
    const json seal = v["sealFrames"];
    test::check(seal["minSealedFrameLength"].get<int>() ==
                    static_cast<int>(kMinSealedFrameLength),
                "minimum sealed frame length = 24 + 16 + 42 = 82");
    const auto sealSessionId = toSessionId(fromHex(seal["sessionIdHex"].get<std::string>()));
    test::check(sealSessionId.has_value(), "HKDF session id converts to fixed 32-byte SessionId");
    test::check(!toSessionId(Bytes(31, 0)).has_value() &&
                    !toSessionId(Bytes(33, 0)).has_value(),
                "toSessionId rejects wrong lengths");
    test::check(static_cast<std::uint8_t>(Direction::kMobileToDesktop) == 0 &&
                    static_cast<std::uint8_t>(Direction::kDesktopToMobile) == 1 &&
                    static_cast<std::uint8_t>(PayloadKind::kText) == 0 &&
                    static_cast<std::uint8_t>(PayloadKind::kBinary) == 1,
                "direction / kind wire values are 0 / 1");

    for (const auto& c : seal["cases"]) {
        FrameDescriptor d{};
        d.sessionId = *sealSessionId;
        d.direction = c["direction"].get<int>() == 0 ? Direction::kMobileToDesktop
                                                     : Direction::kDesktopToMobile;
        d.kind = c["kind"].get<int>() == 0 ? PayloadKind::kText : PayloadKind::kBinary;
        d.counter = u64FromHex(c["counterHex"].get<std::string>());
        const std::string label = c["name"].get<std::string>();

        const Bytes header = encodeHeader(d);
        const Bytes nonce = encodeNonce(d);
        test::checkBytes(header, c["headerHex"].get<std::string>(), label + ": 42-byte header");
        test::checkBytes(nonce, c["nonceHex"].get<std::string>(), label + ": 24-byte nonce");

        const auto roundTrip = decodeHeader(header.data(), header.size());
        test::check(roundTrip && roundTrip->sessionId == d.sessionId &&
                        roundTrip->direction == d.direction && roundTrip->kind == d.kind &&
                        roundTrip->counter == d.counter,
                    label + ": decodeHeader round-trip");

        // Synthetic minimal frame: nonce(24) || cipher-with-MAC(16) || header(42).
        Bytes frame(nonce);
        frame.insert(frame.end(), kSecretboxMacLength, 0);
        frame.insert(frame.end(), header.begin(), header.end());
        test::check(hasValidFrameShape(frame) && nonceMatches(frame, d),
                    label + ": nonceMatches on a minimal frame");
        Bytes tampered(frame);
        tampered[5] ^= 0x01;
        test::check(!nonceMatches(tampered, d), label + ": tampered nonce byte rejected");
        test::check(!hasValidFrameShape(Bytes(frame.begin(), frame.end() - 1)),
                    label + ": 81-byte frame rejected by shape guard");

        Bytes plaintext(header);
        plaintext.push_back(0x7b);  // '{' of a JSON payload
        test::check(headerMatches(plaintext, d), label + ": headerMatches on opened plaintext");
        plaintext[kHeaderLength - 1] ^= 1;
        test::check(!headerMatches(plaintext, d), label + ": tampered header rejected");
        test::check(!headerMatches(Bytes(header.begin(), header.end() - 1), d),
                    label + ": truncated plaintext rejected");
    }

    Bytes badHeader = encodeHeader(FrameDescriptor{});
    badHeader[kSessionIdLength] = 2;
    test::check(!decodeHeader(badHeader.data(), badHeader.size()).has_value(),
                "decodeHeader rejects an unknown direction byte");
    badHeader = encodeHeader(FrameDescriptor{});
    badHeader[kSessionIdLength + 1] = 7;
    test::check(!decodeHeader(badHeader.data(), badHeader.size()).has_value(),
                "decodeHeader rejects an unknown payload kind byte");

    std::uint64_t counter = 0;
    test::check(advanceCounter(counter) && counter == 1, "counter advances 0 -> 1");
    counter = kMaxCounter;
    test::check(!advanceCounter(counter) && counter == kMaxCounter,
                "MAX_COUNTER refuses to wrap (connection must re-handshake)");
    test::check(u64FromHex(seal["maxCounterHex"].get<std::string>()) == kMaxCounter,
                "MAX_COUNTER vector is 2^64-1");

    // Plaintext E2EE v2 handshake bodies (sealing itself still needs libsodium).
    const json hs = v["handshake"];
    E2eeContext context;
    const auto e2eeHello = makeE2eeHello(input.clientPublicKey, input.clientNonce, context);
    test::check(e2eeHello.has_value() && *e2eeHello == hs["hello"],
                "e2ee_hello body vs the TypeScript constructor");
    test::check(!makeE2eeHello(Bytes(31, 0), input.clientNonce, context).has_value() &&
                    !makeE2eeHello(input.clientPublicKey, Bytes(33, 0), context).has_value(),
                "makeE2eeHello rejects non-32-byte key/nonce");
    test::check(!(*e2eeHello)["context"].contains("relayHostId"),
                "direct context OMITS relayHostId (undefined !== '')");
    E2eeContext relayContext;
    relayContext.transport = "relay";
    relayContext.relayHostId = "relay-1";
    const auto relayHello = makeE2eeHello(input.clientPublicKey, input.clientNonce, relayContext);
    test::check(relayHello && (*relayHello)["context"]["relayHostId"] == "relay-1" &&
                    (*relayHello)["context"]["transport"] == "relay",
                "relay context carries relayHostId");

    const std::string hashB64 = hs["transcriptHashB64"].get<std::string>();
    test::check(transcriptHashB64(transcriptHash) == hashB64,
                "transcriptHashB64 matches the independent JS vector");
    test::check(makeE2eeAuth(hashB64, hs["deviceToken"].get<std::string>()) == hs["auth"],
                "e2ee_auth body vs authText()");
    test::check(isE2eeAuthenticated(hs["authenticated"], hashB64),
                "e2ee_authenticated accepted");
    test::check(!isE2eeAuthenticated(json::array(), hashB64),
                "e2ee_authenticated rejects a non-object reply");
    json badReply = hs["authenticated"];
    badReply["transcriptHashB64"] = "wrong";
    test::check(!isE2eeAuthenticated(badReply, hashB64),
                "e2ee_authenticated rejects a transcript hash mismatch");
    badReply = hs["authenticated"];
    badReply["v"] = 1;
    test::check(!isE2eeAuthenticated(badReply, hashB64),
                "e2ee_authenticated rejects v != 2");

    test::check(sameContext(hs["ready"]["context"], (*e2eeHello)["context"]),
                "sameContext accepts identical direct contexts");
    json emptyRelay = (*e2eeHello)["context"];
    emptyRelay["relayHostId"] = "";
    test::check(!sameContext(emptyRelay, (*e2eeHello)["context"]),
                "sameContext: undefined !== '' for relayHostId");

    // validateReady() is the MITM gate. Every single mutation must be refused.
    const json ready = hs["ready"];
    const Bytes pinnedHostKey = input.hostPublicKey;
    const auto accepted = acceptE2eeReady(ready, *e2eeHello, pinnedHostKey);
    test::check(accepted.has_value() &&
                    accepted->hostPublicKey == input.hostPublicKey &&
                    accepted->hostNonce == input.hostNonce,
                "e2ee_ready accepted with a matching pinned host key");

    auto rejected = [&](const std::string& label, auto mutate) {
        json bad = ready;
        mutate(bad);
        test::check(!acceptE2eeReady(bad, *e2eeHello, pinnedHostKey).has_value(), label);
    };
    rejected("ready: wrong type", [](json& j) { j["type"] = "e2ee_hello"; });
    rejected("ready: wrong version", [](json& j) { j["v"] = 3; });
    rejected("ready: version as string", [](json& j) { j["v"] = "2"; });
    rejected("ready: client nonce echo mismatch", [](json& j) { j["clientNonceB64"] = "AAAA"; });
    rejected("ready: framing != 2", [](json& j) { j["selection"]["framing"] = 3; });
    rejected("ready: payloadKinds reordered", [](json& j) {
        j["selection"]["payloadKinds"] = json::parse(R"(["binary","text"])");
    });
    rejected("ready: missing selection", [](json& j) { j.erase("selection"); });
    rejected("ready: context protocol changed", [](json& j) { j["context"]["protocol"] = "other"; });
    rejected("ready: context gains relayHostId='' ", [](json& j) { j["context"]["relayHostId"] = ""; });
    rejected("ready: missing context", [](json& j) { j.erase("context"); });
    rejected("ready: host key swapped (pin mismatch)", [](json& j) {
        j["desktopPublicKeyB64"] = base64Encode(Bytes(32, 0x01));
    });
    rejected("ready: non-canonical base64", [](json& j) { j["desktopNonceB64"] = "not-base64!"; });
    rejected("ready: host nonce is 31 bytes", [](json& j) {
        j["desktopNonceB64"] = base64Encode(Bytes(31, 0x42));
    });
    rejected("ready: missing desktop key", [](json& j) { j.erase("desktopPublicKeyB64"); });
    test::check(!acceptE2eeReady(json::array(), *e2eeHello, pinnedHostKey).has_value(),
                "ready: non-object frame rejected");
    test::check(!acceptE2eeReady(ready, *e2eeHello, Bytes(31, 0)).has_value(),
                "ready: malformed pinned host key rejected");

    return test::finish("protocol tests");
}
