#ifndef DY_ANIMAL_LINEUP_H
#define DY_ANIMAL_LINEUP_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "logic/animals/animal.h"
#include "logic/systems/system.h"

namespace dy {

class GroundField;
class TerrainGenerator;

// The middle of the most level ground of this half size within 400 m of the origin of the world, by the
// heightfield part of the terrain
glm::vec2 levelGround(const TerrainGenerator& generator, glm::vec2 half_size);

// On an animal of the lineup : it plays its clips one after the other
struct AnimalShow {
    std::vector<int> clips;     // empty : the bind pose
    size_t at = 0;
    float left = 0.f;           // seconds of the current clip left
};

// The animals of the library in rows on the ground, to look at them and at their clips (scene animals_test).
// Positions are logged. Before AnimalAnimation in the scene.
//
// params : "species" ([ids], default : all of them),
//          "origin" ([x, z] of the middle of the first row; without it, the most level ground within 400 m),
//          "camera" (true : the desktop camera is put in front of the first row),
//          "per_row" (10), "gap" (1 m between two animals),
//          "clips" : "all" (each clip in turn), a kind ("walk", "idle", "run", "eat", "rest"), a clip name, or
//                    "none" (the bind pose),
//          "seconds" (4 : how long a looping clip is held), "yaw" (degrees, 180 : facing +z)
class AnimalLineupSys : public System {
public:
    AnimalLineupSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "AnimalLineup"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;

private:
    std::vector<std::string> ids;
    glm::vec2 origin = glm::vec2(0.f, -12.f);
    bool has_origin = false;
    bool move_camera = true;
    int per_row = 10;
    float gap = 1.f;
    std::string clips = "all";
    float seconds = 4.f;
    float yaw = 180.f;
};

}

#endif
