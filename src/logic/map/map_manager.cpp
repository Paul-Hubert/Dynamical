#include "map_manager.h"

#include "util/log.h"

#include <cstdlib>

#include <queue>

#include "logic/components/position.h"
#include "logic/components/input.h"
#include "logic/components/camera.h"
#include <extra/optick/optick.h>

using namespace dy;

MapManager::MapManager(entt::registry& reg) : reg(reg), map(), generator(reg, *this) {
    
}

Chunk* MapManager::getChunk(glm::ivec2 pos) const {
    try {
        return map.at(pos).get();
    } catch(std::out_of_range&) {
        return nullptr;
    }
}

Chunk* MapManager::generateChunk(glm::ivec2 pos) {
    if(getChunk(pos) != nullptr) {
        return getChunk(pos);
    }
    map[pos] = std::make_unique<Chunk>();
    Chunk* chunk = map[pos].get();
    
    generator.generateChunk(*chunk, pos);
    
    return chunk;
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
    reg.emplace<Position>(entity, position);
    chunk->addObject(entity);
}

void MapManager::move(entt::entity entity, glm::vec2 position) {
    auto& pos = reg.get<Position>(entity);
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
    pos = position;
    
}

void MapManager::remove(entt::entity entity) {
    auto& pos = reg.get<Position>(entity);
    Chunk* old_chunk = getChunk(getChunkPos(pos));
    if(old_chunk == nullptr) {
        dy::log(dy::Level::critical) << "Chunk not generated\n";
        return;
    }
    old_chunk->removeObject(entity);
}

glm::vec2 MapManager::getMousePosition() const {
    
    auto& input = reg.ctx<Input>();
    auto& cam = reg.ctx<Camera>();
    
    return cam.fromScreenSpace(input.mousePos, input.screenSize);
    
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

std::vector<glm::vec2> MapManager::pathfind(glm::vec2 start, std::function<bool(glm::vec2)> predicate, int iteration_limit) const {
    
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
    
}

