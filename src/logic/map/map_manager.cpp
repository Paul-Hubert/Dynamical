#include "map_manager.h"

#include "util/log.h"

#include <cstdlib>

#include <queue>

#include "logic/components/position.h"
#include "logic/components/input.h"
#include "logic/components/camera.h"
#include "logic/components/object.h"
#include "logic/factories/factory_list.h"
#include "logic/settings/settings.h"
#include <extra/optick/optick.h>

using namespace dy;

MapManager::MapManager(entt::registry& reg) : reg(reg), map(), generator(reg, *this) {

}

Chunk* MapManager::getChunk(glm::ivec2 chunk_pos) const {
    auto it = map.find(chunk_pos);
    if (it != map.end()) {
        return it->second.get();
    }
    return nullptr;
}

Chunk* MapManager::getTileChunk(glm::vec2 pos) const {
    return getChunk(getChunkPos(pos));
}

Chunk* MapManager::generateChunk(glm::ivec2 chunk_pos) {
    if(getChunk(chunk_pos) != nullptr) {
        return getChunk(chunk_pos);
    }
    map[chunk_pos] = std::make_unique<Chunk>();
    Chunk* chunk = map[chunk_pos].get();


    generator.generateChunk(*chunk, chunk_pos);

    return chunk;
}

void MapManager::requestChunkAsync(glm::ivec2 pos, int lod_level) {
    // Skip if already pending
    if (pending_chunks.count(pos)) return;
    // Skip if already in map at equal or better LOD
    Chunk* existing = getChunk(pos);
    if (existing != nullptr && existing->lod_level <= lod_level) return;

    // Capture config by value for thread safety
    auto configurations = reg.ctx<Settings>().map_configurations;

    auto future = std::async(std::launch::async, [pos, lod_level, configurations = std::move(configurations)]() {
        auto chunk = std::make_unique<Chunk>();
        MapGenerator::generateTerrain(*chunk, pos, lod_level, configurations);
        return chunk;
    });

    pending_chunks.insert_or_assign(pos, PendingChunk{std::move(future), lod_level});
}

void MapManager::pollReady() {
    OPTICK_EVENT();

    std::vector<glm::ivec2> ready;
    for (auto& [pos, pending] : pending_chunks) {
        if (pending.future.valid() && pending.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            ready.push_back(pos);
        }
    }

    for (auto& pos : ready) {
        auto& pending = pending_chunks[pos];
        auto new_chunk = pending.future.get();
        int lod = pending.lod_level;

        // Check if this is a LOD upgrade of an existing chunk
        Chunk* existing = getChunk(pos);
        if (existing != nullptr) {
            // Copy terrain data from new chunk, but preserve existing entity set
            for (int i = 0; i < Chunk::size; i++) {
                for (int j = 0; j < Chunk::size; j++) {
                    auto& new_tile = new_chunk->get(glm::ivec2(i, j));
                    auto& old_tile = existing->get(glm::ivec2(i, j));
                    old_tile.level = new_tile.level;
                    old_tile.terrain = new_tile.terrain;
                    // Preserve old_tile.object and old_tile.building
                }
            }
            existing->lod_level = lod;
            existing->setUpdated(true);
        } else {
            // New chunk — insert into map
            map[pos] = std::move(new_chunk);
            map[pos]->setUpdated(true);
        }

        // Queue for finalization if LOD 0
        if (lod == 0) {
            finalization_queue.push_back(pos);
        }

        pending_chunks.erase(pos);
    }
}

void MapManager::processFinalizationQueue(int budget) {
    OPTICK_EVENT();

    int processed = 0;
    while (!finalization_queue.empty() && processed < budget) {
        glm::ivec2 pos = finalization_queue.back();
        finalization_queue.pop_back();

        Chunk* chunk = getChunk(pos);
        if (chunk == nullptr) continue; // chunk was removed somehow

        generator.finalizeChunk(*chunk, pos);
        chunk->setUpdated(true); // trigger GPU re-upload with refined tile types

        processed++;
    }
}

bool MapManager::isChunkPending(glm::ivec2 pos) const {
    return pending_chunks.count(pos) > 0;
}

void MapManager::updateTile(glm::vec2 pos) {
    Tile* tile = getTile(pos);
    if(!tile) return;
    generator.setTileType(*tile, pos);
    if(tile->object != entt::null && reg.get<Object>(tile->object).type == Object::plant) {
        dy::destroyObject(reg, tile->object);
        tile->object = entt::null;
    }
    getTileChunk(pos)->setUpdated();
}

Tile* MapManager::getTile(glm::vec2 pos) const {

    Chunk* chunk = getChunk(getChunkPos(pos));
    if(chunk == nullptr) return nullptr;
    return &chunk->get(getTilePos(pos));
}

void MapManager::insert(entt::entity entity, glm::vec2 position) {
    Chunk* chunk = getChunk(getChunkPos(position));
    if(chunk == nullptr) {
        dy::log(dy::Level::critical) << "Chunk not generated\n";
        return;
    }
    float height = getTile(position)->level;
    reg.emplace_or_replace<Position>(entity, position, height);
    chunk->addObject(entity);
}

void MapManager::move(entt::entity entity, glm::vec2 position) {
    auto& pos = reg.get<Position>(entity);
    float height = pos.getHeight();
    if(getChunkPos(position) != getChunkPos(pos)) {
        Chunk* old_chunk = getChunk(getChunkPos(pos));
        if(old_chunk == nullptr) {
            dy::log(dy::Level::critical) << "Chunk not generated\n";
            return;
        }
        old_chunk->removeObject(entity);
        Chunk* new_chunk = getChunk(getChunkPos(position));
        if(new_chunk == nullptr) {
            dy::log(dy::Level::critical) << "Chunk not generated\n";
            return;
        }
        new_chunk->addObject(entity);
    }
    height = getTile(position)->level;
    reg.replace<Position>(entity, position, height);
}

void MapManager::remove(entt::entity entity) {
    auto& pos = reg.get<Position>(entity);
    Chunk* old_chunk = getChunk(getChunkPos(pos));
    if(old_chunk == nullptr) {
        dy::log(dy::Level::critical) << "Chunk not generated\n";
        return;
    }
    reg.remove<Position>(entity);
    old_chunk->removeObject(entity);

    Tile* tile = getTile(pos);
    if(tile->object == entity) {
        tile->object = entt::null;
    }
}

glm::vec2 MapManager::getMousePosition() const {

    auto& input = reg.ctx<Input>();
    auto& cam = reg.ctx<Camera>();

    return cam.fromScreenSpace(input.mousePos);
}

struct Distance {
    Distance(glm::vec2 pos, float distance) : pos(pos), distance(distance) {}
    Distance() {
        distance = std::numeric_limits<float>::max();
    }
    glm::vec2 pos;
    float distance;

};

inline bool operator> (const Distance a, const Distance b) {
    return a.distance > b.distance;
}

const std::vector<glm::vec2> MapManager::pathfind(glm::vec2 start, std::function<bool(glm::vec2)> predicate, int iteration_limit) const {

    OPTICK_EVENT();

    start = floor(start);

    std::priority_queue<Distance, std::vector<Distance>, std::greater<Distance>> queue;

    std::unordered_map<glm::vec2, Distance> predecessor;

    queue.emplace(start, 0);
    predecessor[start] = Distance(start, 0);

    int iteration = 0;

    while(!queue.empty()) {

        Distance min = queue.top();
        queue.pop();

        if(predicate(min.pos)) {
            std::vector<glm::vec2> path;
            glm::vec2 p = min.pos;
            while(p != start) {
                path.push_back(p);
                p = predecessor[p].pos;
            }
            return path;
        }


        iteration++;
        if(iteration > iteration_limit) break;

        for(int i = -1; i<=1; i++) {
            for(int j = -1; j<=1; j++) {
                if(!(i == 0 && j == 0)) {
                    glm::vec2 adj = min.pos + glm::vec2(i, j);

                    Chunk* chunk = getChunk(getChunkPos(adj));
                    Tile* tile = getTile(adj);
                    if(tile == nullptr || Tile::terrain_speed.at(tile->terrain) == 0.0) {
                        continue;
                    }

                    float weight = glm::distance(min.pos, adj) / Tile::terrain_speed.at(tile->terrain);


                    if(predecessor.count(adj) == 0 || predecessor[adj].distance > min.distance + weight) {
                        predecessor[adj] = Distance(min.pos, min.distance + weight);
                        queue.emplace(adj, min.distance + weight);
                    }

                }
            }
        }

    }

    std::vector<glm::vec2> path;
    return path;
}



MapManager::~MapManager() {
    // Wait for any pending async chunks before destruction
    for (auto& [pos, pending] : pending_chunks) {
        if (pending.future.valid()) {
            pending.future.wait();
        }
    }
}
