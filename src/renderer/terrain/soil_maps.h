#ifndef DY_SOIL_MAPS_H
#define DY_SOIL_MAPS_H

#include "renderer/terrain/chunk_layers.h"
#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <memory>

namespace dy {

class Context;
class SoilField;

// Context variable : where the passes find the soil map of a chunk while they record
struct SoilLayers {
    ChunkLayers chunks;
    bool enabled = true;    // RenderSettings::soil

    // Layer of the soil map of a chunk, -1 : none
    int32_t layer(uint32_t slot, entt::entity chunk) const {
        return enabled ? chunks.layer(slot, chunk) : -1;
    }
};

// The patches of mud, stones and sand of the ground (logic/terrain/soil.h) on the GPU : one layer of a
// 2D array per chunk, baked when the chunk gets its mesh and again when RenderSettings::soil_params
// change. Binding 2 of the view set : the terrain and the grass read it.
class SoilMaps {
public:
    SoilMaps(Context& ctx, entt::registry& reg);
    ~SoilMaps();

    // Main thread, before the transfer commands of the frame are submitted
    void update();

private:
    Context& ctx;
    entt::registry& reg;

    std::unique_ptr<SoilField> field;
    std::shared_ptr<VmaImage> image;
};

}

#endif
