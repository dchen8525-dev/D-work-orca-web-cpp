#include <orca/args.hpp>
#include <orca/platform/paths.hpp>
#include <orca/protocol/versions.hpp>
#include <orca/version.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    } else {
        std::cout << "ok:   " << label << '\n';
    }
}

orca::CliOptions parse(orca::AppKind kind, std::vector<std::string> args) {
    std::vector<std::vector<char>> storage;
    std::vector<char*> argv;
    std::vector<char> program{'o', 'r', 'c', 'a', '\0'};
    argv.push_back(program.data());
    for (auto& arg : args) {
        storage.emplace_back(arg.begin(), arg.end());
        storage.back().push_back('\0');
    }
    for (auto& buf : storage) argv.push_back(buf.data());
    return orca::parseArgs(kind, static_cast<int>(argv.size()), argv.data());
}

}  // namespace

int main() {
    using orca::AppKind;

    // Version consistency gate: the string compiled into version.hpp must be
    // exactly the ORCA_BUILD_VERSION CMake injected (tag builds override it).
    // A hardcoded literal here would go stale on every release; the expected
    // value comes from tests/CMakeLists.txt instead.
#ifdef ORCA_EXPECTED_VERSION
    const std::string version = orca::buildVersion();
    const std::string expected = ORCA_EXPECTED_VERSION;
    const std::string project = ORCA_PROJECT_VERSION;
    check(version == expected,
          "build version matches the injected version (" + expected + ")");
    check(version.rfind(project, 0) == 0,
          "build version agrees with project() version (" + project + ")");
    check(version.find('@') == std::string::npos,
          "version header has no unsubstituted @ORCA_BUILD_VERSION@ placeholder");
#else
    check(!std::string(orca::buildVersion()).empty(), "build version is non-empty");
#endif
    check(orca::versionBanner("orca-server").find("orca-server") != std::string::npos,
          "version banner names the program");

    auto serverCfg = parse(AppKind::kServer, {"--config", "/etc/orca-web/server.json"});
    check(serverCfg.errors.empty() && serverCfg.configPath == "/etc/orca-web/server.json",
          "server --config parses");

    auto serverInit = parse(AppKind::kServer,
                            {"--init-config", "/tmp/server.json", "--public-origin", "https://example.com"});
    check(serverInit.initConfig && serverInit.initConfigPath == "/tmp/server.json",
          "server --init-config parses");
    check(serverInit.publicOrigin == "https://example.com", "server --public-origin parses");

    auto missing = parse(AppKind::kServer, {"--config"});
    check(missing.errors.size() == 1, "missing flag value is reported, not crashed");

    auto unknown = parse(AppKind::kServer, {"--local-orca"});
    check(unknown.errors.size() == 1, "gateway-only flag is rejected by the server");

    auto gateway = parse(AppKind::kGateway, {"--local-orca", "--version"});
    check(gateway.localOrca && gateway.showVersion && gateway.errors.empty(),
          "gateway --local-orca / --version parse");

    auto gatewayUnknown = parse(AppKind::kGateway, {"--public-origin"});
    check(gatewayUnknown.errors.size() == 1, "server-only flag is rejected by the gateway");

    check(!orca::defaultGatewayConfigPath().empty(), "gateway config path is defined");
    check(orca::defaultGatewayConfigPath().filename() == "gateway.json",
          "gateway config filename follows the deployment contract");
    check(orca::defaultGatewayExePath().filename() == "orca-gateway.exe",
          "installed gateway exe name follows the deployment contract");
    check(!orca::defaultStaticDir().empty(), "static dir is defined");

    check(orca::protocol::kLinkProtocolVersion == 1, "link protocol version is 1");
    check(orca::protocol::kWebProtocolVersion == 1, "web protocol version is 1");
    check(orca::protocol::kE2eeFrameVersion == 2, "E2EE framing version is 2");
    check(orca::protocol::kHeaderLength == 42, "E2EE frame header is 42 bytes");
    check(orca::protocol::kSaltLabelSize == sizeof(orca::protocol::kSaltLabel) - 1 &&
              orca::protocol::kSaltLabel[orca::protocol::kSaltLabelSize - 1] == '\0' &&
              orca::protocol::kSaltLabel[orca::protocol::kSaltLabelSize - 2] != '\0',
          "salt label keeps exactly one trailing NUL");
    check(orca::protocol::kInfoLabelSize == sizeof(orca::protocol::kInfoLabel) - 1 &&
              orca::protocol::kInfoLabel[orca::protocol::kInfoLabelSize - 1] == '\0',
          "info label keeps exactly one trailing NUL");

    std::cout << (failures == 0 ? "smoke tests: PASS\n" : "smoke tests: FAIL\n");
    return failures == 0 ? 0 : 1;
}
