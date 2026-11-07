#include <orca/platform/paths.hpp>

#include <cstdlib>

namespace orca {

namespace {

const char* envOr(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

}  // namespace

std::filesystem::path appDataDir() {
#if defined(_WIN32)
    return std::filesystem::path(envOr("APPDATA", "."));
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::filesystem::path(xdg);
    return std::filesystem::path(envOr("HOME", ".")) / ".config";
#endif
}

std::filesystem::path defaultGatewayConfigPath() {
    return appDataDir() / "orca-web" / "gateway.json";
}

std::filesystem::path defaultGatewayExePath() {
    return appDataDir() / "orca-web" / "orca-gateway.exe";
}

std::filesystem::path defaultGatewayLogPath() {
    return appDataDir() / "orca-web" / "gateway.log";
}

std::filesystem::path defaultServerConfigPath() {
#if defined(_WIN32)
    return std::filesystem::path("server.json");
#else
    return std::filesystem::path("/etc/orca-web/server.json");
#endif
}

std::filesystem::path defaultSessionStorePath() {
#if defined(_WIN32)
    return appDataDir() / "orca-web" / "sessions.json";
#else
    return std::filesystem::path("/var/lib/orca-web/sessions.json");
#endif
}

std::filesystem::path defaultStaticDir() {
#if defined(_WIN32)
    return std::filesystem::path("web");
#else
    return std::filesystem::path("/opt/orca-web/web");
#endif
}

}  // namespace orca
