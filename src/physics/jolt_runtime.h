#ifndef DY_JOLT_RUNTIME_H
#define DY_JOLT_RUNTIME_H

#include <cstdint>

namespace dy {

// Jolt's process-wide state: allocator, trace and assert hooks, the factory and type registration.
// One instance lives in each main (and the test main) and outlives every PhysicsWorld: with several worlds,
// the first world destroyed must not unregister types the others still use.
class JoltRuntime {
public:
    JoltRuntime();
    ~JoltRuntime();

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;

    static bool active();

    // Blocks allocated through Jolt's allocator and not freed yet (leak checks)
    static int64_t liveAllocations();
};

}

#endif
