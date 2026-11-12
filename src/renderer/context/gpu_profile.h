#ifndef DY_GPU_PROFILE_H
#define DY_GPU_PROFILE_H

#include "renderer/util/vk.h"
#include "util/profile.h"

#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#endif

#include <atomic>

#include <entt/entt.hpp>

namespace dy {

class Context;

// The GPU side of the profiler (util/profile.h, docs/profiling.md) : zones in the command buffers, timed with
// timestamp queries that Tracy reads back once the GPU is done with them and puts on the same time line as the
// CPU. One context for every queue (they are the same one, Device).
//
//     vk::CommandBuffer command = ctx.compute.getCommandBuffer();
//     DY_GPU_ZONE(ctx.gpu_profile, command, "Marching cubes");     // until the end of the scope
//
// A zone writes a timestamp where it begins and where it ends : it ends before its command buffer does, and every
// command buffer a zone was written in is submitted (Tracy waits for its queries in order). In a render pass or
// out of one.
//
// Nothing without profile=true (profile::start() before the device is made) : `enabled()` is false, a zone is
// one branch.
class GpuProfile {
public:
    GpuProfile(Context& ctx, entt::registry& reg);
    ~GpuProfile();

    bool enabled() const { return tracy != nullptr; }

    // A new frame : the first command buffer of it that calls collect() is the one that collects
    void beginFrame() { collected.store(false, std::memory_order_relaxed); }

    // Reads the timestamps the GPU has written and frees their queries for other zones, once a frame.
    // In a command buffer of the frame, outside of a render pass ; both render targets call it as they begin
    // theirs, the first one does it
    void collect(vk::CommandBuffer command);

#ifdef TRACY_ENABLE
    TracyVkCtx tracy = nullptr;
#else
    void* tracy = nullptr;
#endif

private:
    Context& ctx;
    std::atomic<bool> collected { false };
};

}

#ifdef TRACY_ENABLE

// A zone on the GPU until the end of the scope. name : a string literal
#define DY_GPU_ZONE(profile, command, name) \
    TracyVkNamedZone((profile).tracy, DY_PROFILE_CONCAT(dy_gpu_zone_, __LINE__), static_cast<VkCommandBuffer>(command), name, (profile).enabled())
// with a name known when it runs (a const char*, copied) : the passes of a renderer
#define DY_GPU_ZONE_DYNAMIC(profile, command, name) \
    TracyVkZoneTransient((profile).tracy, DY_PROFILE_CONCAT(dy_gpu_zone_, __LINE__), static_cast<VkCommandBuffer>(command), name, (profile).enabled())

#else

#define DY_GPU_ZONE(profile, command, name)
#define DY_GPU_ZONE_DYNAMIC(profile, command, name)

#endif

#endif
