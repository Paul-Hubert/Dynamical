#ifndef DY_CHUNK_SYSTEMS_H
#define DY_CHUNK_SYSTEMS_H

#include "system.h"

#include "logic/terrain/chunk.h"

#include <atomic>
#include <memory>
#include <vector>

namespace dy {

class TerrainGenerator;

// Creates chunk entities around the desktop camera and the VR rig, tags far ones for destruction
class ChunkManagerSys : public System {
public:
    ChunkManagerSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "ChunkManager"; }
    void preinit() override;
    void tick(double dt) override;
};

// Computes chunk densities on the taskflow executor, nearest chunks first
class ChunkGeneratorSys : public System {
public:
    ChunkGeneratorSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "ChunkGenerator"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    struct Job {
        entt::entity entity;
        glm::ivec3 pos;
        std::vector<float> values;
        float mean = 0.f;
        bool surface = false;
        std::atomic<bool> done = false;
    };
    std::vector<std::shared_ptr<Job>> jobs;
    std::shared_ptr<TerrainGenerator> generator;
};

}

#endif
