#include <orca/version.hpp>

namespace orca {

const char* buildVersion() noexcept {
    return kBuildVersion;
}

std::string versionBanner(const char* program) {
    return std::string(program) + " " + kBuildVersion;
}

}  // namespace orca
