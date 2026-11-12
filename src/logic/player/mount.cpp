#include "player_systems.h"

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include "logic/agents/ground_field.h"
#include "logic/animals/animal.h"
#include "logic/animals/gait.h"
#include "logic/animals/steer.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "physics/physics_sys.h"
#include "scene/system_registry.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace dy;

namespace {

class NotOfPlayer : public JPH::BodyFilter {
public:
    NotOfPlayer(const JPH::GroupFilter* filter, uint32_t group) : filter(filter), group(group) {}
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        if(body.IsSensor()) return false;
        const JPH::CollisionGroup& g = body.GetCollisionGroup();
        return !(g.GetGroupFilter() == filter && g.GetGroupID() == group);
    }

private:
    const JPH::GroupFilter* filter;
    uint32_t group;
};

float smooth(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}

glm::vec3 dy::saddleOf(const entt::registry& reg, entt::entity mount) {
    const auto& mountable = reg.get<Mountable>(mount);
    const auto& transform = reg.get<Transform>(mount);
    if(mountable.joint >= 0) {
        auto* pose = reg.try_get<AnimalPose>(mount);
        if(pose && mountable.joint < (int) pose->world.size()) {
            return glm::vec3(transform.matrix() * pose->world[mountable.joint] * glm::vec4(mountable.joint_local, 1.f));
        }
    }
    return transform.position + transform.rotation * (mountable.saddle * transform.scale);
}



// Mountables

MountablesSys::MountablesSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    if(auto it = params.find("species"); it != params.end() && it->is_array()) {
        for(auto& id : *it) ids.push_back(id.get<std::string>());
    } else if(it != params.end() && it->is_string()) {
        ids.push_back(it->get<std::string>());
    } else {
        ids.push_back("horse");
    }
    range = params.value("range", range);
    walk_factor = params.value("walk", walk_factor);
    run_factor = params.value("run", run_factor);
}

void MountablesSys::preinit() {
    reg.storage<Mountable>();
    reg.storage<MountCollider>();
    reg.storage<Rider>();
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<AnimalBotProxy>();
    reg.storage<AnimalSkin>();
    reg.storage<Transform>();
    reg.on_destroy<Mountable>().connect<&MountablesSys::mountableDestroyed>(this);
}

void MountablesSys::mountableDestroyed(entt::registry& reg, entt::entity entity) {
    entt::entity collider = reg.get<Mountable>(entity).collider;
    if(collider != entt::null && reg.valid(collider)) reg.destroy(collider);
}

const MountablesSys::Shape& MountablesSys::shape(uint32_t index) {

    if(auto it = shapes.find(index); it != shapes.end()) return it->second;
    Shape& out = shapes[index];

    auto& library = animalLibrary(reg);
    auto species = library.species(index);
    if(!species) return out;
    const auto& entry = library.entries[index];

    std::vector<AnimalVertex> vertices;
    try {
        std::vector<uint32_t> indices;
        vertices = AnimalLibrary::loadGeometry(library.directory, *species, indices);
    } catch(const std::exception& e) {
        dy::log(dy::warning) << "Mountables : " << entry.id << " : " << e.what() << "\n";
        return out;
    }

    const float height = std::max(entry.height, 0.1f), length = std::max(entry.length, 0.1f), width = std::max(entry.width, 0.05f);

    // Between the hips of the hind legs and of the fore legs, as its walk shows them
    const float middle = 0.5f * (species->min.z + species->max.z);
    float hind = middle + 0.2f * length, fore = middle - 0.2f * length;
    if(species->walk >= 0) {
        GaitModel model = analyseGait(*species, species->walk);
        if(model.valid && std::abs(model.hind - model.fore) > 0.1f * length) {
            hind = model.hind;
            fore = model.fore;
        }
    }

    // The rider sits on the skin of the back, nearer the fore legs
    Mountable& m = out.mountable;
    const float z = glm::mix(hind, fore, 0.6f), band = std::max(0.06f * length, 0.04f);
    float top = -1e9f;
    for(const auto& v : vertices) {
        if(std::abs(v.position.z - z) < band && std::abs(v.position.x) < 0.3f * width) top = std::max(top, v.position.y);
    }
    if(top < 0.2f * height) top = 0.75f * height;
    m.saddle = glm::vec3(0.f, top, z);

    // ... carried by the joint that moves that skin, and the trunk is as wide as the skin is there
    std::vector<float> weights(species->parents.size(), 0.f);
    float half_width = 0.f;
    for(const auto& v : vertices) {
        if(std::abs(v.position.z - z) >= band) continue;
        if(v.position.y > 0.5f * top) half_width = std::max(half_width, std::abs(v.position.x));
        if(v.position.y > 0.8f * top && std::abs(v.position.x) < 0.3f * width) {
            for(int k = 0; k < 4; k++) weights[v.joints[k]] += (float) v.weights[k];
        }
    }
    auto best = std::max_element(weights.begin(), weights.end());
    if(best != weights.end() && *best > 0.f) {
        m.joint = (int) (best - weights.begin());
        m.joint_local = glm::vec3(species->inverse_bind[m.joint] * glm::vec4(m.saddle, 1.f));
    }

    half_width = std::clamp(half_width, 0.08f, 0.6f * width);
    const float back = std::max(hind, fore) + 0.1f * length, front = std::min(hind, fore) - 0.1f * length;
    m.centre = glm::vec3(0.f, 0.74f * top, 0.5f * (back + front));
    m.half_extent = glm::vec3(half_width, 0.24f * top, 0.5f * (back - front));

    // Faster than its clips: the procedural walk takes quicker and longer steps (a clip with a moment off the ground
    // says little of its speed: what its planted feet cover)
    m.walk_speed = walk_factor * (species->walk >= 0 && species->clips[species->walk].speed > 0.05f ? species->clips[species->walk].speed : 0.6f * length);
    const int run = species->run >= 0 ? species->run : species->trot;
    m.run_speed = std::max(run_factor * (run >= 0 ? species->clips[run].speed : 0.f), 1.6f * m.walk_speed);

    out.ok = true;
    dy::log(dy::info) << "Mountables : " << entry.id << ", saddle " << top << " m up" << (m.joint >= 0 ? " on " + species->joint_names[m.joint] : std::string())
        << ", walks " << m.walk_speed << " m/s, runs " << m.run_speed << " m/s\n";
    return out;

}

void MountablesSys::tick(double dt) {

    auto& library = animalLibrary(reg);
    if(wanted.size() != library.entries.size()) wanted.assign(library.entries.size(), -1);
    auto want = [&](uint32_t species) {
        if(species >= wanted.size()) return false;
        if(wanted[species] < 0) wanted[species] = std::find(ids.begin(), ids.end(), library.entries[species].id) != ids.end() ? 1 : 0;
        return wanted[species] == 1;
    };

    // Animals of the species asked for can be ridden
    std::vector<std::pair<entt::entity, Mountable>> fresh;
    for(auto [entity, animal] : reg.view<Animal>(entt::exclude<Mountable, AnimalSkin>).each()) {
        if(!want(animal.species)) continue;
        const Shape& s = shape(animal.species);
        if(s.ok) fresh.push_back({ entity, s.mountable });
        else wanted[animal.species] = 0;
    }
    for(auto [entity, proxy] : reg.view<AnimalBotProxy>(entt::exclude<Mountable>).each()) {
        if(proxy.species < 0 || !want((uint32_t) proxy.species)) continue;
        const Shape& s = shape((uint32_t) proxy.species);
        if(!s.ok) {
            wanted[proxy.species] = 0;
            continue;
        }
        // The speeds its policy knows; the saddle rides on the joint when the proxy has its skeleton
        Mountable m = s.mountable;
        if(!reg.all_of<AnimalPose>(entity)) m.joint = -1;
        m.walk_speed = proxy.walk_speed;
        m.run_speed = std::max(proxy.top_speed, proxy.walk_speed);
        fresh.push_back({ entity, m });
    }
    for(auto& [entity, mountable] : fresh) reg.emplace<Mountable>(entity, mountable);

    // A body to bump into, while a rider is around
    std::vector<glm::vec3> riders;
    for(auto [entity, rider, transform] : reg.view<Rider, Transform>().each()) riders.push_back(transform.position);

    std::vector<entt::entity> make, drop;
    for(auto [entity, mountable, transform] : reg.view<Mountable, Transform>().each()) {
        bool near = mountable.rider != entt::null;
        for(auto& p : riders) near = near || glm::distance(p, transform.position) < range;
        if(mountable.collider != entt::null && !reg.valid(mountable.collider)) mountable.collider = entt::null;
        if(near && mountable.collider == entt::null) make.push_back(entity);
        if(!near && mountable.collider != entt::null) drop.push_back(entity);
    }
    for(auto entity : drop) {
        auto& mountable = reg.get<Mountable>(entity);
        reg.destroy(mountable.collider);
        mountable.collider = entt::null;
    }
    for(auto entity : make) {
        const float scale = reg.get<Transform>(entity).scale.y;
        entt::entity collider = reg.create();
        reg.emplace<Transform>(collider);
        reg.emplace<MountCollider>(collider).mount = entity;
        reg.emplace<Ungrabbable>(collider);
        auto& body = reg.emplace<RigidBody>(collider);
        body.motion = RigidBody::Motion::Kinematic;
        body.half_extent = reg.get<Mountable>(entity).half_extent * scale;
        body.friction = 0.4f;
        body.restitution = 0.f;
        reg.get<Mountable>(entity).collider = collider;
    }
    for(auto [entity, mountable, transform] : reg.view<Mountable, Transform>().each()) {
        if(mountable.collider == entt::null) continue;
        auto& at = reg.get<Transform>(mountable.collider);
        at.position = transform.position + transform.rotation * (mountable.centre * transform.scale);
        at.rotation = transform.rotation;
    }

}

void MountablesSys::finish() {
    reg.on_destroy<Mountable>().disconnect(this);
}



// Mounting

void MountingSys::preinit() {
    world = &physicsWorld(reg);
    reg.storage<Mountable>();
    reg.storage<MountCollider>();
    reg.storage<AnimalSteer>();
}

entt::entity MountingSys::touched(entt::entity player, const Rider& rider, uint32_t group) {

    JPH::BodyID id = playerBodyId(reg, player);
    if(id.IsInvalid()) return entt::null;
    const JPH::Body* body = world->system().GetBodyLockInterfaceNoLock().TryGetBody(id);
    if(!body) return entt::null;

    // What the body touches (the capsule is the chest), and what the legs under it would
    JPH::CollideShapeSettings settings;
    settings.mMaxSeparationDistance = rider.reach;
    NotOfPlayer filter(playerCollision(reg).filter, group);
    for(float lower : { 0.f, -0.6f }) {
        JPH::RMat44 at = body->GetCenterOfMassTransform();
        at.SetTranslation(at.GetTranslation() + JPH::Vec3(0.f, lower, 0.f));
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        world->system().GetNarrowPhaseQueryNoLock().CollideShape(body->GetShape(), JPH::Vec3::sReplicate(1.f), at, settings, at.GetTranslation(),
            collector, {}, {}, filter);
        collector.Sort();
        for(const auto& hit : collector.mHits) {
            entt::entity other = bodyEntity(reg, *world, hit.mBodyID2);
            if(other == entt::null) continue;
            auto* collider = reg.try_get<MountCollider>(other);
            if(!collider || !reg.valid(collider->mount)) continue;
            auto* mountable = reg.try_get<Mountable>(collider->mount);
            if(mountable && mountable->rider == entt::null) return collider->mount;
        }
    }
    return entt::null;

}

void MountingSys::mount(entt::entity player, Rider& rider, entt::entity animal) {

    auto& mountable = reg.get<Mountable>(animal);
    auto& legs = reg.get<PlayerLegs>(player);
    rider.mount = animal;
    rider.blend = 0.f;
    rider.seat_known = false;
    mountable.rider = player;
    legs.limp = true;
    legs.jump_locked = true;

    // The animal is the rider's to steer, standing as it is until asked
    auto& steer = reg.emplace_or_replace<AnimalSteer>(animal);
    steer.yaw = headingOf(reg.get<Transform>(animal).rotation);
    steer.speed = 0.f;

    // The rider's body goes through the animal's
    JPH::BodyID collider = playerBodyId(reg, mountable.collider);
    if(!collider.IsInvalid() && legs.group != 0) setPlayerGroup(reg, *world, collider, legs.group, PlayerGroups::mount);
    if(rider.left == mountable.collider) rider.left = entt::null;

}

void MountingSys::dismount(entt::entity player, Rider& rider, bool hop) {

    entt::entity animal = rider.mount;
    rider.mount = entt::null;
    auto& legs = reg.get<PlayerLegs>(player);
    legs.limp = false;
    legs.jump_locked = true;

    glm::quat facing(1.f, 0.f, 0.f, 0.f);
    if(animal != entt::null && reg.valid(animal)) {
        reg.remove<AnimalSteer>(animal);
        if(auto* mountable = reg.try_get<Mountable>(animal)) {
            mountable->rider = entt::null;
            // Clear of it before they collide again
            rider.left = mountable->collider;
            rider.left_timer = 0.6f;
        }
        if(auto* transform = reg.try_get<Transform>(animal)) facing = glm::angleAxis(headingOf(transform->rotation), glm::vec3(0.f, 1.f, 0.f));
    }

    // Off to its left, with the speed it had
    JPH::BodyID id = playerBodyId(reg, player);
    if(hop && !id.IsInvalid()) {
        glm::vec3 velocity = rider.seat_velocity + facing * glm::vec3(-rider.hop, 1.f, 0.f);
        world->bodies().SetLinearVelocity(id, toJolt(velocity));
    }

}

void MountingSys::tick(double dt) {

    for(auto [entity, rider, legs, transform] : reg.view<Rider, PlayerLegs, Transform>().each()) {

        const bool jump = playerInput(reg, entity).getJumpButton();
        const bool pressed = jump && !rider.was_jump;
        rider.was_jump = jump;

        // The animal it got off is something to bump into again
        if(rider.left != entt::null) {
            rider.left_timer -= (float) dt;
            if(rider.left_timer <= 0.f) {
                JPH::BodyID collider = reg.valid(rider.left) ? playerBodyId(reg, rider.left) : JPH::BodyID();
                if(!collider.IsInvalid()) clearPlayerGroup(*world, collider);
                rider.left = entt::null;
            }
        }

        if(rider.mount != entt::null) {
            const bool gone = !reg.valid(rider.mount) || !reg.all_of<Mountable, Transform>(rider.mount);
            // Thrown off, held back by something, or the animal was put somewhere else
            const bool lost = !gone && rider.blend >= 1.f
                && glm::distance(transform.position, saddleOf(reg, rider.mount) + glm::vec3(0.f, rider.seat, 0.f)) > rider.lost;
            if(gone || lost) dismount(entity, rider, false);
            else if(pressed) dismount(entity, rider, true);
        } else if(pressed) {
            entt::entity animal = touched(entity, rider, legs.group);
            if(animal != entt::null) mount(entity, rider, animal);
        }

    }

}



// MountSteering

void MountSteeringSys::tick(double dt) {

    for(auto [entity, rider, view] : reg.view<Rider, PlayerView>().each()) {

        if(rider.mount == entt::null || !reg.valid(rider.mount)) continue;
        auto* steer = reg.try_get<AnimalSteer>(rider.mount);
        auto* mountable = reg.try_get<Mountable>(rider.mount);
        auto* transform = reg.try_get<Transform>(rider.mount);
        if(!steer || !mountable || !transform) continue;

        const GenericPlayerInput& input = playerInput(reg, entity);
        const glm::vec2 move = input.getMovementAxis();
        const float amount = std::min(glm::length(move), 1.f);
        if(amount < 0.1f) {
            // The heading stays the last one asked
            steer->speed = 0.f;
            continue;
        }

        // As the legs walk: along the horizontal of the view
        const glm::vec3 forward(-std::sin(view.yaw), 0.f, -std::cos(view.yaw));
        const glm::vec3 right(std::cos(view.yaw), 0.f, -std::sin(view.yaw));
        const glm::vec3 direction = move.x * right + move.y * forward;
        steer->yaw = std::atan2(-direction.x, -direction.z);
        const float speed = input.getRunButton() ? std::max(mountable->run_speed, mountable->walk_speed) : mountable->walk_speed;
        steer->speed = speed * transform->scale.y * amount;
        steer->run = input.getRunButton();

    }

}



// RiderSeat

void RiderSeatSys::preinit() {
    world = &physicsWorld(reg);
    fixedSteps(reg).before.push_back({ this, [this](float h) { step(h); } });
}

void RiderSeatSys::tick(double dt) {

    for(auto [entity, rider] : reg.view<Rider>().each()) {
        if(rider.mount == entt::null || !reg.valid(rider.mount) || !reg.all_of<Mountable, Transform>(rider.mount)) {
            rider.seat_known = false;
            continue;
        }
        // The saddle and how it moves, without the jolts of a frame
        const glm::vec3 seat = saddleOf(reg, rider.mount);
        if(rider.seat_known && dt > 1e-5) {
            const glm::vec3 velocity = (seat - rider.seat_position) / (float) dt;
            if(glm::length(velocity) < 60.f) rider.seat_velocity += (velocity - rider.seat_velocity) * std::min((float) dt / 0.12f, 1.f);
        } else if(!rider.seat_known) {
            rider.seat_velocity = glm::vec3(0.f);
        }
        rider.seat_position = seat;
        rider.seat_known = true;
    }

}

void RiderSeatSys::step(float h) {

    auto& bodies = world->bodies();
    const glm::vec3 gravity = toGlm(world->system().GetGravity());

    for(auto [entity, rider] : reg.view<Rider>().each()) {

        if(rider.mount == entt::null || !rider.seat_known) continue;
        JPH::BodyID id = playerBodyId(reg, entity);
        if(id.IsInvalid()) continue;
        const float mass = bodyMass(*world, id);
        if(mass <= 0.f) continue;

        rider.blend = std::min(rider.blend + h / std::max(rider.settle, 1e-3f), 1.f);

        // The saddle carries the weight at once, as the legs did; its pull to its place and its speed comes in
        // smoothly and is capped
        const glm::vec3 eyes = toGlm(JPH::Vec3(bodies.GetPosition(id)));
        const glm::vec3 velocity = toGlm(bodies.GetLinearVelocity(id));
        const glm::vec3 target = rider.seat_position + glm::vec3(0.f, rider.seat, 0.f);
        const float k = rider.stiffness;
        glm::vec3 pull = (k * k * (target - eyes) + 2.f * k * (rider.seat_velocity - velocity)) * smooth(rider.blend);
        const float size = glm::length(pull);
        if(size > rider.max_acceleration) pull *= rider.max_acceleration / size;

        bodies.ActivateBody(id);
        bodies.AddForce(id, toJolt((pull - gravity) * mass));

    }

}

void RiderSeatSys::finish() {
    fixedSteps(reg).remove(this);
}



// MountSpawn

MountSpawnSys::MountSpawnSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    species = params.value("species", species);
    count = params.value("count", count);
    spacing = params.value("spacing", spacing);
    yaw = params.value("yaw", yaw);
    if(auto it = params.find("position"); it != params.end() && it->is_array() && it->size() == 2) {
        position = glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
    }
}

void MountSpawnSys::preinit() {
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<AnimalGait>();
    reg.storage<Transform>();
}

void MountSpawnSys::init() {

    auto& library = animalLibrary(reg);
    int index = library.find(species);
    auto of = index >= 0 ? library.species((uint32_t) index) : nullptr;
    if(!of) {
        dy::log(dy::warning) << "MountSpawn : no animal " << species << " in the library\n";
        return;
    }

    std::shared_ptr<GroundField> ground;
    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(generator) ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());

    const bool grounded = std::abs(of->min.y) < 0.2f * (of->max.y - of->min.y);
    for(int i = 0; i < count; i++) {
        glm::vec3 at(position.x + spacing * (float) i, 0.f, position.y);
        if(ground) {
            if(auto hit = ground->top(at.x, at.z)) at.y = hit->point.y;
        }
        if(grounded) at.y -= of->min.y;
        entt::entity entity = reg.create();
        reg.emplace<Transform>(entity, at, glm::angleAxis(glm::radians(yaw + 25.f * (float) (i % 3 - 1)), glm::vec3(0.f, 1.f, 0.f)));
        auto& animal = reg.emplace<Animal>(entity);
        animal.species = (uint32_t) index;
        animal.clip = of->idle >= 0 ? of->idle : of->walk;
        animal.time = 0.7f * (float) i;
        reg.emplace<AnimalGait>(entity);
    }

}



void dy::registerMounts() {
    registerSystem("Mountables", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<MountablesSys>(reg, params); }, SystemScope::Host });
    registerSystem("Mounting", systemInfo<MountingSys>(SystemScope::Host));
    registerSystem("MountSteering", systemInfo<MountSteeringSys>(SystemScope::Host));
    registerSystem("RiderSeat", systemInfo<RiderSeatSys>(SystemScope::Host));
    registerSystem("MountSpawn", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<MountSpawnSys>(reg, params); }, SystemScope::Host });
}
