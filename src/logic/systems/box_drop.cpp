#include "box_drop.h"

#include <cmath>

#include "logic/components/model_ref.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "scene/world.h"

using namespace dy;

BoxDropSys::BoxDropSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    count = params.value("count", count);
    spacing = params.value("spacing", spacing);
    if(params.contains("height")) height = glm::vec2(params["height"][0].get<float>(), params["height"][1].get<float>());
    if(params.contains("half_extent")) half_extent = glm::vec2(params["half_extent"][0].get<float>(), params["half_extent"][1].get<float>());
    capsule_every = params.value("capsule_every", capsule_every);
}

void BoxDropSys::init() {

    auto& rng = reg.ctx().get<WorldRng>();
    const int cols = std::max(1, (int) std::ceil(std::sqrt((double) count)));
    const int rows = (count + cols - 1) / cols;

    for(int i = 0; i < count; i++) {
        float half = (float) rng.uniform(half_extent.x, half_extent.y);
        float jitter = 0.2f * spacing;
        glm::vec3 position(((i % cols) - (cols - 1) * 0.5f) * spacing + (float) rng.uniform(-jitter, jitter),
                           (float) rng.uniform(height.x, height.y) + half,
                           ((i / cols) - (rows - 1) * 0.5f) * spacing + (float) rng.uniform(-jitter, jitter));
        glm::quat rotation = glm::angleAxis((float) rng.uniform(0., 6.283185307179586), rng.unitSphere());

        auto e = reg.create();
        reg.emplace<Transform>(e, position, rotation);
        auto& body = reg.emplace<RigidBody>(e);
        body.friction = 1.f;
        body.restitution = 0.f;
        if(capsule_every > 0 && i % capsule_every == capsule_every - 1) {
            body.shape = RigidBody::Shape::Capsule;
            body.half_extent = glm::vec3(half * 0.6f, half * 1.5f, half * 0.6f);
            // Jolt has no rolling resistance: without damping a capsule on its side rolls on for a long time
            body.angular_damping = 1.f;
            reg.emplace<ModelRef>(e, "models/primitives/capsule", glm::vec4(0.95f, 0.75f, 0.25f, 1.f));
        } else {
            body.half_extent = glm::vec3(half);
            reg.emplace<ModelRef>(e, "models/primitives/box.gltf", glm::vec4(0.3f + 0.5f * (float) rng.uniform(), 0.45f, 0.85f, 1.f));
        }
    }

}
