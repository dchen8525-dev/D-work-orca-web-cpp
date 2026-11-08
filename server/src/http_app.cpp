#include <orca/server/http_app.hpp>

#include <orca/crypto/scrypt.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace orca::server {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxLoginBodyBytes = 4 * 1024;
constexpr std::uint64_t kLoginWindowMs = 15 * 60 * 1000;
constexpr std::uint64_t kLoginMaxFailures = 10;
constexpr std::size_t kMinPasswordLength = 8;

// React writes style attributes and self-hosts everything else; wss to the
// serving origin is covered by 'self'.
const char* kContentSecurityPolicy =
    "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
    "connect-src 'self'; font-src 'self'; frame-ancestors 'none'; form-action 'self'; "
    "base-uri 'self'; object-src 'none'";

const char* contentTypeFor(const std::string& extension) {
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".js") return "text/javascript; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".webmanifest") return "application/manifest+json";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".ico") return "image/x-icon";
    if (extension == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

HttpResponse jsonResponse(int status, const json& body) {
    HttpResponse response;
    response.status = status;
    response.headers.emplace_back("Content-Type", "application/json; charset=utf-8");
    response.headers.emplace_back("Cache-Control", "no-store");
    response.body = body.dump();
    return response;
}

HttpResponse textResponse(int status, const char* contentType, std::string body) {
    HttpResponse response;
    response.status = status;
    response.headers.emplace_back("Content-Type", contentType);
    response.body = std::move(body);
    return response;
}

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// decodeURIComponent; nullopt on a malformed escape (served as 400).
std::optional<std::string> percentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size()) return std::nullopt;
        const auto hex = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int high = hex(text[i + 1]);
        const int low = hex(text[i + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return out;
}

std::optional<json> parseJsonBody(const std::optional<std::string>& body) {
    if (!body) return json::object();
    try {
        json parsed = json::parse(*body);
        if (!parsed.is_object()) return json::object();
        return parsed;
    } catch (const json::exception&) {
        return json::object();
    }
}

}  // namespace

HttpApp::HttpApp(ServerConfig config, SessionStore& sessions, HubCore& hub,
                 std::function<void(const std::string&)> log,
                 std::function<void(const std::string&)> applyPasswordHash)
    : config_(std::move(config)),
      sessions_(sessions),
      hub_(hub),
      log_(std::move(log)),
      applyPasswordHash_(std::move(applyPasswordHash)),
      startedAtMs_(nowMs()) {}

HttpResponse HttpApp::handle(const std::string& method, const std::string& target,
                             const std::optional<std::string>& cookie,
                             const std::optional<std::string>& body,
                             const std::string& clientIp) {
    // Strip the query string; routing is path-based.
    const std::size_t query = target.find('?');
    const std::string pathOnly =
        query == std::string::npos ? target : target.substr(0, query);

    HttpResponse response =
        pathOnly.rfind("/api/", 0) == 0 ? handleApi(method, pathOnly, cookie, body, clientIp)
                                        : serveStatic(pathOnly);
    response.headers.emplace_back("X-Content-Type-Options", "nosniff");
    response.headers.emplace_back("Referrer-Policy", "no-referrer");
    response.headers.emplace_back("Content-Security-Policy", kContentSecurityPolicy);
    return response;
}

HttpResponse HttpApp::handleApi(const std::string& method, const std::string& pathname,
                                const std::optional<std::string>& cookie,
                                const std::optional<std::string>& body,
                                const std::string& clientIp) {
    if (pathname == "/api/health") {
        return jsonResponse(200, json{{"ok", true},
                                      {"uptimeMs", nowMs() - startedAtMs_},
                                      {"gateway", hub_.getGatewayState()}});
    }

    if (pathname == "/api/session") {
        const auto token = readCookie(cookie, SessionStore::kCookieName);
        std::optional<SessionRecord> session =
            token ? sessions_.touch(*token) : std::nullopt;
        std::optional<SessionRecord> expiresAt = session;
        HttpResponse response;
        // The cookie's Max-Age is fixed at login while the store rolls; past half
        // the window, reissue so daily use never hits the login wall.
        if (session && token && nowMs() - session->createdAt > config_.sessionTtlMs / 2) {
            auto reissued = sessions_.refresh(*token);
            if (reissued) {
                expiresAt = reissued;
                response.headers.emplace_back(
                    "Set-Cookie",
                    sessionCookie(*token, config_.cookieSecure, config_.sessionTtlMs));
            }
        }
        json payload{{"authenticated", expiresAt.has_value()}};
        if (expiresAt) payload["expiresAt"] = expiresAt->expiresAt;
        return jsonResponse(200, payload);
    }

    if (pathname == "/api/login" && method == "POST") {
        const auto now = nowMs();
        const auto entry = loginFailures_.find(clientIp);
        if (entry != loginFailures_.end() && entry->second.resetAt > now &&
            entry->second.count >= kLoginMaxFailures) {
            return jsonResponse(429, json{{"ok", false}, {"error", "too_many_attempts"}});
        }
        const json parsed = parseJsonBody(body).value_or(json::object());
        std::string password;
        if (parsed.contains("password") && parsed.at("password").is_string()) {
            password = parsed.at("password").get<std::string>();
        }
        const bool verified =
            !password.empty() && orca::crypto::verifyPassword(password, config_.passwordHash);
        if (!verified) {
            const auto it = loginFailures_.find(clientIp);
            if (it == loginFailures_.end() || it->second.resetAt <= now) {
                loginFailures_[clientIp] = RateEntry{1, now + kLoginWindowMs};
            } else {
                it->second.count++;
            }
            // A spraying attacker rotating spoofed XFF values grows the map
            // without bound; expired rows are never read again, so drop them
            // once the table is large.
            if (loginFailures_.size() > 512) {
                for (auto stale = loginFailures_.begin(); stale != loginFailures_.end();) {
                    if (stale->second.resetAt <= now) {
                        stale = loginFailures_.erase(stale);
                    } else {
                        ++stale;
                    }
                }
            }
            return jsonResponse(401, json{{"ok", false}, {"error", "invalid_credentials"}});
        }
        loginFailures_.erase(clientIp);
        const auto issued = sessions_.create();
        HttpResponse response =
            jsonResponse(200, json{{"ok", true}});
        response.headers.emplace_back(
            "Set-Cookie",
            sessionCookie(issued.token, config_.cookieSecure, config_.sessionTtlMs));
        return response;
    }

    if (pathname == "/api/logout" && method == "POST") {
        if (const auto token = readCookie(cookie, SessionStore::kCookieName)) {
            sessions_.destroy(*token);
        }
        HttpResponse response = jsonResponse(200, json{{"ok", true}});
        response.headers.emplace_back("Set-Cookie", clearedSessionCookie(config_.cookieSecure));
        return response;
    }

    if (pathname == "/api/password" && method == "POST") {
        const auto token = readCookie(cookie, SessionStore::kCookieName);
        if (!token || !sessions_.touch(*token)) {
            return jsonResponse(401, json{{"ok", false}, {"error", "unauthenticated"}});
        }
        if (!applyPasswordHash_) {
            return jsonResponse(
                501, json{{"ok", false}, {"error", "password_change_unsupported"}});
        }
        const json parsed = parseJsonBody(body).value_or(json::object());
        std::string currentPassword;
        std::string newPassword;
        if (parsed.contains("current") && parsed.at("current").is_string()) {
            currentPassword = parsed.at("current").get<std::string>();
        }
        if (parsed.contains("new") && parsed.at("new").is_string()) {
            newPassword = parsed.at("new").get<std::string>();
        }
        if (currentPassword.empty() ||
            !orca::crypto::verifyPassword(currentPassword, config_.passwordHash)) {
            return jsonResponse(401, json{{"ok", false}, {"error", "invalid_credentials"}});
        }
        if (newPassword.size() < kMinPasswordLength) {
            return jsonResponse(400, json{{"ok", false}, {"error", "new_password_too_short"}});
        }
        const std::string encoded = orca::crypto::hashPassword(newPassword);
        try {
            applyPasswordHash_(encoded);
        } catch (const std::exception& error) {
            if (log_) log_(std::string("password change failed: ") + error.what());
            return jsonResponse(500, json{{"ok", false}, {"error", "persist_failed"}});
        }
        // In-memory and on disk agree now; retire every session, keep the caller's.
        config_.passwordHash = encoded;
        sessions_.clear();
        const auto fresh = sessions_.create();
        HttpResponse response = jsonResponse(200, json{{"ok", true}});
        response.headers.emplace_back(
            "Set-Cookie",
            sessionCookie(fresh.token, config_.cookieSecure, config_.sessionTtlMs));
        if (log_) log_("password changed");
        return response;
    }

    return jsonResponse(404, json{{"ok", false}, {"error", "not_found"}});
}

HttpResponse HttpApp::serveStatic(const std::string& pathname) const {
    if (!config_.staticDir) {
        return textResponse(404, "text/plain; charset=utf-8",
                            "orca-web server is running; no web build configured\n");
    }
    // A malformed % escape must not take the process down.
    const auto decoded = percentDecode(pathname);
    if (!decoded) {
        return textResponse(400, "text/plain; charset=utf-8", "bad request\n");
    }
    const std::filesystem::path root = std::filesystem::weakly_canonical(*config_.staticDir);
    std::filesystem::path relative(*decoded);
    // Normalize and strip leading traversal segments, then verify the result
    // stays inside root (startsWith alone would admit a sibling root like
    // /app2 when this root is /app).
    relative = relative.lexically_normal();
    auto relativeText = relative.generic_string();
    // A URL path always starts with "/". On Windows that is not an absolute
    // path (no drive), so `root / "/app.js"` resolves against the drive's
    // current directory and every asset would fall back to index.html.
    while (!relativeText.empty() && relativeText.front() == '/') relativeText.erase(0, 1);
    while (relativeText.rfind("../", 0) == 0) relativeText = relativeText.substr(3);
    std::filesystem::path requested = (root / relativeText).lexically_normal();
    const auto rootText = root.generic_string();
    if (!rootText.empty() && requested.generic_string().rfind(rootText, 0) != 0) {
        requested = root / "index.html";
    }

    std::filesystem::path target;
    std::error_code ec;
    if (std::filesystem::is_regular_file(requested, ec)) {
        target = requested;
    } else {
        target = root / "index.html";
    }
    if (!std::filesystem::is_regular_file(target, ec)) {
        return textResponse(404, "text/plain; charset=utf-8", "not found\n");
    }

    HttpResponse response;
    response.status = 200;
    const std::string extension = lowerAscii(target.extension().string());
    response.fileContentType = contentTypeFor(extension);
    // index.html must never be cached: a cached index pointing at replaced
    // assets breaks the app until a hard refresh.
    response.fileNoStore = extension == ".html";
    response.fileTarget = target;
    return response;
}

}  // namespace orca::server
