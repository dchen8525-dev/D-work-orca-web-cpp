#pragma once

#include "config_store.hpp"

#include <filesystem>
#include <string>

namespace orca::ui {

struct LocalOrcaPairing {
    OrcaPairing pairing;   // endpoint (loopback), deviceToken, hostPublicKeyB64
    std::string deviceId;
    std::string deviceName;
};

// Dev-only: read the pairing material a running desktop Orca persisted on this
// machine, mirroring src/gateway/orca/local-pairing.ts. Reads
//   <userDataPath>/orca-runtime.json      (websocket transport)
//   <userDataPath>/orca-devices.json      (newest mobile-scope device)
//   <userDataPath>/orca-e2ee-keypair.json (pinned host key)
// The 0.0.0.0 bind address is rewritten to 127.0.0.1. Throws std::runtime_error
// with a user-facing message when any file is missing or has no usable entry.
// Never log the returned token.
LocalOrcaPairing readLocalOrcaPairing(const std::filesystem::path& userDataPath);

}  // namespace orca::ui
