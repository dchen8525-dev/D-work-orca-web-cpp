#include <orca/args.hpp>
#include <orca/log.hpp>
#include <orca/platform/paths.hpp>
#include <orca/version.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#ifndef ORCA_NO_QT
#include "ui/main_window.hpp"

#ifdef ORCA_HAVE_HUB_CLIENT
#include <orca/gateway/net/hub_client.hpp>
#include <orca/gateway/net/orca_bridge.hpp>
#include "ui/runtime_status_provider.hpp"
#endif

#include <QApplication>
#include <QTimer>
#include <QtGlobal>
#include <QtPlugin>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// A static Qt build cannot discover the Windows platform plugin from a DLL.
// The CMake target links Qt6::QWindowsIntegrationPlugin for exactly this case.
#if defined(ORCA_QT_STATIC) && defined(_WIN32)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

namespace {

#if defined(_WIN32) && !defined(ORCA_NO_QT)
// Console subsystem keeps --version/--help pipeable; hide the console once the
// Qt window takes over. The startup VBS already hides it from the first frame.
void hideConsoleWindow() {
    if (HWND console = ::GetConsoleWindow()) {
        ::ShowWindow(console, SW_HIDE);
    }
}
#endif

std::filesystem::path resolveConfig(const orca::CliOptions& opts) {
    return opts.configPath.empty()
        ? orca::defaultGatewayConfigPath()
        : std::filesystem::path(opts.configPath);
}

}  // namespace

int main(int argc, char** argv) {
    using orca::AppKind;
    const auto opts = orca::parseArgs(AppKind::kGateway, argc, argv);

    // CLI contract must work without ever constructing QApplication (and must
    // stay pipeable to stdout for the version-consistency gate).
    if (opts.showVersion || opts.showHelp || !opts.errors.empty()) {
        if (opts.showVersion) {
            std::cout << orca::versionBanner("orca-gateway") << '\n';
            return 0;
        }
        if (opts.showHelp) {
            std::cout << orca::usage(AppKind::kGateway, "orca-gateway");
            return 0;
        }
        for (const auto& error : opts.errors) orca::logError(error);
        std::cerr << orca::usage(AppKind::kGateway, "orca-gateway");
        return 2;
    }

    const std::filesystem::path config = resolveConfig(opts);
    if (!opts.configPath.empty() && !std::filesystem::exists(config)) {
        orca::logError("配置文件不存在: " + config.string());
        return 3;
    }
    if (opts.localOrca) {
        // P4: read the desktop Orca's local pairing material.
    }

#ifdef ORCA_NO_QT
    // Static Qt is not installed yet: stay an honest CLI skeleton rather than
    // pretending to be the delivered client. CI sets ORCA_REQUIRE_QT=ON.
    orca::logInfo(orca::versionBanner("orca-gateway") + " (CLI 骨架：Qt 尚未安装)");
    orca::logInfo("配置文件: " + config.string());
    return 0;
#else
    {
        std::error_code ec;
        std::filesystem::create_directories(orca::defaultGatewayLogPath().parent_path(), ec);
    }
    orca::setLogFile(orca::defaultGatewayLogPath().string());
    orca::logInfo(orca::versionBanner("orca-gateway") + " starting Qt client");
    orca::logInfo("config: " + config.string());

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("orca-gateway"));
    QApplication::setOrganizationName(QStringLiteral("orca-web"));

#ifdef ORCA_HAVE_HUB_CLIENT
    // P4: dial the hub in a worker thread once the config has the address and
    // key; the window's status cards read the live snapshot every 3 s.
    std::shared_ptr<orca::gateway::HubClient> hubClient;
    {
        orca::ui::ConfigStore store(config);
        const auto file = store.read();
        if (!file.serverUrl.empty() && !file.gatewayKey.empty()) {
            hubClient = std::make_shared<orca::gateway::HubClient>(
                file.serverUrl, file.gatewayKey, file.gatewayId,
                std::make_unique<orca::gateway::NullOrcaBridge>(),
                [](const std::string& message) { orca::logInfo("hub: " + message); });
            hubClient->start();
            orca::logInfo("hub link started: " + file.serverUrl);
        }
    }
#endif

    std::unique_ptr<orca::ui::StatusProvider> provider;
#ifdef ORCA_HAVE_HUB_CLIENT
    if (hubClient) {
        provider = std::make_unique<orca::ui::RuntimeStatusProvider>(config, hubClient);
    }
#endif
    orca::ui::MainWindow window(config, std::move(provider));
    window.show();
#if defined(_WIN32) && !defined(ORCA_NO_QT)
    hideConsoleWindow();
#endif

    // Headless smoke hook: construct the full window, run one refresh, then
    // quit the event loop. Used with QT_QPA_PLATFORM=offscreen in CI/tests.
    if (qEnvironmentVariableIsSet("ORCA_UI_SMOKE")) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    return app.exec();
#endif
}
