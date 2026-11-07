#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace orca::server {

// Mirrors ServerConfig in apps/server/src/config.ts. Field names, defaults and
// env overrides (ORCA_WEB_*) are kept identical for operational parity.
struct ServerConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8788;
    // Browser-facing origin (https://...), also the /client WS Origin check.
    std::optional<std::string> publicOrigin;
    std::string gatewayKey;
    std::string passwordHash;
    std::uint64_t sessionTtlMs = 30ull * 24 * 60 * 60 * 1000;
    // Without it every restart logs the browser out.
    std::optional<std::string> sessionStorePath;
    std::optional<std::string> staticDir;
    // Trust X-Forwarded-For for login rate limiting (behind Caddy/nginx).
    bool trustProxy = false;
    // Secure cookies follow publicOrigin unless explicitly set.
    bool cookieSecure = false;
};

// Throws std::runtime_error when gatewayKey or passwordHash are missing, or
// the file cannot be parsed — same contract as loadServerConfig().
ServerConfig loadServerConfig(const std::optional<std::filesystem::path>& path);

}  // namespace orca::server
