#include "core/Log.hpp"

#include <atomic>
#include <cstdio>
#include <unistd.h>

namespace mc {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Info};

struct LevelStyle {
    std::string_view tag;
    std::string_view colour;
};

constexpr LevelStyle styleFor(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return {"TRACE", "\033[90m"};
    case LogLevel::Debug: return {"DEBUG", "\033[36m"};
    case LogLevel::Info:  return {"INFO ", "\033[32m"};
    case LogLevel::Warn:  return {"WARN ", "\033[33m"};
    case LogLevel::Error: return {"ERROR", "\033[31m"};
    }
    return {"?????", ""};
}

/// Colour is only emitted when stderr is a terminal, so redirected output
/// stays clean.
bool colourEnabled() {
    static const bool enabled = ::isatty(STDERR_FILENO) != 0;
    return enabled;
}

} // namespace

void setLogLevel(LogLevel level) {
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel logLevel() {
    return g_level.load(std::memory_order_relaxed);
}

namespace detail {

void logWrite(LogLevel level, std::string_view message) {
    const LevelStyle style = styleFor(level);

    if (colourEnabled()) {
        std::fprintf(stderr, "%.*s[%.*s]\033[0m %.*s\n",
                     static_cast<int>(style.colour.size()), style.colour.data(),
                     static_cast<int>(style.tag.size()), style.tag.data(),
                     static_cast<int>(message.size()), message.data());
    } else {
        std::fprintf(stderr, "[%.*s] %.*s\n",
                     static_cast<int>(style.tag.size()), style.tag.data(),
                     static_cast<int>(message.size()), message.data());
    }
}

} // namespace detail
} // namespace mc
