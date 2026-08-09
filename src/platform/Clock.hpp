#pragma once

#include "core/Types.hpp"

#include <chrono>

namespace mc {

/// Monotonic wall clock, in seconds since construction.
///
/// steady_clock rather than system_clock: frame timing must not jump when the
/// system time is adjusted.
class Clock {
public:
    Clock() : m_start(std::chrono::steady_clock::now()) {}

    f64 elapsed() const {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<f64>(now - m_start).count();
    }

    void reset() { m_start = std::chrono::steady_clock::now(); }

private:
    std::chrono::steady_clock::time_point m_start;
};

} // namespace mc
