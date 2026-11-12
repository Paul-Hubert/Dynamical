#include "steer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "logic/agents/ground_field.h"
#include "logic/animals/animal.h"
#include "logic/animals/animal_agent.h"
#include "logic/animals/gait.h"
#include "logic/animals/herd.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "scene/system_registry.h"
#include "scene/world.h"

using namespace dy;

namespace {

const float pi = 3.14159265f;

float wrap(float a) {
    while(a > pi) a -= 2.f * pi;
    while(a < -pi) a += 2.f * pi;
    return a;
}

}

float dy::headingOf(glm::quat rotation) {
    const glm::vec3 facing = rotation * glm::vec3(0.f, 0.f, -1.f);
    return std::atan2(-facing.x, -facing.z);
}



// SteeredWalk

SteeredWalkSys::SteeredWalkSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    response = params.value("response", response);
    turn = params.value("turn", turn);
}

void SteeredWalkSys::preinit() {
    reg.storage<AnimalSteer>();
    reg.storage<SteeredWalk>();
    reg.storage<HerdAnimal>();
    reg.storage<AnimalBotProxy>();
    reg.storage<Animal>();
    reg.storage<AnimalGait>();
    reg.storage<Transform>();
}

void SteeredWalkSys::init() {
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());
    else ground = std::make_shared<PlaneField>();
}

void SteeredWalkSys::tick(double dt_seconds) {

    const float dt = (float) std::min(dt_seconds, 0.1);
    if(dt <= 0.f) return;

    // Newly steered: the walk starts where the animal is, as it is
    std::vector<entt::entity> fresh;
    for(auto [entity, steer, transform] : reg.view<AnimalSteer, Transform>(entt::exclude<SteeredWalk, AnimalBotProxy>).each()) fresh.push_back(entity);
    for(auto entity : fresh) {
        const auto& transform = reg.get<Transform>(entity);
        auto& walk = reg.emplace<SteeredWalk>(entity);
        walk.walker.path = AnimalWalker::Path::Free;
        // At the speed its herd had it walking
        auto* herd = reg.try_get<HerdAnimal>(entity);
        walk.walker.speed = herd ? herd->speed : 0.f;
        walk.walker.yaw = headingOf(transform.rotation);
        walk.walker.height = transform.position.y;
        walk.walker.placed = true;
    }

    auto* library = reg.ctx().find<AnimalLibrary>();
    std::vector<entt::entity> halted;

    for(auto [entity, walk, transform] : reg.view<SteeredWalk, Transform>().each()) {

        const AnimalSteer* steer = reg.try_get<AnimalSteer>(entity);
        Animal* animal = reg.try_get<Animal>(entity);
        std::shared_ptr<const AnimalSpecies> species = animal && library ? library->species(animal->species) : nullptr;
        const float scale = std::max(transform.scale.y, 1e-3f);
        float length = scale, sole = 0.f;
        if(species) {
            length = std::max(library->entries[animal->species].length, 0.1f) * scale;
            const bool grounded = std::abs(species->min.y) < 0.2f * (species->max.y - species->min.y);
            sole = grounded ? species->min.y * scale : 0.f;
        }

        // The speed follows the one asked like a damped spring: its change has no jump either, whoever sits on the
        // animal is not thrown
        AnimalWalker& walker = walk.walker;
        const float wanted = steer ? std::max(steer->speed, 0.f) : 0.f;
        walk.acceleration += (response * response * (wanted - walker.speed) - 2.f * response * walk.acceleration) * dt;
        walker.speed = std::max(walker.speed + walk.acceleration * dt, 0.f);
        if(walker.speed == 0.f && wanted == 0.f) walk.acceleration = 0.f;

        // Toward the heading asked, no faster than its size lets it turn, and not on the spot
        float rate = 0.f;
        if(steer && wanted > 0.f) {
            const float limit = std::clamp(turn / std::max(length, 0.3f), 0.6f, 4.f);
            rate = std::clamp(2.5f * wrap(steer->yaw - walker.yaw), -limit, limit);
        }
        walk.yaw_rate += (rate - walk.yaw_rate) * std::min(6.f * dt, 1.f);
        const float moving = std::max(walker.speed, 0.05f);
        walker.turn = walker.speed > 0.02f ? walk.yaw_rate / moving : 0.f;

        stepWalker(walker, transform, length, sole, ground.get(), dt);

        if(species) {
            const int walk_clip = species->walk, run_clip = species->run >= 0 ? species->run : species->trot;
            const float walk_speed = walk_clip >= 0 ? std::max(species->clips[walk_clip].speed * scale, 0.05f) : 1.f;
            // The gait asked with the speed: its run once it is halfway to the run's speed, its walk again once it is
            // back near the last walk asked of it
            if(steer && wanted > 0.f && !steer->run) walk.walk_speed = wanted;
            if(steer && steer->run && wanted > 0.f && walker.speed > 0.5f * wanted) walk.running = true;
            if((!steer || !steer->run) && walker.speed < 1.25f * std::max(walk.walk_speed, walk_speed)) walk.running = false;
            int clip = species->idle;
            if(walker.speed > 0.1f * walk_speed && walk_clip >= 0) clip = walk_clip;
            if(walk.running && run_clip >= 0 && clip == walk_clip) clip = run_clip;
            if(clip >= 0) {
                animal->play(clip, 0.35f);
                // The procedural walk sets the pace of its steps itself
                if(clip == species->idle) animal->rate = 1.f;
                else if(!reg.all_of<AnimalGait>(entity)) animal->rate = std::clamp(walker.speed / std::max(species->clips[clip].speed * scale, 1e-3f), 0.5f, 1.8f);
            }
        }

        if(!steer && walker.speed < 0.02f) halted.push_back(entity);

    }

    for(auto entity : halted) reg.remove<SteeredWalk>(entity);

}



// AnimalBotSteer

AnimalBotSteerSys::AnimalBotSteerSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    wander = params.value("free", std::string("stand")) == "wander";
}

void AnimalBotSteerSys::preinit() {
    reg.storage<AnimalSteer>();
    reg.storage<AnimalBotProxy>();
    reg.storage<Transform>();
}

void AnimalBotSteerSys::tick(double dt) {

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds) return;
    proxies.resize(worlds->size(), entt::null);
    skeletons.resize(worlds->size());
    std::vector<glm::mat4> joints;
    const float never = std::numeric_limits<float>::max();

    for(size_t w = 0; w < worlds->size(); w++) {

        auto* bot = (*worlds)[w].reg.ctx().find<AnimalBot>();
        if(!bot || !bot->built || bot->kinematic) continue;
        const AnimalBody& body = *bot->body;
        const GaitRanges& ranges = bot->ranges;
        const bool runs = ranges.run > 0.f && body.run_speed > 0.f;
        const float slowest = ranges.speed[0] * body.walk_speed;
        const float fastest = runs ? ranges.run_speed[1] * body.run_speed : ranges.speed[1] * body.walk_speed;

        if(proxies[w] == entt::null || !reg.valid(proxies[w])) {
            proxies[w] = reg.create();
            reg.emplace<Transform>(proxies[w]);
            auto& proxy = reg.emplace<AnimalBotProxy>(proxies[w]);
            proxy.world = (int) w;
            proxy.species = animalLibrary(reg).find(body.species);
            proxy.walk_speed = body.walk_speed;
            proxy.top_speed = fastest;
            // With its skeleton when the library has the species: what rides on a joint rides on the ragdoll
            Skeleton& s = skeletons[w];
            s = Skeleton();
            s.species = proxy.species >= 0 ? animalLibrary(reg).species((uint32_t) proxy.species) : nullptr;
            if(s.species) {
                s.bind = animalBodyBind(body, *s.species);
                reg.emplace<AnimalPose>(proxies[w]);
            }
        }

        // The animal's model space where its trunk is, upright, heading as the trunk does
        const SegmentDef& trunk = body.def.segments[body.root];
        auto& transform = reg.get<Transform>(proxies[w]);
        transform.rotation = glm::angleAxis(headingOf(bot->rotation[body.root] * glm::inverse(trunk.spawn_rotation)), glm::vec3(0.f, 1.f, 0.f));
        transform.position = bot->position[body.root] - transform.rotation * trunk.spawn_position;

        // ... and its joints in it, from the segments
        if(const Skeleton& s = skeletons[w]; s.species) {
            auto& pose = reg.get<AnimalPose>(proxies[w]);
            skeletonFromSegments(body, s.bind, *s.species, bot->position, bot->rotation, joints);
            const glm::mat4 back = glm::inverse(transform.matrix());
            pose.world.resize(joints.size());
            for(size_t j = 0; j < joints.size(); j++) pose.world[j] = back * joints[j];
        }

        // The command its task and its observation read
        WalkCommand& command = bot->command;
        const AnimalSteer* steer = reg.try_get<AnimalSteer>(proxies[w]);
        if(steer || !wander) {
            float speed = steer ? std::clamp(steer->speed, 0.f, fastest) : 0.f;
            // No order between standing and the slowest walk was trained
            if(speed > 0.05f) speed = std::max(speed, slowest);
            else speed = 0.f;
            float curve = 0.f;
            if(steer && speed > 0.f) {
                const bool running = runs && speed > 0.5f * (ranges.speed[1] * body.walk_speed + ranges.run_speed[0] * body.run_speed);
                const float tightest = 1.f / (std::max(running ? ranges.run_radius[0] : ranges.radius[0], 0.5f) * std::max(body.length, 0.1f));
                // Turned to within a second or so, as tight as its turns were trained
                curve = std::clamp(1.5f * wrap(steer->yaw - command.yaw) / speed, -tightest, tightest);
            }
            command.wanted_speed = speed;
            command.wanted_turn = curve;
            command.until = never;
        } else if(command.until == never) {
            // Back to the draws of its task
            command.until = command.time;
        }

    }

}

void AnimalBotSteerSys::finish() {
    for(auto entity : proxies) {
        if(entity != entt::null && reg.valid(entity)) reg.destroy(entity);
    }
    proxies.clear();
}



void dy::registerAnimalSteer() {
    registerSystem("SteeredWalk", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<SteeredWalkSys>(reg, params); }, SystemScope::Host });
    registerSystem("AnimalBotSteer", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<AnimalBotSteerSys>(reg, params); }, SystemScope::Host });
}
