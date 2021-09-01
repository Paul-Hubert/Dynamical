#ifndef PROFILE_H
#define PROFILE_H

namespace dy {

#ifndef NDEBUG
constexpr bool profiling = true;
#else
constexpr bool profiling = false;
#endif

constexpr bool marchingcubesprofiling = profiling && true;

constexpr bool systemprofiling = profiling && true;

}

#endif
