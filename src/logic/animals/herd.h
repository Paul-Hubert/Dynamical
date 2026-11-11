#ifndef DY_HERD_H
#define DY_HERD_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "logic/animals/animal.h"
#include "logic/animals/gait.h"
#include "logic/systems/system.h"

namespace dy {

class ForestField;
class GroundField;
class TerrainGenerator;

// A kind of herd : which animals, how many, how far apart
struct HerdKind {
    enum class Where { Any, Open, Edge, Forest };

    std::string name;
    float weight = 1.f;             // how often this kind is drawn
    int count_min = 4, count_max = 10;
    float spacing = 4.f;            // metres between two animals at rest
    float scale_min = 0.92f, scale_max = 1.08f;
    Where where = Where::Any;       // against the forest cover (ForestField), when the scene has forests
    std::vector<uint32_t> species;  // indices in the library
    std::vector<float> weights;     // cumulative, the last one is the total
    std::vector<float> scales;      // per species, times the herd's scale (calves)
};

struct HerdParams {
    uint32_t seed = 1;
    float cell = 180.f;             // metres : the world is cut in cells, each with a herd or none
    float density = 0.7f;           // part of the cells that have a herd
    float radius = 420.f;           // herds this close to the viewer exist
    float range = 140.f;            // how far from its home a herd goes
    float max_slope = 18.f;         // degrees : slope of the land herds keep off
    float graze_min = 20.f, graze_max = 60.f;   // seconds a herd stays before it moves on
};

struct HerdDefinition {
    HerdParams params;
    std::vector<HerdKind> kinds;

    // { "cell": 180, "density": 0.7, ..., "herds": [ { "name": "red deer", "weight": 3, "count": [6, 14], "spacing": 4,
    //     "scale": [0.92, 1.08], "where": "any" | "open" | "edge" | "forest",
    //     "members": { "<id>": weight, ... } or { "<id>": { "weight": 2, "scale": 0.6 } } } ] }
    // Animals the library does not have, or that have no walk, are left out with a warning; a herd left without
    // members is dropped. Throws std::runtime_error on a malformed file
    static HerdDefinition parse(const nlohmann::json& j, AnimalLibrary& library);
};

// On each animal of a herd
struct HerdAnimal {
    uint32_t herd = 0;              // index of its herd's kind
    glm::ivec2 cell = glm::ivec2(0);
    bool moving = false;
    float speed = 0.f;              // m/s over the ground
};

// A herd, for whoever looks at them
struct HerdInfo {
    glm::ivec2 cell = glm::ivec2(0);
    std::string kind;
    glm::vec3 centre = glm::vec3(0.f);  // of its animals, on the ground
    float extent = 0.f;                 // radius around the centre that holds them
    uint32_t animals = 0;
    bool travelling = false;
};

// Context variable written by HerdSys
struct HerdStats {
    uint32_t herds = 0;
    uint32_t animals = 0;
    uint32_t moving = 0;
    uint32_t travelling = 0;        // herds on their way to another place
    std::vector<HerdInfo> list;
};

// Herds of animals that graze and move on over the terrain (docs/animals.md). Herds are anchored to the world : a
// cell of the world has the same herd whenever the viewer comes back, starting from its home. Animals walk at the
// speed of their walk clip, turn at a limited rate, keep off each other and off the trunks of the trees, stand on
// the ground and lean with its slope. Needs AnimalAnimation after it in the scene.
//
// params : "definition" (resources path, "herds/tautavel.herds.json", or the object itself), and to override the
//          definition's : "seed", "cell", "density", "radius", "range", "max_slope", "graze" ([min, max] seconds),
//          "centre" ([x, z]) : herds are kept around this point instead of the desktop camera,
//          "procedural" (true) : the animals walk with the procedural walk (AnimalGait after AnimalAnimation in
//          the scene), "style" : its GaitStyle
class HerdSys : public System {
public:
    HerdSys(entt::registry& reg, const nlohmann::json& params);
    ~HerdSys() override;
    const char* name() override { return "Herds"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override;
    void finish() override;

    struct Member {
        entt::entity entity = entt::null;
        std::shared_ptr<const AnimalSpecies> species;
        glm::vec2 slot = glm::vec2(0.f);        // its place around the centre of the herd
        glm::vec2 spot = glm::vec2(0.f);        // where it stands now, around its slot
        glm::vec2 position = glm::vec2(0.f);
        float height = 0.f;
        float yaw = 0.f, pitch = 0.f;
        float scale = 1.f;
        float length = 1.f, width = 0.5f;       // scaled
        float walk_speed = 1.f;                 // scaled
        float speed = 0.f;
        float timer = 0.f;                      // until it does something else
        bool moving = false;
        bool placed = false;
    };

    struct Herd {
        glm::ivec2 cell = glm::ivec2(0);
        uint32_t kind = 0;
        uint32_t rng = 0;
        glm::vec2 home = glm::vec2(0.f), centre = glm::vec2(0.f), destination = glm::vec2(0.f);
        bool travelling = false;
        float timer = 0.f;
        float speed = 1.f;
        float extent = 1.f;                     // radius of the herd at rest
        std::vector<Member> members;
    };

    const std::vector<Herd>& all() const { return herds; }
    const HerdDefinition& def() const { return definition; }

    // The herd of a cell, or no herd : only depends on the definition, the seed and the land
    bool describe(glm::ivec2 cell, Herd& herd);

    // Home of the nearest herd of a kind within `reach` metres of a point; false without one
    bool nearest(const std::string& kind, glm::vec2 from, float reach, glm::vec2& home);

private:
    struct Trunk {
        glm::vec2 position;
        float radius;
    };

    bool suits(const HerdKind& kind, glm::vec2 p) const;
    float landSlope(glm::vec2 p) const;
    float groundHeight(glm::vec2 p, float near_y, bool known) const;
    void spawn(Herd& herd);
    void despawn(Herd& herd);
    void updateHerd(Herd& herd, float dt);
    void updateMember(Herd& herd, Member& member, float dt);
    void rebuildTrunks();

    nlohmann::json params;
    HerdDefinition definition;
    bool has_centre = false;
    glm::vec2 centre = glm::vec2(0.f);
    bool procedural = true;
    GaitStyle style;

    std::shared_ptr<GroundField> ground;
    std::shared_ptr<TerrainGenerator> generator;
    std::shared_ptr<ForestField> forest;

    std::vector<Herd> herds;
    std::unordered_map<uint64_t, std::vector<Trunk>> trunks;   // by cell of 8 m
    size_t trunk_plants = ~(size_t) 0;
    float trunk_timer = 0.f;
    float scan_timer = 0.f;
};

// The desktop camera goes round a herd, at the height of someone standing : videos of the herds
// (tools/herd_video.py). It takes the herd nearest to the camera, a travelling one first, and stays with it.
// After Herds and PlayerControl in the scene, which it overrides.
//
// params : "distance" (9 m from the herd), "height" (2.6 m above the ground, more when the ground hides the herd), "orbit" (5 degrees
//          a second), "kind" (name of a kind of herd to prefer : the one with the most other herds around it), "clear" (true) : the camera stays where no crown is
//          between it and the herd, it stops turning rather than go through a tree
class HerdWatchSys : public System {
public:
    HerdWatchSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "HerdWatch"; }
    void init() override;
    void tick(double dt) override;

private:
    float distance = 9.f;
    float height = 2.6f;
    float orbit = 5.f;
    bool clear = true;
    std::string kind;

    // How open the view of the herd is from an angle around it : metres between the sight line and the nearest crown
    float clearance(float at_angle) const;

    std::vector<glm::vec4> trees;       // x, z, radius of the crown, radius of what of it hangs in a sight line, around the herd
    float tree_timer = 0.f;
    float spin = 0.f;                   // radians a second around the herd
    bool has_spin = false;

    std::shared_ptr<GroundField> ground;
    bool has_herd = false;
    bool searched = false;
    float wait = 0.f;                   // seconds left for a herd of the kind asked for to show up
    glm::ivec2 cell = glm::ivec2(0);
    glm::vec3 focus = glm::vec3(0.f);
    float radius = 20.f;
    float angle = 0.f;
    float eye = 0.f;
};

}

#endif
