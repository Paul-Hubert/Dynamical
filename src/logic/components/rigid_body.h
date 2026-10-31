#ifndef DY_RIGID_BODY_H
#define DY_RIGID_BODY_H

#include <glm/glm.hpp>

#include <cstdint>

namespace dy {

// A Jolt body driven by PhysicsSys. Needs a Transform.
//  - Dynamic : simulated, PhysicsSys writes the Transform (interpolated between fixed steps)
//  - Kinematic : follows the Transform written by gameplay code (VR hands), pushes dynamic bodies
//  - Static : never moves
class RigidBody {
public:
    enum class Motion : uint8_t { Static, Dynamic, Kinematic };
    enum class Shape : uint8_t { Box, Sphere, Capsule };

    Motion motion = Motion::Dynamic;
    Shape shape = Shape::Box;

    glm::vec3 half_extent = glm::vec3(0.5f);   // world units, scale included. Sphere radius is half_extent.x;
                                               // capsule radius half_extent.x, cylinder half-height half_extent.y (local Y)
    glm::vec3 offset = glm::vec3(0.f);         // shape center relative to the Transform position, in local space

    float friction = 0.6f;
    float restitution = 0.1f;

    glm::vec3 initial_velocity = glm::vec3(0.f);
    glm::vec3 initial_angular_velocity = glm::vec3(0.f);

    uint32_t body = 0xffffffffu; // JPH::BodyID, assigned by PhysicsSys

    bool created() const { return body != 0xffffffffu; }
};

// Flat ground (physics.md): a static box whose top face is at top_y, on the STATIC layer. Created by RigidBodySys
struct Ground {
    glm::vec3 size = glm::vec3(150.f, 10.f, 150.f);     // full extents
    float top_y = 0.f;
    float friction = 1.f;
    float restitution = 0.f;

    uint32_t body = 0xffffffffu;

    bool created() const { return body != 0xffffffffu; }
};

// Context variable, written by PhysicsSys
struct PhysicsStats {
    uint32_t bodies = 0;
    uint32_t active_bodies = 0;
    uint32_t terrain_cells = 0;
    uint32_t terrain_triangles = 0;
    int steps_last_frame = 0;
    float step_ms = 0.f;
    float colliders_ms = 0.f;
};

}

#endif
