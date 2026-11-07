#pragma once

#include <orca/server/hub_core.hpp>
#include <orca/server/server_config.hpp>
#include <orca/server/session_store.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orca::server {

// HTTP surface: login/logout/session/password/health plus the static app.
// Mirrors apps/server/src/http-app.ts; the IO layer (boost::beast) translates
// between real sockets and this plain-function interface.
struct HttpResponse {
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    // Static files are streamed by the IO layer; body stays empty.
    std::optional<std::filesystem::path> fileTarget;
    std::string fileContentType;
    bool fileNoStore = false;
};

class HttpApp {
public:
    // applyPasswordHash persists the rotated hash into the config file. A
    // handler without it still verifies the current password but answers 501
    // for changes (same as the TS --config-less dev mode).
    HttpApp(ServerConfig config, SessionStore& sessions, HubCore& hub,
            std::function<void(const std::string&)> log,
            std::function<void(const std::string&)> applyPasswordHash = nullptr);

    // method/target per HTTP; cookie/body/ip may be absent. target must be the
    // raw (still percent-encoded) request target.
    HttpResponse handle(const std::string& method, const std::string& target,
                        const std::optional<std::string>& cookie,
                        const std::optional<std::string>& body,
                        const std::string& clientIp);

private:
    struct RateEntry {
        std::uint64_t count = 0;
        std::uint64_t resetAt = 0;
    };

    HttpResponse handleApi(const std::string& method, const std::string& pathname,
                           const std::optional<std::string>& cookie,
                           const std::optional<std::string>& body, const std::string& clientIp);
    HttpResponse serveStatic(const std::string& pathname) const;

    ServerConfig config_;
    SessionStore& sessions_;
    HubCore& hub_;
    std::function<void(const std::string&)> log_;
    std::function<void(const std::string&)> applyPasswordHash_;
    std::map<std::string, RateEntry> loginFailures_;
    std::uint64_t startedAtMs_;
};

}  // namespace orca::server
