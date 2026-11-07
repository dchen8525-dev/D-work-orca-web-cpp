#include <orca/server/server_config.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace orca::server {

namespace {

using nlohmann::json;

std::optional<std::string> envValue(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
}

bool envFlag(const char* name, bool fallback) {
    const auto value = envValue(name);
    if (!value) return fallback;
    return *value == "1" || *value == "true";
}

std::string requireOne(const std::optional<std::string>& value, const char* label) {
    if (!value || value->empty()) {
        throw std::runtime_error(std::string("Missing ") + label +
                                 ": set it in the server config file or the environment");
    }
    return *value;
}

std::uint64_t sessionTtlMs(const json* file) {
    std::uint64_t days = 30;
    if (const auto envDays = envValue("ORCA_WEB_SESSION_TTL_DAYS")) {
        days = static_cast<std::uint64_t>(std::stoull(*envDays));
    } else if (file != nullptr && file->contains("sessionTtlDays") &&
               file->at("sessionTtlDays").is_number()) {
        days = file->at("sessionTtlDays").get<std::uint64_t>();
    }
    return days * 24 * 60 * 60 * 1000;
}

std::optional<std::string> stringOr(const json* file, const char* key,
                                    const std::optional<std::string>& env) {
    if (env) return env;
    if (file != nullptr && file->contains(key) && file->at(key).is_string()) {
        return file->at(key).get<std::string>();
    }
    return std::nullopt;
}

}  // namespace

ServerConfig loadServerConfig(const std::optional<std::filesystem::path>& path) {
    // Env override first, file second, default last — same precedence as config.ts.
    std::optional<json> file;
    if (path) {
        std::ifstream in(*path);
        if (!in) throw std::runtime_error("Could not read config " + path->string());
        try {
            json parsed;
            in >> parsed;
            if (!parsed.is_object()) throw std::runtime_error("config root is not an object");
            file = std::move(parsed);
        } catch (const json::exception& error) {
            throw std::runtime_error("Could not read config " + path->string() + ": " +
                                     error.what());
        }
    }
    const json* f = file ? &*file : nullptr;

    ServerConfig config;
    if (const auto host = stringOr(f, "host", envValue("ORCA_WEB_HOST"))) config.host = *host;
    if (const auto port = envValue("ORCA_WEB_PORT")) {
        config.port = static_cast<std::uint16_t>(std::stoul(*port));
    } else if (f != nullptr && f->contains("port") && f->at("port").is_number()) {
        config.port = f->at("port").get<std::uint16_t>();
    }
    config.publicOrigin = stringOr(f, "publicOrigin", envValue("ORCA_WEB_PUBLIC_ORIGIN"));
    config.gatewayKey = requireOne(stringOr(f, "gatewayKey", envValue("ORCA_WEB_GATEWAY_KEY")),
                                   "gatewayKey");
    config.passwordHash = requireOne(stringOr(f, "passwordHash", envValue("ORCA_WEB_PASSWORD_HASH")),
                                     "passwordHash");
    config.sessionTtlMs = sessionTtlMs(f);
    config.sessionStorePath =
        stringOr(f, "sessionStorePath", envValue("ORCA_WEB_SESSION_STORE"));
    config.staticDir = stringOr(f, "staticDir", envValue("ORCA_WEB_STATIC_DIR"));
    config.trustProxy = envFlag("ORCA_WEB_TRUST_PROXY",
                                f != nullptr && f->contains("trustProxy") &&
                                    f->at("trustProxy").is_boolean() &&
                                    f->at("trustProxy").get<bool>());
    const bool secureFromFile = f != nullptr && f->contains("cookieSecure") &&
                                f->at("cookieSecure").is_boolean() &&
                                f->at("cookieSecure").get<bool>();
    // C++17: std::string::starts_with is C++20; rfind(p, 0) == 0 is the idiom.
    const bool originIsHttps =
        config.publicOrigin && config.publicOrigin->rfind("https://", 0) == 0;
    config.cookieSecure = envFlag("ORCA_WEB_COOKIE_SECURE", secureFromFile || originIsHttps);
    return config;
}

}  // namespace orca::server
