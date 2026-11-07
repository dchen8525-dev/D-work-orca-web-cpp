#pragma once

#include <string>
#include <vector>

namespace orca {

enum class AppKind { kServer, kGateway };

struct CliOptions {
    bool showVersion = false;
    bool showHelp = false;
    bool initConfig = false;
    bool localOrca = false;
    std::string configPath;
    std::string initConfigPath;
    std::string publicOrigin;
    std::vector<std::string> errors;
};

// Parses without exiting. Unknown flags and missing values land in errors so
// main() can print usage and return a stable exit code.
CliOptions parseArgs(AppKind kind, int argc, char** argv);
std::string usage(AppKind kind, const char* program);

}  // namespace orca
