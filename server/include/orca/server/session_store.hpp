#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace orca::server {

// Opaque session tokens stored by SHA-256 hash: a memory dump never yields a
// usable cookie value (mirrors apps/server/src/sessions.ts).
struct SessionRecord {
    std::uint64_t createdAt = 0;
    std::uint64_t lastSeenAt = 0;
    std::uint64_t expiresAt = 0;
};

// Single-threaded by design (one asio io_context); the mutex exists so tests
// and future threading changes cannot regress. Persistence is explicit: the
// owner calls flush() from a debounce timer, exactly like the TS 5s save.
class SessionStore {
public:
    static constexpr const char* kCookieName = "orca_web_session";

    explicit SessionStore(std::uint64_t ttlMs, std::optional<std::string> storePath = std::nullopt);

    SessionStore(const SessionStore&) = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    struct Issued {
        std::string token;
        SessionRecord record;
    };
    Issued create();

    // Rolling expiry: every authenticated use extends the session.
    std::optional<SessionRecord> touch(const std::string& token);
    // Re-anchor the rolling window; the cookie then needs a matching Max-Age.
    std::optional<SessionRecord> refresh(const std::string& token);
    void destroy(const std::string& token);
    // Password rotation: every device must log in again.
    void clear();
    std::size_t size() const;

    void sweep(std::uint64_t nowMs);
    // Atomic tmp+rename write of token hashes; best effort, memory stays truth.
    void flush();

private:
    static std::string keyOf(const std::string& token);

    std::uint64_t ttlMs_;
    std::optional<std::string> storePath_;
    mutable std::mutex mutex_;
    std::map<std::string, SessionRecord> records_;
    bool dirty_ = false;
};

// Cookie helpers (SameSite=Strict, HttpOnly; Secure per config).
std::optional<std::string> readCookie(const std::optional<std::string>& header, const char* name);
std::string sessionCookie(const std::string& token, bool secure, std::uint64_t ttlMs);
std::string clearedSessionCookie(bool secure);

}  // namespace orca::server
