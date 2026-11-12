#include "sandbox_watch.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "logic/animals/animal_agent.h"
#include "logic/components/camera.h"
#include "logic/sandbox/sandbox.h"
#include "scene/world.h"

using namespace dy;

SandboxWatchSys::SandboxWatchSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    species = params.value("species", species);
    distance = params.value("distance", distance);
    height = params.value("height", height);
    orbit = params.value("orbit", orbit);
    tour = params.value("tour", tour);
    stay = params.value("stay", stay);
}

// A living animal to go to. The first one: of the species asked for, else a predator, else anyone, the first in entity
// order. `another`: the nearest to where the camera looks of a species other than the last one's, among those it has
// not just been to (anyone else when there is none)
entt::entity SandboxWatchSys::pick(entt::registry& world, bool another) const {
    const auto& sb = world.ctx().get<Sandbox>();
    std::vector<entt::entity> list;
    for(auto e : world.view<SandboxAnimal, AnimalBot>()) if(!world.get<SandboxAnimal>(e).dead) list.push_back(e);
    std::sort(list.begin(), list.end());
    entt::entity best = entt::null;
    float best_score = -1e30f;
    for(auto e : list) {
        const auto& candidate = world.get<SandboxAnimal>(e);
        float score;
        if(another) {
            if(e == target) continue;
            const glm::vec3 d = world.get<AnimalBot>(e).position[0] - focus;
            score = -std::sqrt(d.x * d.x + d.z * d.z);
            if(candidate.species == target_species) score -= 1e6f;
            if(std::find(visited.begin(), visited.end(), e) != visited.end()) score -= 1e4f;
        } else {
            const SandboxSpecies& s = sb.species[candidate.species];
            score = !species.empty() && s.life.id == species ? 3.f : species.empty() && s.life.predator ? 2.f : 1.f;
        }
        if(score > best_score) {
            best_score = score;
            best = e;
        }
    }
    return best;
}

void SandboxWatchSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || worlds->size() == 0) return;
    auto& world = (*worlds)[0].reg;
    if(!world.ctx().find<Sandbox>()) return;

    const SandboxAnimal* a = world.valid(target) ? world.try_get<SandboxAnimal>(target) : nullptr;
    const bool lost = !a || a->dead;
    bool leave = lost;
    if(tour && !lost && !flying) {
        timer += (float) dt;
        leave = timer >= stay;
    }
    if(leave) {
        entt::entity next = pick(world, tour && has_focus);
        if(next == entt::null && lost) return;
        if(next != entt::null) {
            target = next;
            target_species = world.get<SandboxAnimal>(target).species;
            visited.push_back(target);
            if(visited.size() > 6) visited.erase(visited.begin());
            if(tour && has_focus) {
                flying = true;
                from = focus;
                from_reach = reach;
                from_rise = rise;
                const glm::vec3 d = world.get<AnimalBot>(target).position[0] - focus;
                flight = std::clamp(std::sqrt(d.x * d.x + d.z * d.z) / 10.f, 1.5f, 4.f);
            }
        }
        timer = 0.f;
    }

    const auto& bot = world.get<AnimalBot>(target);
    const glm::vec3 at = bot.position[0];
    const float target_reach = distance * std::max(bot.body->length, bot.body->height);
    const float target_rise = height * bot.body->height - bot.body->stand_height;
    if(!has_focus) {
        focus = at;
        reach = target_reach;
        rise = target_rise;
    }
    has_focus = true;

    float turn = 1.f;
    glm::vec3 look;
    if(flying) {
        timer += (float) dt;
        const float t = std::min(timer / std::max(flight, 1e-3f), 1.f), s = t * t * (3.f - 2.f * t);
        // Back and up over the middle of the way, by how far it is
        const glm::vec3 way = at - from;
        const float far = std::sqrt(way.x * way.x + way.z * way.z), bump = std::sin(glm::pi<float>() * t);
        focus = glm::mix(from, at, s);
        // It turns to the next animal before it gets there: the animal is in the picture for most of the way
        const float ahead = std::min(1.8f * t, 1.f);
        look = glm::mix(from, at, ahead * ahead * (3.f - 2.f * ahead));
        reach = glm::mix(from_reach, target_reach, s) + bump * std::min(0.15f * far, 6.f);
        rise = glm::mix(from_rise, target_rise, s) + bump * std::min(0.1f * far, 4.f);
        turn = 1.f - 0.7f * bump;
        if(t >= 1.f) {
            flying = false;
            timer = 0.f;
        }
    } else {
        // The camera eases after the animal: a fall or a blow does not shake the view
        focus += (at - focus) * std::min((tour ? 8.f : 4.f) * (float) dt, 1.f);
        look = focus;
        reach = target_reach;
        rise = target_rise;
    }
    angle += glm::radians(orbit) * turn * (float) dt;

    auto& camera = reg.ctx().get<Camera>();
    camera.position = focus + glm::vec3(std::cos(angle) * reach, rise, std::sin(angle) * reach);
    camera.position.y = std::max(camera.position.y, 0.4f);
    const glm::vec3 dir = glm::normalize(look - camera.position);
    camera.pitch = std::asin(glm::clamp(dir.y, -1.f, 1.f));
    camera.yaw = std::atan2(-dir.x, -dir.z);

}
