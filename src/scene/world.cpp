#include "world.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "component_registry.h"
#include "system_registry.h"

#include "logic/components/rigid_body.h"

using namespace dy;

namespace {

uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

void hashBytes(uint64_t& h, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for(size_t i = 0; i < size; i++) {
        h ^= bytes[i];
        h *= 0x100000001b3ull;
    }
}

const char* const combine_names[] = { "geometric", "max", "min" };

}

double WorldRng::uniform() {
    // 53 random bits: the same value on every platform
    return (double) (engine() >> 11) * (1.0 / 9007199254740992.0);
}

double WorldRng::uniform(double a, double b) {
    return a + (b - a) * uniform();
}

double WorldRng::normal() {
    double u1 = std::max(uniform(), 1e-300);
    double u2 = uniform();
    return std::sqrt(-2. * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

glm::vec3 WorldRng::unitSphere() {
    double z = uniform(-1., 1.);
    double a = uniform(0., 6.283185307179586);
    double r = std::sqrt(1. - z * z);
    return glm::vec3((float) (r * std::cos(a)), (float) (r * std::sin(a)), (float) z);
}

uint64_t dy::worldSeed(uint64_t base_seed, uint32_t index) {
    return splitmix64(splitmix64(base_seed) ^ (0x632be59bd9b4e019ull * (index + 1)));
}

PhysicsConfig dy::physicsConfig(const WorldDef& def) {
    PhysicsConfig c;
    try {
        JsonFields f(def.physics);
        int max_bodies = (int) c.max_bodies, max_body_pairs = (int) c.max_body_pairs, max_contacts = (int) c.max_contacts;
        int temp_bytes = (int) c.temp_bytes;
        int velocity_steps = -1, position_steps = -1;
        float speculative_distance = -1.f, penetration_slop = -1.f;
        f.read("threads", c.threads);
        f.read("max_bodies", max_bodies);
        f.read("max_body_pairs", max_body_pairs);
        f.read("max_contacts", max_contacts);
        f.read("temp_bytes", temp_bytes);
        f.read("gravity", c.gravity);
        f.read("substeps", c.substeps);
        f.read("allow_sleeping", c.allow_sleeping);
        f.readEnum("friction_combine", c.friction, combine_names);
        f.readEnum("restitution_combine", c.restitution, combine_names);
        f.read("velocity_steps", velocity_steps);
        f.read("position_steps", position_steps);
        f.read("speculative_distance", speculative_distance);
        f.read("penetration_slop", penetration_slop);
        f.finish();
        c.max_bodies = (uint32_t) max_bodies;
        c.max_body_pairs = (uint32_t) max_body_pairs;
        c.max_contacts = (uint32_t) max_contacts;
        c.temp_bytes = (uint32_t) temp_bytes;
        if(velocity_steps >= 0) c.velocity_steps = velocity_steps;
        if(position_steps >= 0) c.position_steps = position_steps;
        if(speculative_distance >= 0.f) c.speculative_distance = speculative_distance;
        if(penetration_slop >= 0.f) c.penetration_slop = penetration_slop;
        if(c.threads < 0 || c.substeps < 1 || max_bodies < 1) throw ComponentError("", "threads >= 0, substeps >= 1, max_bodies >= 1");
    } catch(const ComponentError& e) {
        throw SceneError(def.file, def.path + "/physics" + e.path, e.what());
    }
    return c;
}

WorldClock dy::worldClock(const WorldDef& def) {
    WorldClock clock;
    try {
        JsonFields f(def.clock);
        int physics_hz = 60, control_hz = 0;
        f.read("physics_hz", physics_hz);
        f.read("control_hz", control_hz);
        f.finish();
        if(control_hz == 0) control_hz = physics_hz;
        if(physics_hz <= 0 || control_hz <= 0 || physics_hz % control_hz != 0) {
            throw ComponentError("", "physics_hz must be a positive multiple of control_hz");
        }
        clock.dt = 1. / physics_hz;
        clock.decision_period = (uint32_t) (physics_hz / control_hz);
    } catch(const ComponentError& e) {
        throw SceneError(def.file, def.path + "/clock" + e.path, e.what());
    }
    return clock;
}

World::World(uint32_t index, uint64_t seed, const WorldDef& def) : index(index), template_name(def.template_name), systems(reg), entities(def.entities) {

    reg.ctx().emplace<PhysicsWorld>(physicsConfig(def));
    reg.ctx().emplace<WorldClock>(worldClock(def));
    reg.ctx().emplace<WorldRng>(seed);
    reg.ctx().emplace<WorldInfo>(WorldInfo { index, seed, def.template_name });
    reg.ctx().emplace<Parked>();

    for(auto& e : def.fixed) {
        systems.add(SystemSet::List::Fixed, findSystem(e.name)->make(reg, e.params), e.phase, e.name);
    }

}

World::~World() {
    finish();
}

void World::preinit() {
    DY_ZONE_N("World preinit");
    systems.preinit();
}

void World::init() {

    DY_ZONE_N("World init");

    for(auto& e : entities) {
        entt::entity entity = reg.create();
        for(auto& [name, fields] : e.components.items()) {
            try {
                findComponent(name)->emplace(reg, entity, fields);
            } catch(const ComponentError& error) {
                throw SceneError(e.file, e.path + "/" + name + error.path, error.what());
            }
        }
    }

    systems.init();

}

void World::tick() {
    DY_ZONE_N("World tick");
    auto& clock = reg.ctx().get<WorldClock>();
    uint64_t k = clock.tick % clock.decision_period;
    systems.fixed_tick(clock.dt, k == 0, k == clock.decision_period - 1);
    clock.tick++;
}

void World::finish() {
    if(finished) return;
    finished = true;
    systems.finish();
}

uint64_t World::hash() const {

    uint64_t h = 0xcbf29ce484222325ull;
    auto* world = reg.ctx().find<PhysicsWorld>();
    if(!world) return h;
    auto& bodies = const_cast<PhysicsWorld*>(world)->bodies();

    std::vector<entt::entity> list;
    for(auto entity : reg.view<RigidBody>()) list.push_back(entity);
    std::sort(list.begin(), list.end());

    for(auto entity : list) {
        auto& body = reg.get<RigidBody>(entity);
        if(!body.created()) continue;
        JPH::RVec3 p;
        JPH::Quat q;
        bodies.GetPositionAndRotation(JPH::BodyID(body.body), p, q);
        float values[7] = { (float) p.GetX(), (float) p.GetY(), (float) p.GetZ(), q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
        hashBytes(h, values, sizeof(values));
    }
    return h;

}

void Worlds::create(const SceneDef& scene, uint64_t base_seed) {
    uint32_t index = (uint32_t) list.size();
    for(auto& def : scene.worlds) {
        for(int i = 0; i < def.count; i++, index++) {
            list.push_back(std::make_unique<World>(index, worldSeed(base_seed, index), def));
        }
    }
}

void Worlds::preinit() {
    for(auto& w : list) w->preinit();
}

void Worlds::init() {
    for(auto& w : list) w->init();
}

void Worlds::finish() {
    for(auto& w : list) w->finish();
}

void Worlds::step(int64_t ticks, tf::Executor* executor) {

    DY_ZONE();

    auto run = [ticks](std::unique_ptr<World>& w) {
        for(int64_t t = 0; t < ticks; t++) w->tick();
    };

    if(!executor || list.size() < 2) {
        for(auto& w : list) run(w);
        return;
    }

    tf::Taskflow flow;
    flow.for_each(list.begin(), list.end(), run);
    executor->run(flow).wait();

}
