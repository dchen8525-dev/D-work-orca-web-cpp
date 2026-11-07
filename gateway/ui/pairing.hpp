#pragma once

#include <string>
#include <string_view>

namespace orca::ui {

struct PairingOffer {
    int version = 0;
    std::string endpoint;
    std::string deviceToken;
    std::string publicKeyB64;
    std::string scope;
    std::string pairedDeviceId;
};

// Accepts either the deep link (orca://pair?code=...) or the bare base64url
// code. Mirrors decodePairingCode() in apps/gateway/src/config.ts:178-208,
// including the 4 mandatory field checks. Throws std::runtime_error on any
// invalid input so the caller can show the message verbatim.
PairingOffer decodePairingCode(std::string_view input);

// The advertised LAN endpoint is less stable than loopback: keep the port and
// path but force 127.0.0.1. Mirrors loopbackEndpoint() in config.ts:211-218.
std::string loopbackEndpoint(std::string_view endpoint);

}  // namespace orca::ui
