#ifndef DY_GROUND_FIELD_H
#define DY_GROUND_FIELD_H

#include <glm/glm.hpp>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "logic/systems/system.h"
#include "logic/terrain/chunk.h"

namespace dy {

class MarchingCubes;
class TerrainGenerator;

// The ground as a density function (docs/physical-agents/ik-gait.md): what the kinematic walk plants its feet on,
// with no collider involved. World context variable std::shared_ptr<GroundField>
class GroundField {
public:
    struct Hit {
        glm::vec3 point;
        glm::vec3 normal;
    };

    virtual ~GroundField() = default;

    // > 0 inside the ground, as the marching cubes densities
    virtual float density(glm::vec3 p) const = 0;
    // The first surface straight below `from`, no further than `distance`
    virtual std::optional<Hit> below(glm::vec3 from, float distance) const = 0;

    // Height of the surface under (x, z), looking down from `from_y`; `fallback` without one
    float height(float x, float z, float from_y, float fallback) const {
        auto hit = below(glm::vec3(x, from_y, z), 64.f);
        return hit ? hit->point.y : fallback;
    }

    uint64_t version = 0;       // counts changes of the ground: planted feet are fitted again
};

// A plane through `origin`, tilted by `pitch` about +x then `roll` about +z (degrees). Positive pitch rises toward
// -z (uphill for a walker heading 0), positive roll rises toward +x (its right)
class PlaneField : public GroundField {
public:
    PlaneField(float pitch = 0.f, float roll = 0.f, glm::vec3 origin = glm::vec3(0.f)) : origin(origin) { setTilt(pitch, roll); }

    void setTilt(float pitch_degrees, float roll_degrees);
    float pitch() const { return pitch_degrees; }
    float roll() const { return roll_degrees; }
    glm::vec3 normal() const { return n; }

    float density(glm::vec3 p) const override { return glm::dot(n, origin - p); }
    std::optional<Hit> below(glm::vec3 from, float distance) const override;

    glm::vec3 origin;

private:
    float pitch_degrees = 0.f, roll_degrees = 0.f;
    glm::vec3 n = glm::vec3(0.f, 1.f, 0.f);
};

// The marching cubes terrain: the TerrainGenerator's densities on the cube grid, and as surface the triangles the
// renderer and the colliders mesh from those same samples, so a foot sits on the drawn ground. Blocks of
// block_cubes^3 cubes are meshed on first use and kept
class TerrainField : public GroundField {
public:
    static constexpr int block_cubes = 4;

    TerrainField(std::shared_ptr<TerrainGenerator> generator, std::shared_ptr<MarchingCubes> marching_cubes);

    float density(glm::vec3 p) const override;      // trilinear between the grid samples
    std::optional<Hit> below(glm::vec3 from, float distance) const override;

    size_t blocks() const { return cache.size(); }

private:
    struct Block {
        std::vector<float> samples;                 // (block_cubes + 1)^3, [x][z][y]
        std::vector<glm::vec3> triangles;           // 3 world vertices each
    };
    const Block& block(glm::ivec3 index) const;

    std::shared_ptr<TerrainGenerator> generator;
    std::shared_ptr<MarchingCubes> marching_cubes;
    mutable std::unordered_map<glm::ivec3, Block, IVec3Hash> cache;
};

// The world's ground field; a level plane at y = 0 when no system made one (the ground_flat prefab)
GroundField& groundField(entt::registry& reg);

// A tilted plane as the world's ground. params: { "pitch": 0.0, "roll": 0.0 } in degrees
class GroundPlaneSys : public System {
public:
    GroundPlaneSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "GroundPlane"; }
    void preinit() override;
    void tick(double h) override {}

private:
    float pitch = 0.f, roll = 0.f;
};

// The marching cubes terrain as the world's ground. params: { "seed": 1337 } (base's terrain is seed 1337)
class GroundTerrainSys : public System {
public:
    GroundTerrainSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "GroundTerrain"; }
    void preinit() override;
    void tick(double h) override {}

private:
    int seed = 1337;
};

void registerGroundField();

}

#endif
