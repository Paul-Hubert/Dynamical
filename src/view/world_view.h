#ifndef DY_WORLD_VIEW_H
#define DY_WORLD_VIEW_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <utility>
#include <vector>

namespace dy {

// Host entity drawing one shape of a world body
struct WorldViewLink {
    uint32_t world;
    entt::entity entity;
};

// Draws the bodies of the worlds (scenes.md, WorldView): one host entity with Transform and ModelRenderable per
// shape, so the renderer keeps reading the host registry only. World i sits at grid(i) * spacing on the XZ plane.
// Boxes and spheres scale the unit primitives of resources/models/primitives; capsules are generated per
// (radius, half-height). Dynamic bodies are drawn between their last two ticks with the WorldStepper alpha.
// params: { "worlds": "all" or [indices], "spacing": 6.0, "colors": [[r, g, b(, a)], ...] (optional: world i's bodies take
//           colors[i % n] instead of their ModelRef color) }
class WorldViewSys : public System {
public:
    WorldViewSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "WorldView"; }
    void tick(double dt) override;
    void finish() override;

    // Offset of world i among n on the XZ grid (row-major, centered on the origin)
    static glm::vec3 gridOffset(uint32_t index, size_t count, float spacing);

private:
    float spacing = 6.f;
    std::vector<uint32_t> only;     // empty: every world
    std::vector<glm::vec4> colors;  // per world index, cyclic; empty: the bodies' own
    std::map<std::pair<uint32_t, entt::entity>, entt::entity> links;
};

}

#endif
