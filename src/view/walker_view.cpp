#include "walker_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/agents/agent_io.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#include "logic/components/camera.h"
#include "scene/world.h"

using namespace dy;

WalkerViewSys::WalkerViewSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    world = params.value("world", 0);
    distance = params.value("distance", 4.f);
    height = params.value("height", 1.2f);
    side = params.value("side", 1.f);
    azimuth = params.value("azimuth", azimuth);
    camera = params.value("camera", true);
}

void WalkerViewSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || world >= (int) worlds->size()) return;
    auto& w = (*worlds)[world];
    auto* bot = w.reg.ctx().find<HumanBot>();
    if(!bot || !bot->built) return;

    glm::vec3 pelvis = bot->state.position[bot->body->spine];
    const glm::vec3 up(0.f, 1.f, 0.f);
    glm::vec3 toward = safeNormalize(horizontal(bot->target - pelvis));
    float heading = std::atan2(-toward.x, -toward.z);
    // A new episode starts somewhere else on a terrain: the camera goes there at once
    if(!has_focus || glm::length(focus - pelvis) > 6.f) {
        focus = pelvis;
        view_heading = heading;
        eye = pelvis + up * 1e4f;
        has_focus = true;
    }
    focus = glm::mix(focus, pelvis, 1.f - std::exp(-(float) dt * 6.f));

    if(camera && bot->ground) {
        const GroundField& ground = *bot->ground;
        float turn = heading - view_heading;
        while(turn > 3.14159265f) turn -= 6.2831853f;
        while(turn < -3.14159265f) turn += 6.2831853f;
        view_heading += turn * (1.f - std::exp(-(float) dt * 1.f));

        struct Place { float side, turn, lift; };
        const Place places[] = { { 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f }, { 1.f, 0.f, 1.5f }, { -1.f, 0.f, 1.5f }, { 1.f, 60.f, 2.f }, { -1.f, 60.f, 2.f },
            { 1.f, 0.f, 3.5f }, { -1.f, 0.f, 3.5f }, { 1.f, 100.f, 4.f } };
        const int count = (int) (sizeof(places) / sizeof(places[0]));
        auto at = [&](const Place& place) {
            float a = view_heading + place.side * side * glm::radians(azimuth + place.turn);
            glm::vec3 e = focus + glm::vec3(std::sin(a), 0.f, std::cos(a)) * distance + up * (height - 1.f + place.lift);
            if(auto hit = ground.below(e + up * 50.f, 100.f)) e.y = std::max(e.y, hit->point.y + 0.4f);
            return e;
        };
        auto sees = [&](glm::vec3 from, glm::vec3 to) {
            for(int i = 1; i < 16; i++) {
                glm::vec3 point = glm::mix(from, to, i / 16.f);
                if(auto hit = ground.below(point + up * 30.f, 60.f)) {
                    if(hit->point.y > point.y - 0.03f) return false;
                }
            }
            return true;
        };
        auto clear = [&](const Place& place) {
            glm::vec3 e = at(place);
            for(int foot : { bot->body->left_foot, bot->body->right_foot }) if(!sees(e, bot->state.position[foot] + up * 0.05f)) return false;
            return sees(e, focus);
        };
        if(!clear(places[std::clamp(view_choice, 0, count - 1)])) {
            view_choice = count - 1;
            for(int i = 0; i < count; i++) if(clear(places[i])) { view_choice = i; break; }
        } else if(view_choice != 0 && clear(places[0])) {
            view_choice = 0;
        }
        glm::vec3 wanted = at(places[view_choice]);
        if(glm::length(eye - focus) > 4.f * distance + 8.f) eye = wanted;
        eye = glm::mix(eye, wanted, 1.f - std::exp(-(float) dt * 2.5f));
        glm::vec3 d = glm::normalize(focus - up * 0.25f - eye);
        auto& cam = reg.ctx().get<Camera>();
        cam.position = eye;
        cam.pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
        cam.yaw = std::atan2(-d.x, -d.z);
    } else if(camera) {
        auto& cam = reg.ctx().get<Camera>();
        eye = focus + glm::vec3(distance * side, height, distance * 0.35f);
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
    if(bot->ground) {
        if(auto hit = bot->ground->below(pelvis + up, 8.f)) {
            glm::vec3 right(-toward.z, 0.f, toward.x);
            ImGui::Text("slope %.1f deg along, %.1f across", glm::degrees(std::atan2(-glm::dot(hit->normal, toward), hit->normal.y)), glm::degrees(std::atan2(-glm::dot(hit->normal, right), hit->normal.y)));
        }
    }
    ImGui::End();

}
