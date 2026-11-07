#include <orca/log.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>

namespace orca {

namespace {

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kError: return "ERROR";
    }
    return "INFO";
}

std::string utcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm broken{};
#if defined(_WIN32)
    gmtime_s(&broken, &raw);
#else
    gmtime_r(&raw, &broken);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &broken);
    return std::string(buffer);
}

}  // namespace

void log(LogLevel level, std::string_view message) {
    std::ostream& out = level == LogLevel::kInfo ? std::cout : std::cerr;
    out << utcTimestamp() << " [" << levelName(level) << "] " << message << '\n';
    out.flush();
}

}  // namespace orca
