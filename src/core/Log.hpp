#pragma once

#include <format>
#include <string_view>

namespace mc {

enum class LogLevel : int {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
};

namespace detail {
/// Writes an already-formatted message. Kept out of the header template so
/// that <cstdio> and the ANSI colour handling do not leak into every
/// translation unit.
void logWrite(LogLevel level, std::string_view message);
} // namespace detail

void setLogLevel(LogLevel level);
LogLevel logLevel();

template <typename... Args>
void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    if (level < logLevel()) {
        return;
    }
    detail::logWrite(level, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void logTrace(std::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Trace, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void logDebug(std::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void logInfo(std::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void logWarn(std::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void logError(std::format_string<Args...> fmt, Args&&... args) {
    log(LogLevel::Error, fmt, std::forward<Args>(args)...);
}

} // namespace mc
