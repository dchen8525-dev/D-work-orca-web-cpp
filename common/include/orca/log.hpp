#pragma once

#include <string_view>

namespace orca {

enum class LogLevel { kInfo, kWarn, kError };

// ISO-8601 UTC timestamp, one line per event. Info -> stdout, others -> stderr.
void log(LogLevel level, std::string_view message);

inline void logInfo(std::string_view message) { log(LogLevel::kInfo, message); }
inline void logWarn(std::string_view message) { log(LogLevel::kWarn, message); }
inline void logError(std::string_view message) { log(LogLevel::kError, message); }

}  // namespace orca
