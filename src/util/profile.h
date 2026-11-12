#ifndef DY_PROFILE_H
#define DY_PROFILE_H

// The profiler (Tracy, lib/tracy ; docs/profiling.md) : zones on the CPU, frames, plots. The zones of the GPU
// are the viewer's (GpuProfile, a member of its Context).
//
// Nothing is recorded, and Tracy has no thread and no socket, until profile::start() : a zone then costs one
// branch. dynamical and dynamical_train start it with profile=true ; the tests and the tools never do.
// Without DYNAMICAL_PROFILE (CMake) the macros are empty and Tracy is not compiled at all.

#include <atomic>
#include <cstring>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace dy::profile {

#ifdef TRACY_ENABLE
extern std::atomic<bool> started;
inline bool active() { return started.load(std::memory_order_relaxed); }
#else
constexpr bool active() { return false; }
#endif

// Starts the profiler : from here on events are kept in memory until a Tracy profiler (or tracy-capture)
// connects to this process, on this machine. On the main thread, before any other thread records.
// False when the build has no profiler
bool start();

// Sends what is left and ends the profiler. The threads that recorded are gone by then (the executors)
void stop();

// The name of the calling thread in the profiler
void threadName(const char* name);

// A line of text on the timeline
void message(const char* text);

}

#define DY_PROFILE_CONCAT_(a, b) a##b
#define DY_PROFILE_CONCAT(a, b) DY_PROFILE_CONCAT_(a, b)

#ifdef TRACY_ENABLE

// A zone until the end of the scope, named after its function
#define DY_ZONE() ZoneNamed(DY_PROFILE_CONCAT(dy_zone_, __LINE__), dy::profile::active())
// with a name of its own (a string literal)
#define DY_ZONE_N(name) ZoneNamedN(DY_PROFILE_CONCAT(dy_zone_, __LINE__), name, dy::profile::active())
// with a name known when it runs (a const char*, copied) : the systems of a scene, the passes of a renderer
#define DY_ZONE_DYNAMIC(name) ZoneTransientN(DY_PROFILE_CONCAT(dy_zone_, __LINE__), name, dy::profile::active())

// The end of a frame of the main loop, and of a frame of another kind (a string literal : "Physics step")
#define DY_FRAME() do { if(dy::profile::active()) { FrameMark; } } while(0)
#define DY_FRAME_NAMED(name) do { if(dy::profile::active()) { FrameMarkNamed(name); } } while(0)

// A value over time (name : a string literal)
#define DY_PLOT(name, value) do { if(dy::profile::active()) { TracyPlot(name, value); } } while(0)

#else

#define DY_ZONE()
#define DY_ZONE_N(name)
#define DY_ZONE_DYNAMIC(name)
#define DY_FRAME() do {} while(0)
#define DY_FRAME_NAMED(name) do {} while(0)
#define DY_PLOT(name, value) do {} while(0)

#endif

#endif
