#pragma once

// Profiling macros. These compile to nothing unless the release-tracy preset is
// used, so they can be placed freely in hot code without a debate about cost.
//
// Every phase in docs/DESIGN.md ends with a Tracy capture. A phase is not
// complete until its performance is measured rather than assumed.

#ifdef MC_TRACY_ENABLED

#include <tracy/Tracy.hpp>

#define MC_PROFILE_FRAME()          FrameMark
#define MC_PROFILE_SCOPE()          ZoneScoped
#define MC_PROFILE_SCOPE_N(name)    ZoneScopedN(name)
#define MC_PROFILE_THREAD(name)     tracy::SetThreadName(name)
#define MC_PROFILE_PLOT(name, value) TracyPlot(name, value)

#else

#define MC_PROFILE_FRAME()           ((void)0)
#define MC_PROFILE_SCOPE()           ((void)0)
#define MC_PROFILE_SCOPE_N(name)     ((void)0)
#define MC_PROFILE_THREAD(name)      ((void)(name))
#define MC_PROFILE_PLOT(name, value) ((void)(name), (void)(value))

#endif
