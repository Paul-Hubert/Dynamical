#include "sandbox_watch.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
}

void SandboxWatchSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || worlds->size() == 0) return;
    auto& world = (*worlds)[0].reg;
    auto* sb = world.ctx().find<Sandbox>();
    if(!sb) return;

    const SandboxAnimal* a = world.valid(target) ? world.try_get<SandboxAnimal>(target) : nullptr;
    if(!a || a->dead) {
        // Another one: of the species asked for, else a predator, else anyone; the first in entity order
        std::vector<entt::entity> list;
        for(auto e : world.view<SandboxAnimal, AnimalBot>()) if(!world.get<SandboxAnimal>(e).dead) list.push_back(e);
        std::sort(list.begin(), list.end());
        target = entt::null;
        int best = -1;
        for(auto e : list) {
            const auto& candidate = world.get<SandboxAnimal>(e);
            const SandboxSpecies& s = sb->species[candidate.species];
            const int rank = !species.empty() && s.life.id == species ? 3 : species.empty() && s.life.predator ? 2 : 1;
            if(rank > best) {
                best = rank;
                target = e;
            }
        }
        if(target == entt::null) return;
        a = &world.get<SandboxAnimal>(target);
    }

    const auto& bot = world.get<AnimalBot>(target);
    const glm::vec3 at = bot.position[0];
    // The camera eases after the animal: a fall or a blow does not shake the view
    if(!has_focus) focus = at;
    has_focus = true;
    focus += (at - focus) * std::min(4.f * (float) dt, 1.f);
    angle += glm::radians(orbit) * (float) dt;

    const float reach = distance * std::max(bot.body->length, bot.body->height);
    auto& camera = reg.ctx().get<Camera>();
    camera.position = focus + glm::vec3(std::cos(angle) * reach, height * bot.body->height - bot.body->stand_height, std::sin(angle) * reach);
    camera.position.y = std::max(camera.position.y, 0.4f);
    const glm::vec3 dir = glm::normalize(focus - camera.position);
    camera.pitch = std::asin(glm::clamp(dir.y, -1.f, 1.f));
    camera.yaw = std::atan2(-dir.x, -dir.z);

}
