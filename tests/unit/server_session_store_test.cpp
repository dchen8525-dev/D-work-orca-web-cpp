// P3: SessionStore — hashing of tokens at rest, rolling expiry, refresh and
// file persistence round-trip (the format the deb ships in /var/lib/orca-web).

#include <orca/server/session_store.hpp>

#include "test_util.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using orca::server::SessionStore;

int main() {
    // Basic lifecycle with no persistence.
    SessionStore memory(1000);
    auto issued = memory.create();
    test::check(issued.token.size() >= 32, "tokens carry enough entropy");
    test::check(memory.touch(issued.token).has_value(), "a fresh token touches");
    test::check(!memory.touch("bogus").has_value(), "unknown tokens fail");

    // destroy removes a specific token.
    auto second = memory.create();
    memory.destroy(second.token);
    test::check(!memory.touch(second.token).has_value(), "a destroyed token fails immediately");

    // refresh re-anchors createdAt. The store runs on the real clock at
    // millisecond granularity, so pause between mutations to make the strict
    // comparisons deterministic.
    SessionStore rolling(1000);
    auto session = rolling.create();
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const auto touched = rolling.touch(session.token);
    test::check(touched.has_value() && touched->expiresAt > session.record.expiresAt,
                "touch extends the rolling window");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const auto refreshed = rolling.refresh(session.token);
    test::check(refreshed.has_value() && refreshed->createdAt > session.record.createdAt,
                "refresh re-anchors createdAt");

    // destroy/clear.
    rolling.destroy(session.token);
    test::check(!rolling.touch(session.token).has_value(), "destroy kills the token");
    test::check(rolling.size() == 0, "store empty after destroy");
    rolling.create();
    rolling.clear();
    test::check(rolling.size() == 0, "clear retires every session");

    // Persistence: create in a store with a path, reload in a second store.
    const std::filesystem::path storePath =
        std::filesystem::temp_directory_path() / "orca-session-store-test.json";
    std::filesystem::remove(storePath);
    {
        SessionStore persisted(100000, storePath.string());
        persisted.create();
        persisted.create();
        persisted.flush();
    }
    {
        SessionStore reloaded(100000, storePath.string());
        test::check(reloaded.size() == 2, "sessions survive a restart through the store file");
    }
    std::error_code ec;
    std::filesystem::remove(storePath, ec);

    // A corrupt store file must not crash startup.
    const std::filesystem::path corrupt =
        std::filesystem::temp_directory_path() / "orca-session-store-corrupt.json";
    {
        std::ofstream out(corrupt);
        out << "{not json";
    }
    SessionStore resilient(1000, corrupt.string());
    test::check(resilient.size() == 0, "a corrupt store file costs one login, never startup");
    std::filesystem::remove(corrupt, ec);

    // Cookie helpers.
    const std::string cookie = orca::server::sessionCookie("tok", true, 60000);
    test::check(cookie.find("orca_web_session=tok") == 0 && cookie.find("HttpOnly") != std::string::npos &&
                    cookie.find("SameSite=Strict") != std::string::npos &&
                    cookie.find("Max-Age=60") != std::string::npos &&
                    cookie.find("Secure") != std::string::npos,
                "session cookie carries the TS attribute set");
    const auto parsed = orca::server::readCookie("a=1; " + cookie, SessionStore::kCookieName);
    test::check(parsed.has_value() && *parsed == "tok", "readCookie finds the session value");

    return test::finish("server_session_store_test");
}
