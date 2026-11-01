#include "world_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "logic/components/model_ref.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"
#include "renderer/model/model_manager.h"
#include "scene/world.h"
#include "util/primitive_mesh.h"
#include "view/components/model_renderable.h"
#include "view/world_stepper.h"

using namespace dy;

namespace {

const char* const box_model = "./resources/models/primitives/box.gltf";
const char* const sphere_model = "./resources/models/primitives/sphere.gltf";

glm::vec4 bodyColor(entt::entity e) {
    uint32_t h = entt::to_integral(e) * 2654435761u;
    return glm::vec4(0.35f + 0.6f * ((h >> 8) & 255) / 255.f, 0.35f + 0.6f * ((h >> 16) & 255) / 255.f, 0.35f + 0.6f * ((h >> 24) & 255) / 255.f, 1.f);
}

}

WorldViewSys::WorldViewSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    spacing = params.value("spacing", 6.f);
    if(auto it = params.find("worlds"); it != params.end() && it->is_array()) {
        for(auto& i : *it) only.push_back(i.get<uint32_t>());
    }
    if(auto it = params.find("colors"); it != params.end() && it->is_array()) {
        for(auto& c : *it) colors.push_back(glm::vec4(c.at(0).get<float>(), c.at(1).get<float>(), c.at(2).get<float>(), c.size() > 3 ? c.at(3).get<float>() : 1.f));
    }
}

glm::vec3 WorldViewSys::gridOffset(uint32_t index, size_t count, float spacing) {
    int cols = std::max(1, (int) std::ceil(std::sqrt((double) count)));
    int rows = ((int) count + cols - 1) / cols;
    int x = (int) index % cols, z = (int) index / cols;
    return glm::vec3((x - (cols - 1) * 0.5f) * spacing, 0.f, (z - (rows - 1) * 0.5f) * spacing);
}

void WorldViewSys::tick(double dt) {

    OPTICK_EVENT();

    auto* worlds = reg.ctx().find<Worlds>();
    auto* models = reg.ctx().find<ModelManager>();
    if(!worlds || !models) return;

    const auto* stepper = reg.ctx().find<WorldStepperState>();
    const float alpha = stepper ? (float) stepper->alpha : 1.f;

    std::vector<std::pair<uint32_t, entt::entity>> seen;

    for(size_t wi = 0; wi < worlds->size(); wi++) {
        uint32_t index = (uint32_t) wi;
        if(!only.empty() && std::find(only.begin(), only.end(), index) == only.end()) continue;
        World& world = (*worlds)[wi];
        glm::vec3 offset = gridOffset(index, worlds->size(), spacing);

        auto place = [&](entt::entity source, entt::entity model, glm::vec4 color, glm::vec3 position, glm::quat rotation, glm::vec3 scale) {
            auto key = std::make_pair(index, source);
            seen.push_back(key);
            auto it = links.find(key);
            entt::entity host;
            if(it == links.end()) {
                host = reg.create();
                reg.emplace<Transform>(host);
                reg.emplace<ModelRenderable>(host, model, color);
                reg.emplace<WorldViewLink>(host, index, source);
                links.emplace(key, host);
            } else {
                host = it->second;
            }
            auto& t = reg.get<Transform>(host);
            t.position = offset + position;
            t.rotation = rotation;
            t.scale = scale;
        };

        for(auto [e, ground] : world.reg.view<Ground>().each()) {
            if(!ground.created()) continue;
            place(e, models->load(box_model), glm::vec4(0.55f, 0.57f, 0.5f, 1.f), glm::vec3(0.f, ground.top_y - ground.size.y * 0.5f, 0.f),
                glm::quat(1.f, 0.f, 0.f, 0.f), ground.size);
        }

        for(auto [e, transform, body] : world.reg.view<Transform, RigidBody>().each()) {
            if(!body.created()) continue;

            glm::vec3 position = transform.position;
            glm::quat rotation = transform.rotation;
            if(auto* pose = world.reg.try_get<PhysicsPose>(e)) {
                position = glm::mix(pose->prev_position, pose->position, alpha);
                rotation = glm::slerp(pose->prev_rotation, pose->rotation, alpha);
            }
            position += rotation * body.offset;

            glm::vec4 color = bodyColor(e);
            if(auto* ref = world.reg.try_get<ModelRef>(e)) color = ref->color;
            if(!colors.empty()) color = colors[index % colors.size()];

            entt::entity model = entt::null;
            glm::vec3 scale(1.f);
            if(body.shape == RigidBody::Shape::Box) {
                model = models->load(box_model);
                scale = body.half_extent * 2.f;
            } else if(body.shape == RigidBody::Shape::Sphere) {
                model = models->load(sphere_model);
                scale = glm::vec3(body.half_extent.x * 2.f);
            } else {
                char key[64];
                std::snprintf(key, sizeof(key), "capsule:%.6g:%.6g", body.half_extent.x, body.half_extent.y);
                model = models->add(key, capsuleMesh(body.half_extent.x, body.half_extent.y));
            }
            if(model == entt::null) continue;
            place(e, model, color, position, rotation, scale);
        }
    }

    // Bodies that went away take their host entity with them
    std::sort(seen.begin(), seen.end());
    for(auto it = links.begin(); it != links.end();) {
        if(!std::binary_search(seen.begin(), seen.end(), it->first)) {
            if(reg.valid(it->second)) reg.destroy(it->second);
            it = links.erase(it);
        } else {
            ++it;
        }
    }

}

void WorldViewSys::finish() {
    for(auto& [key, host] : links) {
        if(reg.valid(host)) reg.destroy(host);
    }
    links.clear();
}
