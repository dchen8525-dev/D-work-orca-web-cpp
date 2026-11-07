#include <orca/args.hpp>
#include <orca/crypto/base64.hpp>
#include <orca/crypto/scrypt.hpp>
#include <orca/log.hpp>
#include <orca/platform/paths.hpp>
#include <orca/server/hub_core.hpp>
#include <orca/server/http_app.hpp>
#include <orca/server/server_config.hpp>
#include <orca/server/server_runner.hpp>
#include <orca/server/session_store.hpp>
#include <orca/version.hpp>

#include <nlohmann/json.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace {

std::string isoNow() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

void hubLog(const std::string& message) {
    std::cout << isoNow() << ' ' << message << '\n';
    std::cout.flush();
}

std::vector<std::uint8_t> randomBytes(std::size_t count) {
    std::vector<std::uint8_t> bytes(count);
    std::random_device device;
    for (std::size_t i = 0; i < count; i += 4) {
        const std::uint32_t pool = device();
        for (std::size_t j = 0; j < 4 && i + j < count; ++j) {
            bytes[i + j] = static_cast<std::uint8_t>((pool >> (8 * j)) & 0xff);
        }
    }
    return bytes;
}

std::string randomHex(std::size_t bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto raw = randomBytes(bytes);
    std::string out;
    out.reserve(bytes * 2);
    for (const auto byte : raw) {
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

// bootstrap/deb entry: generate server.json (never overwrites an existing
// config); the password is printed exactly once, like the TS implementation.
int initConfig(const std::string& outPath, const std::optional<std::string>& publicOrigin) {
    std::error_code ec;
    if (std::filesystem::exists(outPath, ec)) {
        orca::logError("配置已存在，拒绝覆盖: " + outPath);
        return 4;
    }
    const std::string password = randomHex(6);
    const auto gatewayKeyBytes = randomBytes(24);

    nlohmann::json cfg{{"host", "127.0.0.1"},
                       {"port", 8788},
                       {"gatewayKey", orca::crypto::base64Encode(gatewayKeyBytes)},
                       {"passwordHash", orca::crypto::hashPassword(password)},
                       {"staticDir", "apps/web/dist"},
                       {"sessionStorePath", "/var/lib/orca-web/sessions.json"}};
    if (publicOrigin) {
        cfg["publicOrigin"] = *publicOrigin;
        // C++17: no std::string::starts_with yet.
        cfg["cookieSecure"] = publicOrigin->rfind("https://", 0) == 0;
        cfg["trustProxy"] = true;
    }

    const std::filesystem::path final{outPath};
    std::filesystem::create_directories(final.parent_path(), ec);
    const std::filesystem::path temp = final.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            orca::logError("无法写入配置: " + temp.string());
            return 4;
        }
        out << cfg.dump(2) << '\n';
    }
    std::filesystem::rename(temp, final, ec);
    if (ec) {
        std::filesystem::remove(final, ec);
        std::filesystem::rename(temp, final, ec);
    }
#ifndef _WIN32
    std::filesystem::permissions(final, std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_write);
#endif
    std::cout << "配置已生成: " << outPath << '\n';
    std::cout << "登录密码（只显示这一次，请立即保存）: " << password << '\n';
    if (publicOrigin) {
        std::cout << "https:// 配置已写入 publicOrigin: " << *publicOrigin << '\n';
    }
    return 0;
}

// Password rotation: rewrite the config file with the new hash, atomically.
void persistPasswordHash(const std::filesystem::path& configPath, const std::string& encoded) {
    nlohmann::json file;
    {
        std::ifstream in(configPath);
        if (!in) throw std::runtime_error("cannot read " + configPath.string());
        in >> file;
    }
    if (!file.is_object()) throw std::runtime_error("config root is not an object");
    file["passwordHash"] = encoded;
    const std::filesystem::path temp = configPath.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + temp.string());
        out << file.dump(2) << '\n';
    }
    std::error_code ec;
    std::filesystem::rename(temp, configPath, ec);
    if (ec) {
        std::filesystem::remove(configPath, ec);
        std::filesystem::rename(temp, configPath, ec);
    }
#ifndef _WIN32
    std::filesystem::permissions(configPath, std::filesystem::perms::owner_read |
                                                 std::filesystem::perms::owner_write);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    using orca::AppKind;
    const auto opts = orca::parseArgs(AppKind::kServer, argc, argv);

    if (opts.showVersion) {
        std::cout << orca::versionBanner("orca-server") << '\n';
        return 0;
    }
    if (opts.showHelp) {
        std::cout << orca::usage(AppKind::kServer, "orca-server");
        return 0;
    }
    if (!opts.errors.empty()) {
        for (const auto& error : opts.errors) orca::logError(error);
        std::cerr << orca::usage(AppKind::kServer, "orca-server");
        return 2;
    }

    if (!opts.initConfigPath.empty()) {
        std::optional<std::string> origin;
        if (!opts.publicOrigin.empty()) origin = opts.publicOrigin;
        return initConfig(opts.initConfigPath, origin);
    }

    if (opts.configPath.empty()) {
        orca::logError("--config 或 --init-config 必须二选一");
        std::cerr << orca::usage(AppKind::kServer, "orca-server");
        return 2;
    }
    const std::filesystem::path configPath = opts.configPath;
    if (!std::filesystem::exists(configPath)) {
        orca::logError("配置文件不存在: " + configPath.string());
        return 3;
    }

    orca::server::ServerConfig config;
    try {
        config = orca::server::loadServerConfig(configPath);
    } catch (const std::exception& error) {
        orca::logError(error.what());
        return 1;
    }

    orca::server::SessionStore sessions(config.sessionTtlMs, config.sessionStorePath);
    orca::server::HubCore hub(config.gatewayKey, hubLog);
    std::optional<std::function<void(const std::string&)>> persist;
    if (!opts.configPath.empty()) {
        persist = [&configPath](const std::string& encoded) {
            persistPasswordHash(configPath, encoded);
        };
    }
    orca::server::HttpApp app(config, sessions, hub, hubLog,
                              persist ? *persist : std::function<void(const std::string&)>());

    boost::asio::io_context io;
    orca::server::ServerRunner runner(io, config, sessions, hub, app);
    if (!runner.listen()) {
        orca::logError("无法监听 " + config.host + ":" + std::to_string(config.port));
        return 1;
    }
    hubLog("orca-web server listening on http://" + config.host + ":" +
        std::to_string(config.port));
    hubLog("public origin " +
        (config.publicOrigin ? *config.publicOrigin
                             : "(unset: origin check disabled, development only)"));
    hubLog(std::string("secure cookie ") + (config.cookieSecure ? "true" : "false"));

    // SIGINT/SIGTERM: explicit closes, stop accepting, force-exit after 3s.
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&runner](std::error_code, int signal) {
        hubLog(std::string(signal == SIGINT ? "SIGINT" : "SIGTERM") + ": shutting down");
        runner.shutdown("signal");
    });

    // Session hygiene: debounced persistence and an expiry sweep. Both are
    // recurring timers that reschedule themselves; the store ignores no-op
    // flushes when nothing is dirty.
    struct SessionMaintenance {
        boost::asio::steady_timer flushTimer;
        boost::asio::steady_timer sweepTimer;
        orca::server::SessionStore& store;
        void start() {
            flushTimer.expires_after(std::chrono::seconds(5));
            flushTimer.async_wait([this](std::error_code) {
                store.flush();
                start();
            });
            sweepOnce();
        }
        void sweepOnce() {
            sweepTimer.expires_after(std::chrono::minutes(10));
            sweepTimer.async_wait([this](std::error_code) {
                store.sweep(nowMs());
                sweepOnce();
            });
        }
    };
    SessionMaintenance maintenance{boost::asio::steady_timer(io, std::chrono::seconds(5)),
                                   boost::asio::steady_timer(io, std::chrono::minutes(10)),
                                   sessions};
    maintenance.start();

    runner.run();
    return 0;
}
