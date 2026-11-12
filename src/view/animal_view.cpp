#include "animal_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "logic/agents/agent_io.h"
#include "logic/animals/animal.h"
#include "logic/animals/animal_agent.h"
#include "logic/animals/steer.h"
#include "logic/components/camera.h"
#include "logic/components/frame_clock.h"
#include "logic/components/transform.h"
#include "renderer/model/model_manager.h"
#include "scene/world.h"
#include "view/components/model_renderable.h"
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
    marks = params.value("marks", marks);
    log_path = params.value("log", log_path);
    if(auto it = params.find("orders"); it != params.end()) {
        // A scene variable is a string
        nlohmann::json list = it->is_string() ? (it->get<std::string>().empty() ? nlohmann::json::array() : nlohmann::json::parse(it->get<std::string>())) : *it;
        for(auto& o : list) orders.push_back({ o.at(0).get<float>(), o.at(1).get<float>(), o.at(2).get<float>() });
    }
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
            reg.emplace<AnimalSkin>(s.entity);
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

    // The orders of the scene instead of the draws: what the task and the observation read is the same command
    int order = -1;
    if(!orders.empty()) {
        WalkCommand& command = bot->command;
        tour += std::max(command.time - last_time, 0.f);
        last_time = command.time;
        float from = 0.f;
        for(size_t i = 0; i < orders.size(); i++) {
            order = (int) i;
            if(tour < from + orders[i][0]) break;
            from += orders[i][0];
        }
        const auto& o = orders[order];
        const float never = std::numeric_limits<float>::max();
        if(o[1] > 0.f) {
            command.wanted_speed = o[1] * body.walk_speed;
            command.wanted_turn = o[2] == 0.f ? 0.f : (o[2] > 0.f ? 1.f : -1.f) / (std::abs(o[2]) * body.length);
            command.until = never;
            // An episode starts on its first order, not on a draw
            if(command.time < 0.05f) {
                command.speed = command.wanted_speed;
                command.turn = command.wanted_turn;
            }
        } else if(command.until == never) {
            command.until = command.time;
        }
    }

    if(marks > 0.f) {
        // A chequer fixed to the world, laid where the animal is
        const int cells = 40;
        if(tiles.empty()) {
            auto& models = reg.ctx().get<ModelManager>();
            const entt::entity box = models.load("./resources/models/primitives/box.gltf");
            for(int i = 0; i < cells * cells / 2; i++) {
                entt::entity e = reg.create();
                reg.emplace<Transform>(e);
                reg.emplace<ModelRenderable>(e, box, glm::vec4(0.44f, 0.47f, 0.4f, 1.f));
                tiles.push_back(e);
            }
        }
        const float ground = bot->groundUnder(bot->position[0]);
        const int cx = (int) std::floor(trunk.x / marks), cz = (int) std::floor(trunk.z / marks);
        size_t next = 0;
        for(int i = -cells / 2; i < cells / 2; i++) {
            for(int j = -cells / 2; j < cells / 2; j++) {
                if(((cx + i + cz + j) & 1) != 0 || next >= tiles.size()) continue;
                auto& t = reg.get<Transform>(tiles[next++]);
                t.position = glm::vec3((cx + i + 0.5f) * marks, ground + 0.002f, (cz + j + 0.5f) * marks);
                t.rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
                t.scale = glm::vec3(marks, 0.004f, marks);
            }
        }
    }

    if(!log_path.empty()) {
        if(!log.is_open()) {
            log.open(log_path);
            log << "frame,tick,time,order,speed,turn,wanted_speed,wanted_turn,yaw,x,z,vx,vz,trunk,walk_speed,length\n";
        }
        const auto* clock = reg.ctx().find<FrameClock>();
        const glm::vec3 v = bot->origin_velocity[0];
        log << (clock ? clock->frame : 0) << ',' << bot->tick << ',' << bot->command.time << ',' << order << ',' << bot->command.speed << ','
            << bot->command.turn << ',' << bot->command.wanted_speed << ',' << bot->command.wanted_turn << ',' << bot->command.yaw << ','
            << bot->position[0].x << ',' << bot->position[0].z << ',' << v.x << ',' << v.z << ',' << bot->trunkHeight() / body.stand_height << ','
            << body.walk_speed << ',' << body.length << "\n";
        log.flush();
    }

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
