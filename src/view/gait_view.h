#ifndef DY_GAIT_VIEW_H
#define DY_GAIT_VIEW_H

#include "logic/systems/system.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <vector>

namespace dy {

// Host system of the ik_walk scenes (docs/physical-agents/ik-gait.md): a camera that follows one world's kinematic
// walker from where the ground does not hide its feet (the asked side first, then the other side, higher, behind), a panel to command it (speed, heading, the test plane's tilt) and read its checks, and markers: the
// test plane with a metre grid, the soles' prints (orange on the heel edge, green flat, blue on the toe edge, grey
// where the swing foot will land), the legs' IK targets, the centre of mass over its ground point (red), the
// extrapolated centre of mass (magenta) and the outline of the prints it must stay between. After WorldStepper.
// params: { "world": 0, "distance": 3.4, "height": 1.3, "azimuth": 75 (degrees from behind the walker), "camera": true,
//           "markers": true, "panel": true }
class GaitViewSys : public System {
public:
    GaitViewSys(entt::registry& reg, const nlohmann::json& params);
    const char* name() override { return "GaitView"; }
    void tick(double dt) override;
    void finish() override;

private:
    enum class Shape { Box, Sphere };
    void place(Shape shape, glm::vec3 position, glm::quat rotation, glm::vec3 scale, glm::vec4 color);
    void bar(glm::vec3 from, glm::vec3 to, float thickness, glm::vec4 color);

    int world = 0;
    float distance = 3.4f, height = 1.3f, azimuth = 75.f;
    bool camera = true, markers = true, panel = true;
    glm::vec3 focus = glm::vec3(0.f), eye = glm::vec3(0.f);
    int view_choice = 0;                        // which side and height the camera took to see the feet
    float view_heading = 0.f;
    bool has_focus = false;

    // Marker entities, reused in the order they are placed each frame
    std::vector<entt::entity> boxes, spheres;
    size_t used_boxes = 0, used_spheres = 0;
};

}

#endif
