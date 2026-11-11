#include "animal_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/agents/agent_io.h"
#include "logic/animals/animal.h"
#include "logic/animals/animal_agent.h"
#include "logic/components/camera.h"
#include "logic/components/transform.h"
#include "scene/world.h"
#include "view/world_view.h"

using namespace dy;

AnimalAgentViewSys::AnimalAgentViewSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    world = params.value("world", world);
    distance = params.value("distance", distance);
    height = params.value("height", height);
    azimuth = params.value("azimuth", azimuth);
    spacing = params.value("spacing", spacing);
    camera = params.value("camera", camera);
    mesh = params.value("mesh", mesh);
}

void AnimalAgentViewSys::preinit() {
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<Transform>();
    animalLibrary(reg);
}

void AnimalAgentViewSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds) return;
    auto& library = animalLibrary(reg);
    shown.resize(worlds->size());

    for(size_t w = 0; mesh && w < worlds->size(); w++) {
        auto* bot = (*worlds)[w].reg.ctx().find<AnimalBot>();
        Shown& s = shown[w];
        if(!bot || !bot->built || s.failed) continue;
        const AnimalBody& body = *bot->body;
        if(!s.species) {
            int index = library.find(body.species);
            s.species = index >= 0 ? library.species((uint32_t) index) : nullptr;
            if(!s.species) {
                s.failed = true;
                continue;
            }
            s.bind = animalBodyBind(body, *s.species);
            s.entity = reg.create();
            reg.emplace<Transform>(s.entity);
            Animal animal;
            animal.species = (uint32_t) index;
            reg.emplace<Animal>(s.entity, animal);
            reg.emplace<AnimalPose>(s.entity);
        }
        // The joints from the segments, about the trunk
        const glm::vec3 offset = WorldViewSys::gridOffset((uint32_t) w, worlds->size(), spacing);
        const glm::vec3 origin = bot->position[0];
        auto& pose = reg.get<AnimalPose>(s.entity);
        std::vector<glm::mat4> joints;
        skeletonFromSegments(body, s.bind, *s.species, bot->position, bot->rotation, joints);
        const glm::mat4 back = glm::translate(glm::mat4(1.f), -origin);
        pose.world.resize(joints.size());
        for(size_t j = 0; j < joints.size(); j++) pose.world[j] = back * joints[j];
        s.species->skinMatrices(pose.world, pose.skin);
        auto& transform = reg.get<Transform>(s.entity);
        transform.position = origin + offset;
        transform.rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
        transform.scale = glm::vec3(1.f);
    }

    if(world < 0 || world >= (int) worlds->size()) return;
    auto& w = (*worlds)[world];
    auto* bot = w.reg.ctx().find<AnimalBot>();
    if(!bot || !bot->built) return;
    const AnimalBody& body = *bot->body;
    const glm::vec3 offset = WorldViewSys::gridOffset((uint32_t) world, worlds->size(), spacing);
    const glm::vec3 trunk = bot->position[0] + offset;
    const float size = std::max(body.length, body.height);

    if(camera) {
        if(!has_focus || glm::length(focus - trunk) > 3.f * size + 3.f) {
            focus = trunk;
            view_heading = bot->command.yaw;
            has_focus = true;
        }
        focus = glm::mix(focus, trunk, 1.f - std::exp(-(float) dt * 6.f));
        float turn = bot->command.yaw - view_heading;
        while(turn > 3.14159265f) turn -= 6.2831853f;
        while(turn < -3.14159265f) turn += 6.2831853f;
        view_heading += turn * (1.f - std::exp(-(float) dt * 0.6f));
        // From behind the heading, swung to the side
        const float a = view_heading + glm::radians(azimuth);
        glm::vec3 aim = focus;
        aim.y = bot->groundUnder(bot->position[0]) + 0.5f * body.height;
        glm::vec3 eye = aim + glm::vec3(std::sin(a), 0.f, std::cos(a)) * (distance * size + 0.5f) + glm::vec3(0.f, height * body.height + 0.1f, 0.f);
        glm::vec3 d = glm::normalize(aim - eye);
        auto& cam = reg.ctx().get<Camera>();
        cam.position = eye;
        cam.pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
        cam.yaw = std::atan2(-d.x, -d.z);
    }

    const auto& io = w.reg.ctx().get<AgentIO>();
    ImGui::Begin("Animal");
    ImGui::Text("%s: %d bodies, %.1f kg", body.species.c_str(), (int) body.def.segments.size(), body.mass);
    ImGui::Text("tick %d, decisions %d", bot->tick, bot->decisions);
    ImGui::Text("command %.2f m/s (%.2f of the walk), turn %.2f rad/m", bot->command.speed, bot->command.speed / std::max(body.walk_speed, 1e-3f), bot->command.turn);
    glm::vec3 velocity = bot->origin_velocity[0];
    ImGui::Text("speed %.2f m/s", std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
    ImGui::Text("trunk %.2f of its standing height (fallen under %.2f)", bot->trunkHeight() / body.stand_height, bot->params.fall_height);
    ImGui::Text("reward %.3f, episode %.2f", io.reward, bot->cumulative_reward);
    ImGui::End();

}
