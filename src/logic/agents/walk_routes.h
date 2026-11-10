#ifndef DY_WALK_ROUTES_H
#define DY_WALK_ROUTES_H

#include <glm/glm.hpp>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <optional>

#include "logic/systems/system.h"

namespace dy {

class GroundField;
class WorldRng;

// Where a walker's episodes go on ground that is not level everywhere (docs/physical-agents/reports/terrain-walker.md): the
// marching cubes terrain is steep almost everywhere, and the walker does not see it. Spawn points and target
// directions are drawn until the ground along the path stays inside a slope band. Component of the entity with
// HumanBotSpawn; angles in radians (degrees in JSON)
struct WalkRoutes {
    float up = 0.349066f;                       // steepest climb along a path (20 degrees)
    float down = 0.122173f;                     // steepest descent along it (7 degrees: beyond, the kinematic walk's swing knee passes 95 degrees)
    float across = 0.209440f;                   // steepest slope across it (12 degrees)
    float length = 12.f;                        // m of path ahead that must stay in the band
    float half_width = 1.f;                     // m to each side of the path that must stay in it too
    float clear = 1.5f;                         // m around a spawn point where the ground is no steeper than the least of the three, whatever the direction
    glm::vec2 centre = glm::vec2(0.f);          // spawn points are drawn within spawn_radius of it (x, z)
    float spawn_radius = 1000.f;
    int direction_tries = 96;                   // directions tried from a point, spread evenly around it
    int spawn_tries = 20000;                    // points tried for a spawn
};

// The ground along a straight path, as a walker heading along it meets it
struct RouteSlope {
    bool ground = true;                         // false: somewhere on the path there is no ground
    float up = 0.f, down = 0.f;                 // the steepest climb and the steepest descent along the path, radians (both >= 0)
    float across = 0.f;                         // the steepest slope across it
    float rise = 0.f;                           // m from the start to the end
    float mean_across = 0.f;                    // mean slope across the path, signed: positive rises to the right
};

// Measures the ground from `from` along the horizontal unit vector `direction` for `length` metres, on the path and
// `half_width` to each side, every `step` metres: facet normals, and height differences between samples (a ledge
// between two level facets is a slope too). Stops at the first sample outside the band when `limits` is given
RouteSlope measureRoute(const GroundField& ground, glm::vec3 from, glm::vec3 direction, float length, float half_width,
    float step = 0.5f, const WalkRoutes* limits = nullptr);

bool inBand(const RouteSlope& slope, const WalkRoutes& routes);

// A direction from `from` whose path stays in the band; none after direction_tries of them
std::optional<glm::vec3> drawDirection(const GroundField& ground, WorldRng& rng, const WalkRoutes& routes, glm::vec3 from);

struct RouteStart {
    bool found = false;                         // false: no point qualified, `point` is the last one tried
    glm::vec3 point = glm::vec3(0.f);           // on the ground
    glm::vec3 direction = glm::vec3(0.f, 0.f, -1.f);
    int tries = 0;
};

// A spawn point and a first direction: gentle ground within `clear` of the point, and a path in the band
RouteStart drawStart(const GroundField& ground, WorldRng& rng, const WalkRoutes& routes);

// The route the kinematic walker is recording (GaitRoutes): component of the rig entity
struct GaitRoute {
    int episode = 0;                            // routes started so far
    bool found = false;                         // drawn in the slope band
    glm::vec3 from = glm::vec3(0.f), direction = glm::vec3(0.f, 0.f, -1.f);
    uint64_t start_tick = 0, warm_ticks = 0;    // recorded from start_tick + warm_ticks
};

// The recording episodes of the kinematic walk (walker_demo_gait, walker_demo_terrain): each one walks a fresh route
// drawn by the entity's WalkRoutes (the path long enough for the whole episode). The walk starts from a stand and is
// not recorded for `warmup` seconds plus up to `jitter` more, drawn per route so that the recorded stretches start
// anywhere in the stride; then `decisions` decisions are recorded at the commanded speed. First in the fixed list,
// before GaitBase. params: { "warmup": 3.0, "jitter": 1.2, "decisions": 150 }
class GaitRoutesSys : public System {
public:
    GaitRoutesSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "GaitRoutes"; }
    void tick(double h) override;

private:
    void start(GaitRoute& route);

    float warmup = 3.f, jitter = 1.2f;
    int decisions = 150;
};

void registerWalkRoutes();

}

#endif
