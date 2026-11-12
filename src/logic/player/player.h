#ifndef DY_PLAYER_H
#define DY_PLAYER_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logic/components/vr.h"
#include "logic/player/generic_input.h"

namespace dy {

// Physical players (docs/players.md), after Unity Dynamical's GenericPlayer, FPSPlayer, VRPlayer, Hand, Grabber and
// Thrower. A player is an entity with a rigid body at its eyes; what it can do is the components it carries, each
// with one system. Systems only meet through these components:
//
//   *Input        what the player asks: MouseKeyboardInput, ControllersInput, VRPlayerInput,      <- their device systems
//                 ScriptedInput, read through playerInput() (generic_input.h)
//   PlayerView    where it looks: the walk follows its yaw                                        <- LookTurn, VRBody
//   PlayerLegs    stands the body over the ground, walks and jumps with forces                    <- PlayerLegs
//   VRTracking    head and hands of the headset, in stage space                                   <- VRPlayerInput
//   VRBody        the body under the head: follows it in the room, turns, holds through a menu    <- VRBody
//   Hand          a rigid body hand asked to follow a controller                                  <- VRHands
//   Grabber       a hand that locks onto what it touches while the grip is held, and throws       <- Grab
//   Holder        a body that picks up what it looks at and holds it in front of its eyes         <- Hold
//   BodyDrive     a rigid body driven to a pose relative to another by a force-limited motor      <- BodyDrives
//   DesktopView   the desktop camera is this player's eyes                                        <- PlayerViewpoint
//   Rider         gets on the animal it touches with a jump, steers it, sits on it (mount.h)      <- Mounting, MountSteering, RiderSeat
//
// A hand and a held object are the same thing to the physics: a body with a BodyDrive anchored on the player.

// Where the player looks, world space. yaw 0 looks toward -Z, as the Camera
struct PlayerView {
    float yaw = 0.f;
    float pitch = 0.f;

    glm::quat rotation() const {
        return glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f));
    }
    glm::vec3 forward() const { return rotation() * glm::vec3(0.f, 0.f, -1.f); }
};

// The view axis turns the view (Unity's LookTurn): a mouse by its motion, a stick at a rate
struct LookTurn {
    float stick_speed = 3.f;            // rad/s at full deflection
};

// The desktop camera sits at this player's eyes
struct DesktopView {};

// ... from behind and above them, this far back along the view (to see what the player rides)
struct ThirdPerson {
    float distance = 4.f;
    float height = 0.25f;               // of the distance, above the eyes
};

// Unity's GenericPlayer: the body floats at `height` over what is under it, held up by a force that asks for a
// vertical speed; walking asks for a horizontal one. Forces are Unity's: (gain x speed error) x mass / 10.
// Needs a dynamic RigidBody with lock_rotation whose Transform is the eyes, an input component and a PlayerView
struct PlayerLegs {
    float height = 1.8f;                // eyes over the ground, m (VRBody moves it with the head)
    float leg_force = 1000.f;           // cap of the stand and jump gains x speed error
    float stand_force = 1000.f;
    float stand_speed = 0.5f;           // m/s asked per half metre under the height (x2 x min(0.5, error))
    float jump_force = 500.f;
    float jump_speed = 5.f;             // m/s
    float walk_force = 500.f;
    float walk_speed = 2.f;             // m/s
    float sprint_speed = 4.f;
    float air_walk = 0.f;               // part of the walk force left off the ground
    float reach = 0.2f;                 // the ground is looked for this far under the feet

    bool limp = false;                  // no stand, walk or jump
    bool jump_locked = false;           // the press of the jump button did something else (got on an animal, got off
                                        // it): no jump until the button is let go

    // Written by the system
    bool standing = false;
    bool walking = false;
    float ground_distance = 0.f;        // eyes to the ground, when standing
    entt::entity ground = entt::null;   // the entity stood on, when it is one
    glm::vec3 ground_velocity = glm::vec3(0.f);
    glm::vec3 applied_force = glm::vec3(0.f);   // this step's leg forces: what is carried gets its share (hands, a held body)
    uint32_t group = 0;                 // collision group of the player and what it carries, given by the system
};

// A rigid body driven to a pose relative to another (Unity's ConfigurableJoint drives as the hand uses them: no
// spring, a damper so large that the drive is a velocity one). Each physics step the motor asks for
//   feed forward x max(1 - |error|, 0) + gain x error / step
// with a limited force, and the anchor gets the reaction. Needs a dynamic RigidBody; the target is where the
// body's centre of mass goes
struct BodyDrive {
    entt::entity anchor = entt::null;   // the body the drive pushes against; null: the world
    glm::vec3 position = glm::vec3(0.f);            // from the anchor's origin, in its frame
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 velocity = glm::vec3(0.f);            // feed forward, same frame, relative to the anchor
    glm::vec3 angular_velocity = glm::vec3(0.f);
    float gain = 0.607f;                // part of the position error closed in a step (Unity: exp(-50 / 100))
    float angular_gain = 0.183f;        // exp(-170 / 100)
    float max_force = 10000.f;          // N, per axis
    float max_torque = 10000.f;         // N m, per axis
    bool rotate = true;                 // false: the position alone is driven

    // Written by the system, world space
    glm::vec3 error = glm::vec3(0.f);   // target - centre of mass
    float angular_error = 0.f;          // rad
    bool driven = false;                // the motor exists
};

// Head and hands of a headset, in stage space (the play area). Written by VRPlayerInput from VRInput, or by a test
struct VRTracking {
    bool focused = false;               // a running, focused session: poses can be trusted
    VRPose head;

    struct Hand {
        VRPose pose;
        glm::vec3 velocity = glm::vec3(0.f);
        glm::vec3 angular_velocity = glm::vec3(0.f);
        float grip = 0.f;
        float trigger = 0.f;
    };
    Hand hands[2];
};

// Unity's VRPlayer: the body is at the head. It follows the head's motion in the room, the view axis turns the play
// area around it, the legs stand it at the head's height. While the session is not focused, and for the first two
// frames after, poses are held (PoseHold): a system menu never moves the body.
// Needs PlayerLegs, an input component, PlayerView and VRTracking
struct VRBody {
    float yaw = 0.f;                    // of the play area, rad
    float turn_speed = 3.14159f;        // rad/s at full deflection (Unity: 2 degrees a frame)
    float standing_height = 1.8f;       // the height the legs hold when the head is 1.8 m over the stage floor
    entt::entity hands[2] = { entt::null, entt::null };

    // Written by the system
    glm::vec3 head = glm::vec3(0.f);    // the last trusted head position, stage space
    bool tracked = false;               // head has been read once
    bool pose_hold = false;
    bool was_focused = true;
    bool seen_focus = false;
    int refocus_frame = 0;              // 0: not re-basing; 1, 2, 3: frames since the focus came back

    glm::quat rotation() const { return glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)); }
};

// Unity's Hand: a rigid body that follows a controller through a BodyDrive anchored on the player. More than
// `failsafe` away from where it is asked, it is put there. Needs a dynamic RigidBody and a BodyDrive
struct Hand {
    entt::entity player = entt::null;
    int side = VRInput::left;
    float failsafe = 1.f;               // m
    glm::vec3 rest = glm::vec3(0.f);    // where it waits before the controller is tracked, from the head, in the body's frame

    // The last trusted controller state, in the axes of the stage; the position from the head, so that a head
    // re-based after a menu does not move the hands
    bool tracked = false;
    glm::vec3 position = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 velocity = glm::vec3(0.f);
    glm::vec3 angular_velocity = glm::vec3(0.f);
    float grip = 0.f;
    float trigger = 0.f;
    int teleports = 0;                  // times the failsafe put it back
};

// Unity's Grabber and Thrower, on a hand: while the grip is held, what the hand touches is locked to it where they
// touch (a body, or the world: the player pulls itself). Let go, a body leaves with the controller's velocity
struct Grabber {
    float grip_threshold = 0.5f;
    float touch = 0.02f;                // m: how near counts as touching
    float velocity_multiplier = 1.f;
    float angular_velocity_multiplier = 1.f;
    float collision_delay = 0.1f;       // s before the hand and what it let go collide again

    // Written by the system
    bool grabbing = false;              // the grip is held
    bool grabbed = false;
    entt::entity object = entt::null;   // the entity held; null with `grabbed`: the world, or a body that is no entity
};

// On what a hand holds
struct Grabbed {
    entt::entity hands[2] = { entt::null, entt::null };
};

// Nothing grabs or picks up this entity
struct Ungrabbable {};

// The FPS player's hold: `interact` picks up the dynamic body looked at, which then follows a point in front of the
// eyes through a BodyDrive (it lags, collides, weighs on the player); `interact` again drops it, `primary` throws it,
// the wheel brings it nearer or further. Needs an input component and PlayerView
struct Holder {
    float reach = 3.f;                  // m, to pick up
    float distance = 1.2f;              // m in front of the eyes
    float min_distance = 0.6f;
    float max_distance = 2.5f;
    float wheel_step = 0.15f;           // m per notch
    float max_force = 1500.f;           // N per axis: what is too heavy stays on the ground
    float max_torque = 300.f;
    float throw_speed = 8.f;            // m/s
    float break_distance = 1.5f;        // m off its place: dropped

    // Written by the system
    entt::entity held = entt::null;
    glm::quat held_rotation = glm::quat(1.f, 0.f, 0.f, 0.f);    // of the body, in the view's frame
    bool was_interact = false, was_primary = false;             // the buttons as last seen: a press is a change
};

// On what a Holder holds
struct Held {
    entt::entity by = entt::null;
};

// A box to grab, made by PlayerSpawn. The viewer gives it a model (PlayerModels)
struct Prop {
    glm::vec4 color = glm::vec4(1.f);
};

// An FPS player with its eyes at `eyes`: a keyboard with its mouse and a gamepad, legs, a look, the desktop camera,
// a hold, and it rides (Rider, mount.h)
entt::entity makeFPSPlayer(entt::registry& reg, glm::vec3 eyes, float yaw = 0.f);

// A VR player with its head at `eyes` and two hands
entt::entity makeVRPlayer(entt::registry& reg, glm::vec3 eyes, float yaw = 0.f);

// The systems: PlayerSpawn, VRPlayerInput, LookTurn, VRBody, PlayerLegs, VRHands, Grab, Hold, BodyDrives,
// PlayerViewpoint, and those of mount.h (host; MouseKeyboardInput, ControllersInput and PlayerModels are the viewer's)
void registerPlayer();

}

#endif
