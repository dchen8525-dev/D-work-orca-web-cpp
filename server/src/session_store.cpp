#include <orca/server/session_store.hpp>

#include <orca/crypto/sha256.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

namespace orca::server {

namespace {

using nlohmann::json;

std::string randomToken() {
    // 32 random bytes, base64url — same shape as randomBytes(32).toString('base64url').
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string token;
    token.reserve(43);
    std::random_device device;
    for (int round = 0; round < 2; ++round) {
        std::uint32_t pool = device();
        for (int bit = 0; bit < 16; ++bit) {
            token.push_back(kAlphabet[pool & 63]);
            pool >>= 6;
        }
    }
    return token;
}

std::string sha256Hex(const std::string& text) {
    const auto digest = orca::crypto::sha256(text);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xf]);
    }
    return out;
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

json recordToJson(const SessionRecord& record) {
    return json{{"createdAt", record.createdAt},
                {"lastSeenAt", record.lastSeenAt},
                {"expiresAt", record.expiresAt}};
}

std::optional<SessionRecord> recordFromJson(const json& value) {
    if (!value.is_object() || !value.contains("expiresAt") || !value.at("expiresAt").is_number()) {
        return std::nullopt;
    }
    SessionRecord record;
    record.expiresAt = value.at("expiresAt").get<std::uint64_t>();
    record.createdAt = value.contains("createdAt") && value.at("createdAt").is_number()
                           ? value.at("createdAt").get<std::uint64_t>()
                           : 0;
    record.lastSeenAt = value.contains("lastSeenAt") && value.at("lastSeenAt").is_number()
                            ? value.at("lastSeenAt").get<std::uint64_t>()
                            : 0;
    return record;
}

}  // namespace

SessionStore::SessionStore(std::uint64_t ttlMs, std::optional<std::string> storePath)
    : ttlMs_(ttlMs), storePath_(std::move(storePath)) {
    // Logins survive a restart; a corrupt file costs one login, never startup.
    if (!storePath_) return;
    std::ifstream in(*storePath_);
    if (!in) return;
    try {
        json stored;
        in >> stored;
        if (!stored.is_object()) return;
        const auto now = nowMs();
        std::lock_guard lock(mutex_);
        for (auto it = stored.begin(); it != stored.end(); ++it) {
            if (auto record = recordFromJson(it.value());
                record && record->expiresAt > now) {
                records_.emplace(it.key(), *record);
            }
        }
    } catch (const json::exception&) {
        // keep whatever parsed; next flush rewrites the file wholesale
    }
}

SessionStore::Issued SessionStore::create() {
    Issued issued;
    issued.token = randomToken();
    const auto now = nowMs();
    issued.record = SessionRecord{now, now, now + ttlMs_};
    std::lock_guard lock(mutex_);
    records_.emplace(keyOf(issued.token), issued.record);
    dirty_ = true;
    return issued;
}

std::optional<SessionRecord> SessionStore::touch(const std::string& token) {
    const auto now = nowMs();
    std::lock_guard lock(mutex_);
    const auto it = records_.find(keyOf(token));
    if (it == records_.end()) return std::nullopt;
    if (now >= it->second.expiresAt) {
        records_.erase(it);
        dirty_ = true;
        return std::nullopt;
    }
    it->second.lastSeenAt = now;
    it->second.expiresAt = now + ttlMs_;
    dirty_ = true;
    return it->second;
}

std::optional<SessionRecord> SessionStore::refresh(const std::string& token) {
    const auto now = nowMs();
    std::lock_guard lock(mutex_);
    const auto it = records_.find(keyOf(token));
    if (it == records_.end()) return std::nullopt;
    if (now >= it->second.expiresAt) {
        records_.erase(it);
        dirty_ = true;
        return std::nullopt;
    }
    it->second.createdAt = now;
    it->second.lastSeenAt = now;
    it->second.expiresAt = now + ttlMs_;
    dirty_ = true;
    return it->second;
}

void SessionStore::destroy(const std::string& token) {
    std::lock_guard lock(mutex_);
    if (records_.erase(keyOf(token)) > 0) dirty_ = true;
}

void SessionStore::clear() {
    std::lock_guard lock(mutex_);
    if (!records_.empty()) dirty_ = true;
    records_.clear();
}

std::size_t SessionStore::size() const {
    std::lock_guard lock(mutex_);
    return records_.size();
}

void SessionStore::sweep(std::uint64_t nowMs_) {
    std::lock_guard lock(mutex_);
    const auto before = records_.size();
    for (auto it = records_.begin(); it != records_.end();) {
        if (nowMs_ >= it->second.expiresAt) {
            it = records_.erase(it);
        } else {
            ++it;
        }
    }
    if (records_.size() != before) dirty_ = true;
}

void SessionStore::flush() {
    std::string payload;
    {
        std::lock_guard lock(mutex_);
        if (!storePath_ || !dirty_) return;
        json stored = json::object();
        for (const auto& [key, record] : records_) stored[key] = recordToJson(record);
        payload = stored.dump();
        dirty_ = false;
    }
    const std::filesystem::path final = *storePath_;
    std::error_code ec;
    std::filesystem::create_directories(final.parent_path(), ec);
    const std::filesystem::path temp = final.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;  // best effort; memory stays authoritative
        out << payload;
    }
    std::filesystem::rename(temp, final, ec);
    // Windows rename fails onto an existing target; fall back to remove+rename.
    if (ec) {
        std::filesystem::remove(final, ec);
        std::filesystem::rename(temp, final, ec);
    }
}

std::string SessionStore::keyOf(const std::string& token) {
    return sha256Hex(token);
}

std::optional<std::string> readCookie(const std::optional<std::string>& header, const char* name) {
    if (!header) return std::nullopt;
    std::size_t begin = 0;
    while (begin <= header->size()) {
        const std::size_t end = header->find(';', begin);
        const std::string_view part =
            std::string_view(*header).substr(begin, end == std::string::npos ? std::string::npos
                                                                             : end - begin);
        const auto eq = part.find('=');
        if (eq != std::string_view::npos) {
            const auto key = part.substr(0, eq);
            // trim
            const auto keyFirst = key.find_first_not_of(" \t");
            const auto keyLast = key.find_last_not_of(" \t");
            if (keyFirst != std::string_view::npos &&
                key.substr(keyFirst, keyLast - keyFirst + 1) == name) {
                return std::string(part.substr(eq + 1));
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return std::nullopt;
}

std::string sessionCookie(const std::string& token, bool secure, std::uint64_t ttlMs) {
    std::string out = std::string(SessionStore::kCookieName) + "=" + token +
                      "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
                      std::to_string(ttlMs / 1000);
    if (secure) out += "; Secure";
    return out;
}

std::string clearedSessionCookie(bool secure) {
    std::string out =
        std::string(SessionStore::kCookieName) + "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
    if (secure) out += "; Secure";
    return out;
}

}  // namespace orca::server
