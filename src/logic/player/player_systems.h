#ifndef DY_PLAYER_SYSTEMS_H
#define DY_PLAYER_SYSTEMS_H

#include "logic/systems/system.h"
#include "physics/physics_world.h"
#include "mount.h"
#include "player.h"

#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>

#include <nlohmann/json.hpp>

#include <unordered_map>
#include <vector>

namespace dy {

// The systems of the physical players (docs/players.md). Host systems of the scene's pre list, in this order, all
// before RigidBody, TerrainColliders and Physics, PlayerViewpoint after them. What acts on the bodies does it in a
// FixedSteps step (Unity's FixedUpdate), the rest once a frame (Update).

// Bodies of a player do not collide with each other where Unity's joints and IgnoreCollision say so: one group per
// player, a sub group per role
namespace PlayerGroups {
    enum Sub : uint32_t { body, left_hand, right_hand, held_left, held_right, held_both, held_body, mount, count };
}

// Context variable: the group filter of every player
struct PlayerCollision {
    JPH::Ref<JPH::GroupFilterTable> filter;
    uint32_t next_group = 1;
};

// params: { "kind": "fps" | "vr", "position": [x, z], "yaw": 0, "props": 8, "ground": false, "third_person": 0 }
// One player on the ground at `position` (the terrain's height when the scene has a terrain), and `props` boxes of
// a hand's size and more in front of it. "ground": a flat Ground at y = 0, for a scene without a terrain
class PlayerSpawnSys : public System {
public:
    PlayerSpawnSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "PlayerSpawn"; }
    void preinit() override;
    void init() override;
    void tick(double dt) override {}

private:
    bool vr = false;
    glm::vec2 position = glm::vec2(0.f);
    float yaw = 0.f;
    int props = 8;
    bool flat = false;
    float third_person = 0.f;
};

// VRInput (the headset, written by the renderer) to the VRTracking and VRPlayerInput of every VRBody
class VRPlayerInputSys : public System {
public:
    VRPlayerInputSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "VRPlayerInput"; }
    void tick(double dt) override;

private:
    double time = 0.;
};

// The input's view axis to PlayerView, for players with a LookTurn
class LookTurnSys : public System {
public:
    LookTurnSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "LookTurn"; }
    void tick(double dt) override;
};

// VRBody: the body follows the head, the play area turns, poses are held without focus
class VRBodySys : public System {
public:
    VRBodySys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "VRBody"; }
    void preinit() override;
    void tick(double dt) override;

private:
    PhysicsWorld* world = nullptr;
};

// PlayerLegs: stand, walk, jump
class PlayerLegsSys : public System {
public:
    PlayerLegsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "PlayerLegs"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    void step(float h);

    PhysicsWorld* world = nullptr;
};

// Hand: controller poses to the hands' BodyDrive, the failsafe, the legs' forces carried to the hands
class VRHandsSys : public System {
public:
    VRHandsSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "VRHands"; }
    void preinit() override;
    void tick(double dt) override {}
    void finish() override;

private:
    void step(float h);

    PhysicsWorld* world = nullptr;
};

// Grabber: a fixed constraint between a hand and what it touches while the grip is held
class GrabSys : public System {
public:
    GrabSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Grab"; }
    void preinit() override;
    void tick(double dt) override {}
    void finish() override;

private:
    struct Grip {
        JPH::Ref<JPH::FixedConstraint> constraint;
        JPH::BodyID body;
        entt::entity object = entt::null;
        int teleports = 0;
    };
    struct Regroup {
        double time;
        entt::entity object;
    };

    void step(float h);
    void grab(entt::entity hand_entity, Hand& hand, Grabber& grabber);
    void release(entt::entity hand_entity, bool thrown);
    void regroup(entt::entity object);
    void bodyDestroyed(entt::registry& reg, entt::entity entity);

    PhysicsWorld* world = nullptr;
    std::unordered_map<entt::entity, Grip> grips;   // by hand
    std::vector<Regroup> regroups;
    double time = 0.;
};

// Holder: picks up, holds, drops and throws
class HoldSys : public System {
public:
    HoldSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "Hold"; }
    void preinit() override;
    void tick(double dt) override;
    void finish() override;

private:
    void step(float h);
    void pick(entt::entity player, Holder& holder);
    void drop(entt::entity player, Holder& holder, const glm::vec3* velocity);

    PhysicsWorld* world = nullptr;
};

// BodyDrive: a six degrees of freedom constraint with velocity motors between the anchor and the body
class BodyDrivesSys : public System {
public:
    BodyDrivesSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "BodyDrives"; }
    void preinit() override;
    void tick(double dt) override {}
    void finish() override;

private:
    struct Motor {
        JPH::Ref<JPH::SixDOFConstraint> constraint;
        entt::entity anchor = entt::null;
        bool rotate = true;
    };

    void step(float h);
    void drop(entt::entity entity);
    void driveDestroyed(entt::registry& reg, entt::entity entity);
    void bodyDestroyed(entt::registry& reg, entt::entity entity);

    PhysicsWorld* world = nullptr;
    std::unordered_map<entt::entity, Motor> motors;     // by driven entity
};

// After Physics: the desktop camera at the eyes of the DesktopView player, the play area (VRRig) under the VRBody
class PlayerViewpointSys : public System {
public:
    PlayerViewpointSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "PlayerViewpoint"; }
    void preinit() override;
    void tick(double dt) override;
};

// Shared by the systems

// The Jolt body of an entity's RigidBody, invalid when it has none yet
JPH::BodyID playerBodyId(const entt::registry& reg, entt::entity entity);
// The entity of a body made by RigidBodySys, null for any other
entt::entity bodyEntity(const entt::registry& reg, PhysicsWorld& world, JPH::BodyID id);
float bodyMass(PhysicsWorld& world, JPH::BodyID id);    // 0 when not dynamic
PlayerCollision& playerCollision(entt::registry& reg);
void setPlayerGroup(entt::registry& reg, PhysicsWorld& world, JPH::BodyID id, uint32_t group, PlayerGroups::Sub sub);
void clearPlayerGroup(PhysicsWorld& world, JPH::BodyID id);

}

#endif
