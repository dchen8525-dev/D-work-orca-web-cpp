#include <orca/args.hpp>
#include <orca/log.hpp>
#include <orca/platform/paths.hpp>
#include <orca/version.hpp>

#include <filesystem>
#include <iostream>
#include <string>

// P0 entry point. It deliberately does NOT pretend to serve traffic: the only
// behaviours implemented so far are the CLI contract and config-path checks,
// both of which are covered by tests/unit/smoke_test.cpp. P3 adds HTTP/WS.
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

    const std::filesystem::path config =
        opts.configPath.empty()
            ? orca::defaultServerConfigPath()
            : std::filesystem::path(opts.configPath);
    if (!opts.configPath.empty() && !std::filesystem::exists(config)) {
        orca::logError("配置文件不存在: " + config.string());
        return 3;
    }

    orca::logInfo(orca::versionBanner("orca-server") + " (P0 骨架：服务尚未实现)");
    orca::logInfo("配置文件: " + config.string());
    if (!opts.initConfigPath.empty()) {
        orca::logInfo("待生成配置: " + opts.initConfigPath);
    }
    if (!opts.publicOrigin.empty()) {
        orca::logInfo("public origin: " + opts.publicOrigin);
    }
    return 0;
}
