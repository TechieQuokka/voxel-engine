#pragma once

#include <string_view>

namespace mc::detail {

[[noreturn]] void assertFail(std::string_view expr,
                             std::string_view file,
                             int line,
                             std::string_view function,
                             std::string_view message);

} // namespace mc::detail

/// Always-active check. Use where the cost of the test is negligible next to
/// the cost of the failure -- GL object creation, buffer mapping, file loads.
#define MC_VERIFY(cond)                                                        \
    do {                                                                       \
        if (!(cond)) [[unlikely]] {                                            \
            ::mc::detail::assertFail(#cond, __FILE__, __LINE__,                \
                                     static_cast<const char*>(__func__), {});  \
        }                                                                      \
    } while (false)

#define MC_VERIFY_MSG(cond, msg)                                               \
    do {                                                                       \
        if (!(cond)) [[unlikely]] {                                            \
            ::mc::detail::assertFail(#cond, __FILE__, __LINE__,                \
                                     static_cast<const char*>(__func__),       \
                                     (msg));                                   \
        }                                                                      \
    } while (false)

#ifdef NDEBUG
/// Debug-only check. Compiles away entirely in release, so it is free to use
/// inside hot loops such as the mesher and palette accessors.
#define MC_ASSERT(cond)          ((void)0)
#define MC_ASSERT_MSG(cond, msg) ((void)0)
#else
#define MC_ASSERT(cond)          MC_VERIFY(cond)
#define MC_ASSERT_MSG(cond, msg) MC_VERIFY_MSG(cond, msg)
#endif
