#ifndef DY_CHUNK_LAYERS_H
#define DY_CHUNK_LAYERS_H

#include "renderer/context/num_frames.h"
#include "renderer/terrain/terrain_sys.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace dy {

// Which layer of a 2D array image holds the map of each chunk (the height of its grass, its soil) : a
// layer is given to a chunk when it gets its mesh, before it is drawn, and taken back NUM_FRAMES + 1
// frames after the chunk is gone or its map was baked again, when no frame in flight reads it any more.
// Chunks are known by their slot of TerrainSys. Main thread, but layer() which the passes call while
// they record
class ChunkLayers {
public:
    explicit ChunkLayers(uint32_t layers = 0) {
        for(uint32_t layer = layers; layer > 0; layer--) {
            free.push_back(layer - 1);
        }
    }

    // Once per frame. bake(layer, chunk entity) fills a layer : called for every chunk without a map,
    // and for at most max_rebakes of those whose map was baked with another key (they keep the map they
    // have until then)
    template<typename Bake>
    void update(entt::registry& reg, size_t slots, uint64_t key, int max_rebakes, Bake&& bake) {

        frame++;

        for(auto it = retired.begin(); it != retired.end();) {
            if(frame - it->frame > NUM_FRAMES + 1) {
                free.push_back(it->layer);
                it = retired.erase(it);
            } else {
                ++it;
            }
        }

        if(maps.size() < slots) {
            maps.resize(slots);
        }

        for(auto [entity, gpu] : reg.view<ChunkGPU, ChunkReady>().each()) {
            if(gpu.slot >= maps.size()) continue;
            Map& map = maps[gpu.slot];
            map.seen = frame;

            // The slot went to another chunk
            if(map.chunk != entity) {
                retire(map);
                map.chunk = entity;
            }

            if(map.layer >= 0 && (map.key == key || max_rebakes-- <= 0)) continue;
            if(free.empty()) continue;
            uint32_t layer = free.back();
            free.pop_back();
            bake(layer, entity);

            // Frames in flight still read the map that this one replaces
            retire(map);
            map.layer = (int32_t) layer;
            map.key = key;
        }

        // Slots whose chunk is gone
        for(auto& map : maps) {
            if(map.seen == frame || map.chunk == entt::null) continue;
            retire(map);
            map = Map {};
        }

    }

    // Layer of the map of a chunk, -1 when it has none (yet, or the layers ran out)
    int32_t layer(uint32_t slot, entt::entity chunk) const {
        if(slot >= maps.size() || maps[slot].chunk != chunk) return -1;
        return maps[slot].layer;
    }

private:
    struct Map {
        entt::entity chunk = entt::null;    // that holds the slot
        uint64_t key = 0;                   // of the parameters it was baked with
        int32_t layer = -1;
        uint64_t seen = 0;                  // last frame the chunk was there
    };

    void retire(Map& map) {
        if(map.layer >= 0) retired.push_back({(uint32_t) map.layer, frame});
        map.layer = -1;
    }

    std::vector<Map> maps;                  // per chunk slot (TerrainBuffers)
    std::vector<uint32_t> free;

    struct Retired {
        uint32_t layer;
        uint64_t frame;
    };
    std::vector<Retired> retired;
    uint64_t frame = 0;
};

}

#endif
