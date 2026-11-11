#ifndef DY_LIGHTING_STATS_H
#define DY_LIGHTING_STATS_H

#include <cstdint>

namespace dy {

// Context variable : what the ray traced lighting is doing (docs/lighting.md), for the debug menu and for
// the captures, which wait for it. Only there with ray tracing
struct LightingStats {
    uint32_t chunks_lit = 0;        // chunks with an acceleration structure
    uint32_t chunks_pending = 0;    // meshed, not yet in the rays
    uint32_t instances = 0;         // of the top level structure
    uint32_t plants_near = 0;       // plants that are their leaves in the rays
    uint32_t plants_far = 0;        // plants that are a box
    uint32_t structures_pending = 0;    // species, models and animals waiting for their structure
    uint32_t probes_pending = 0;    // probes of new bricks that were never traced
    uint32_t probes_active = 0;     // probes that are traced
    uint32_t bricks = 0;            // chunks with probes
    uint64_t structure_bytes = 0;   // memory of the bottom level structures

    // Nothing left to build : what a capture waits for
    bool settled() const { return chunks_pending == 0 && structures_pending == 0 && probes_pending == 0; }
};

}

#endif
