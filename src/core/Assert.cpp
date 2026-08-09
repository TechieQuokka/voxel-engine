#include "core/Assert.hpp"

#include "core/Log.hpp"

#include <cstdlib>

namespace mc::detail {

void assertFail(std::string_view expr,
                std::string_view file,
                int line,
                std::string_view function,
                std::string_view message) {
    if (message.empty()) {
        logError("Assertion failed: {}\n  at {}:{} in {}", expr, file, line, function);
    } else {
        logError("Assertion failed: {}\n  {}\n  at {}:{} in {}",
                 expr, message, file, line, function);
    }
    std::abort();
}

} // namespace mc::detail
