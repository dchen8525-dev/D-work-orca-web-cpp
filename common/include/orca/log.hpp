#pragma once

#include <string>
#include <string_view>

namespace orca {

enum class LogLevel { kInfo, kWarn, kError };

// ISO-8601 UTC timestamp, one line per event. Info -> stdout, others -> stderr.
void log(LogLevel level, std::string_view message);

// GUI-subsystem executables have no console. When a log file is set, log()
// writes there instead of stdout/stderr. Passing an empty path restores the
// console behaviour. Failure to open is silently ignored: logging must never
// take the process down.
void setLogFile(const std::string& path);

inline void logInfo(std::string_view message) { log(LogLevel::kInfo, message); }
inline void logWarn(std::string_view message) { log(LogLevel::kWarn, message); }
inline void logError(std::string_view message) { log(LogLevel::kError, message); }

}  // namespace orca
