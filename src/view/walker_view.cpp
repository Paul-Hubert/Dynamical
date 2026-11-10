#include "walker_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/agents/agent_io.h"
#include "logic/agents/humanbot.h"
#include "logic/components/camera.h"
#include "scene/world.h"

using namespace dy;

WalkerViewSys::WalkerViewSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    world = params.value("world", 0);
    distance = params.value("distance", 4.f);
    height = params.value("height", 1.2f);
    side = params.value("side", 1.f);
    camera = params.value("camera", true);
}

void WalkerViewSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || world >= (int) worlds->size()) return;
    auto& w = (*worlds)[world];
    auto* bot = w.reg.ctx().find<HumanBot>();
    if(!bot || !bot->built) return;

    glm::vec3 pelvis = bot->state.position[bot->body->spine];
    if(!has_focus) { focus = pelvis; has_focus = true; }
    focus = glm::mix(focus, pelvis, 1.f - std::exp(-(float) dt * 6.f));

    if(camera) {
        auto& cam = reg.ctx().get<Camera>();
        glm::vec3 eye = focus + glm::vec3(distance * side, height, distance * 0.35f);
        glm::vec3 d = glm::normalize(focus + glm::vec3(0.f, 0.1f, 0.f) - eye);
        cam.position = eye;
        cam.pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
        cam.yaw = std::atan2(-d.x, -d.z);
    }

    const auto& io = w.reg.ctx().get<AgentIO>();
    ImGui::Begin("Walker");
    ImGui::Text("tick %d, decisions %d", bot->tick, bot->decisions);
    ImGui::Text("target speed %.2f m/s", bot->target_speed);
    glm::vec3 velocity = horizontal(bot->state.linear[bot->body->spine]);
    ImGui::Text("speed %.2f m/s", glm::length(velocity));
    ImGui::Text("head height %.3f (kill below %.3f)", bot->currentHeight(), bot->params.dying_height_factor * bot->est0);
    ImGui::Text("reward %.3f, episode %.2f", io.reward, bot->cumulative_reward);
    ImGui::End();

}
