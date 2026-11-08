#pragma once

#include <filesystem>
#include <string>

namespace orca {

// Deployment contract. All paths are centralised here so packaging, the two
// binaries and the docs cannot disagree (see docs/NAMING.md).

std::filesystem::path appDataDir();                 // Windows APPDATA / Linux XDG_CONFIG_HOME
std::filesystem::path defaultGatewayConfigPath();  // %APPDATA%/orca-web/gateway.json
std::filesystem::path defaultGatewayExePath();     // %APPDATA%/orca-web/orca-gateway.exe
std::filesystem::path defaultGatewayLogPath();     // %APPDATA%/orca-web/gateway.log
std::filesystem::path defaultServerConfigPath();   // /etc/orca-web/server.json (Linux)
std::filesystem::path defaultSessionStorePath();   // /var/lib/orca-web/sessions.json (Linux)
std::filesystem::path defaultStaticDir();          // /opt/orca-web/web (Linux)

// The machine hostname, for the default gatewayId (apps/gateway config.ts).
std::string hostName();

}  // namespace orca
