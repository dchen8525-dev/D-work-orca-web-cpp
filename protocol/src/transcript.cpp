#include <orca/protocol/transcript.hpp>

#include <orca/protocol/versions.hpp>

namespace orca::protocol {

using orca::crypto::Bytes;
using orca::crypto::appendField;
using orca::crypto::fieldValue;
using orca::crypto::numberList;
using orca::crypto::stringList;

Bytes encodeTranscript(const TranscriptInput& input) {
    const Bytes framingList = numberList({2});
    const Bytes payloadKinds = stringList({"text", "binary"});
    const std::uint32_t version = 2;

    Bytes out;
    // 1. domain
    appendField(out, "domain", fieldValue(kTranscriptDomain));

    // 2-12. mobile-to-desktop (hello): 11 fields
    appendField(out, "mobile-to-desktop.type", fieldValue("e2ee_hello"));
    appendField(out, "mobile-to-desktop.version", fieldValue(version));
    appendField(out, "mobile-to-desktop.client-public-key", input.clientPublicKey);
    appendField(out, "mobile-to-desktop.client-nonce", input.clientNonce);
    appendField(out, "mobile-to-desktop.capabilities.framing", framingList);
    appendField(out, "mobile-to-desktop.capabilities.payload-kinds", payloadKinds);
    appendField(out, "mobile-to-desktop.context.protocol", fieldValue(kE2eeProtocol));
    appendField(out, "mobile-to-desktop.context.initiator", fieldValue("mobile"));
    appendField(out, "mobile-to-desktop.context.responder", fieldValue("desktop"));
    appendField(out, "mobile-to-desktop.context.transport", fieldValue(input.transport));
    appendField(out, "mobile-to-desktop.context.relay-host-id", fieldValue(input.relayHostId));

    // 13-24. desktop-to-mobile (ready): 12 fields
    appendField(out, "desktop-to-mobile.type", fieldValue("e2ee_ready"));
    appendField(out, "desktop-to-mobile.version", fieldValue(version));
    appendField(out, "desktop-to-mobile.desktop-public-key", input.hostPublicKey);
    appendField(out, "desktop-to-mobile.client-nonce-echo", input.clientNonce);
    appendField(out, "desktop-to-mobile.desktop-nonce", input.hostNonce);
    appendField(out, "desktop-to-mobile.selection.framing", fieldValue(version));
    appendField(out, "desktop-to-mobile.selection.payload-kinds", payloadKinds);
    appendField(out, "desktop-to-mobile.context.protocol", fieldValue(kE2eeProtocol));
    appendField(out, "desktop-to-mobile.context.initiator", fieldValue("mobile"));
    appendField(out, "desktop-to-mobile.context.responder", fieldValue("desktop"));
    appendField(out, "desktop-to-mobile.context.transport", fieldValue(input.transport));
    appendField(out, "desktop-to-mobile.context.relay-host-id", fieldValue(input.relayHostId));

    return out;
}

}  // namespace orca::protocol
