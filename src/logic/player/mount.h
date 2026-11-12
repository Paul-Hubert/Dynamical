#ifndef DY_MOUNT_H
#define DY_MOUNT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "logic/systems/system.h"

namespace dy {

class PhysicsWorld;

// Riding (docs/players.md). A player that touches an animal and presses jump gets on it, steers it with its walk
// axis along where it looks, and gets off with jump. One system per thing done, meeting through components:
//
//   Mountable       an animal that can be ridden: where the rider sits, what of it is bumped into     <- Mountables
//   Rider           a player that rides: gets on what it touches, gets off                           <- Mounting
//                   ... its walk axis and its view are the animal's heading and speed (AnimalSteer)  <- MountSteering
//                   ... its body is carried over the saddle                                           <- RiderSeat
//   AnimalSteer     (logic/animals/steer.h) what the animal is asked: it knows nothing of the rider   <- SteeredWalk,
//                                                                                                        AnimalBotSteer
//
// The animal is a kinematic one of the library (the horses of a herd, of MountSpawn) or the physical one of a world
// with its policy (AnimalBotProxy): the rider does not know which.

// On an animal: it can be ridden. Lengths are the animal's own (model space, before its Transform's scale)
struct Mountable {
    glm::vec3 saddle = glm::vec3(0.f, 1.f, 0.f);    // the top of its back where the rider sits, in the Transform's frame
    int joint = -1;                                 // the joint of its AnimalPose that carries the saddle, -1: the Transform does
    glm::vec3 joint_local = glm::vec3(0.f);         // the saddle in that joint's frame
    glm::vec3 centre = glm::vec3(0.f, 0.8f, 0.f);   // the box of its trunk, in the Transform's frame: what a player bumps into
    glm::vec3 half_extent = glm::vec3(0.3f, 0.3f, 0.8f);
    float walk_speed = 1.5f;                        // m/s a rider asks of it
    float run_speed = 5.f;                          // ... with the run button

    // Written by the systems
    entt::entity rider = entt::null;
    entt::entity collider = entt::null;             // the entity of its kinematic body, while a rider is around
};

// On the entity that holds the kinematic body of a Mountable
struct MountCollider {
    entt::entity mount = entt::null;
};

// On a player (with PlayerLegs, an input component and a PlayerView): it rides
struct Rider {
    float reach = 0.3f;                 // m: how near its body has to be to the animal to get on
    float seat = 0.9f;                  // m from the saddle up to its eyes
    float stiffness = 9.f;              // 1/s: how fast the body is brought over the saddle and kept there
    float max_acceleration = 40.f;      // m/s^2 the seat gives it at most
    float settle = 0.5f;                // seconds over which the seat's pull comes in: no jolt getting on
    float lost = 2.f;                   // m from the saddle: fallen off
    float hop = 1.8f;                   // m/s to the side as it gets off

    // Written by the systems
    entt::entity mount = entt::null;
    bool was_jump = false;              // the jump button as last seen
    float blend = 0.f;                  // 0 getting on, 1 seated
    glm::vec3 seat_position = glm::vec3(0.f);   // of the saddle, world, this frame
    glm::vec3 seat_velocity = glm::vec3(0.f);   // ... smoothed
    bool seat_known = false;
    entt::entity left = entt::null;     // the collider of the animal it got off: not collided with for a moment more
    float left_timer = 0.f;
};

// Where a rider's seat is: the saddle of a Mountable, world
glm::vec3 saddleOf(const entt::registry& reg, entt::entity mount);

// Animals of the species listed (Animal entities, AnimalBotProxy) get a Mountable, measured on the skin of the
// species: the top of the back between the fore and the hind legs, the joint that carries it, the box of the trunk.
// Mountables within `range` of a Rider get a kinematic body there (their collider entity), moved with them.
// Before RigidBody in the scene.
// params: { "species": ["horse"], "range": 40, "walk": 2, "run": 2.4 (the speeds a rider asks, in speeds of the
//           walk clip and of the run clip) }
class MountablesSys : public System {
public:
    MountablesSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "Mountables"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    struct Shape {
        bool ok = false;
        Mountable mountable;
    };
    const Shape& shape(uint32_t species);
    void mountableDestroyed(entt::registry& reg, entt::entity entity);

    std::vector<std::string> ids;
    float range = 40.f;
    float walk_factor = 2.f, run_factor = 2.4f;
    std::unordered_map<uint32_t, Shape> shapes;     // by species of the library
    std::vector<int> wanted;                        // per species: -1 not looked at yet, 0, 1
};

// Rider: the press of the jump button gets it on the Mountable its body touches (the legs go limp, the animal gets
// an AnimalSteer), or off the one it is on. Off too when the animal is gone or the seat is lost. Before PlayerLegs
class MountingSys : public System {
public:
    MountingSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Mounting"; }
    void preinit() override;
    void tick(double dt) override;

private:
    entt::entity touched(entt::entity player, const Rider& rider, uint32_t group);
    void mount(entt::entity player, Rider& rider, entt::entity animal);
    void dismount(entt::entity player, Rider& rider, bool hop);

    PhysicsWorld* world = nullptr;
};

// Rider on an animal: the walk axis, turned by where the player looks (the horizontal of its view), is the heading
// asked of the animal; its length the speed, the walk's or with the run button the run's. After the input systems
// and LookTurn, before SteeredWalk
class MountSteeringSys : public System {
public:
    MountSteeringSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "MountSteering"; }
    void tick(double dt) override;
};

// Rider on an animal: its body is held over the saddle by a force that asks for the saddle's place and velocity, comes
// in over Rider::settle and is capped: the body's acceleration has no jump, getting on or riding. After
// AnimalAnimation and AnimalGait (the saddle of this frame), before Physics
class RiderSeatSys : public System {
public:
    RiderSeatSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "RiderSeat"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    void step(float h);

    PhysicsWorld* world = nullptr;
};

// A few animals of the library standing in a row on the ground, to get on (they are steered by SteeredWalk once
// they have a rider). Needs AnimalAnimation after it in the scene, AnimalGait for the procedural walk.
// params: { "species": "horse", "count": 3, "position": [4, -6], "spacing": 3, "yaw": 90 }
class MountSpawnSys : public System {
public:
    MountSpawnSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "MountSpawn"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override {}

private:
    std::string species = "horse";
    int count = 3;
    glm::vec2 position = glm::vec2(4.f, -6.f);
    float spacing = 3.f;
    float yaw = 90.f;
};

// Mountables, Mounting, MountSteering, RiderSeat, MountSpawn
void registerMounts();

}

#endif
