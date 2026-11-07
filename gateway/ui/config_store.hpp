#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace orca::ui {

struct OrcaPairing {
    std::string endpoint;
    std::string deviceToken;
    std::string hostPublicKeyB64;

    bool empty() const noexcept {
        return endpoint.empty() && deviceToken.empty() && hostPublicKeyB64.empty();
    }
};

// Mirrors ConfigUpdates in apps/gateway/src/config.ts:95-99. An empty field
// means "leave this value alone" when passed to ConfigStore::apply().
struct ConfigUpdate {
    std::string serverUrl;
    std::string gatewayKey;
    OrcaPairing orca;

    bool empty() const noexcept {
        return serverUrl.empty() && gatewayKey.empty() && orca.empty();
    }
};

struct ConfigSnapshot {
    std::string serverUrl;
    std::string gatewayKey;
    OrcaPairing orca;
    std::string raw;         // exact file text for the "view config" pane
    std::string configPath;
    bool exists = false;
    std::string error;       // non-empty when the file could not be read/parsed
};

// ws:// or wss:// only; a path other than /gateway gets /gateway appended.
// Mirrors normalizeHubUrl() in config.ts:154-166 with a small hand-written
// parser (no URL library), so unusual URL forms may differ from JS `new URL`.
std::string normalizeHubUrl(std::string_view value);

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path);

    const std::filesystem::path& path() const noexcept { return path_; }

    // Never throws: read failures are reported through ConfigSnapshot::error.
    ConfigSnapshot read() const;

    // Merge only the supplied fields, preserve unknown keys, then persist
    // atomically (.tmp -> rename). Throws std::runtime_error on bad input/I/O.
    std::string apply(const ConfigUpdate& update);

private:
    std::filesystem::path path_;
};

}  // namespace orca::ui
