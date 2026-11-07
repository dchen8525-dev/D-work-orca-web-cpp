#include <orca/args.hpp>
#include <orca/log.hpp>
#include <orca/platform/paths.hpp>
#include <orca/version.hpp>

#include <filesystem>
#include <iostream>
#include <string>

// P0 entry point. See server/main.cpp for the same reasoning; P4 adds the
// hub link, the Orca bridge, the loopback admin page and self-update.
int main(int argc, char** argv) {
    using orca::AppKind;
    const auto opts = orca::parseArgs(AppKind::kGateway, argc, argv);

    if (opts.showVersion) {
        std::cout << orca::versionBanner("orca-gateway") << '\n';
        return 0;
    }
    if (opts.showHelp) {
        std::cout << orca::usage(AppKind::kGateway, "orca-gateway");
        return 0;
    }
    if (!opts.errors.empty()) {
        for (const auto& error : opts.errors) orca::logError(error);
        std::cerr << orca::usage(AppKind::kGateway, "orca-gateway");
        return 2;
    }

    const std::filesystem::path config =
        opts.configPath.empty()
            ? orca::defaultGatewayConfigPath()
            : std::filesystem::path(opts.configPath);
    if (!opts.configPath.empty() && !std::filesystem::exists(config)) {
        orca::logError("配置文件不存在: " + config.string());
        return 3;
    }

    orca::logInfo(orca::versionBanner("orca-gateway") + " (P0 骨架：服务尚未实现)");
    orca::logInfo("配置文件: " + config.string());
    if (opts.localOrca) {
        orca::logInfo("--local-orca: 将读取本机桌面 Orca 的配对材料");
    }
    return 0;
}
