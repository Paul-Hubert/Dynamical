#ifndef DY_LIGHTS_H
#define DY_LIGHTS_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

#include "renderer/scene_pass.h"

namespace dy {

class Context;

constexpr int max_point_lights = 8;

// Set 0, binding 3 of every scene pipeline : the sun or the moon, the sky and the point lights nearest to
// the viewers. One per frame, the same for both targets. `Lights` of include/scene.glsl
struct LightsUBO {
    glm::vec4 sun_direction;    // xyz : towards the light that casts shadows (the sun, the moon at night), w : radius of its disc, radians
    glm::vec4 sun_color;        // rgb : its light on a surface facing it, w : 1 the sun, 0 the moon
    glm::vec4 sky_zenith;       // rgb
    glm::vec4 sky_horizon;      // rgb, w : 0 the day, 1 the night
    glm::vec4 sky_glow;         // rgb : around the sun, w : how tight
    glm::vec4 sun_disc;         // xyz : towards the sun, w : how bright its disc is drawn
    glm::vec4 moon_disc;        // xyz : towards the moon, w : its disc
    glm::vec4 ambient_sky;      // rgb : light of the sky on a surface facing up, w : sky light for a sky colour
    glm::vec4 ambient_ground;   // rgb : light the ground sends back up, w : number of point lights
    glm::vec4 frame;            // x : frames drawn so far (seeds of the noises), y : seconds
    struct Point {
        glm::vec4 position;     // w : radius
        glm::vec4 color;        // rgb : light a metre away
    } points[max_point_lights];
};

// Fills the LightsUBO of the frame from the Sky of the registry and its PointLight entities
class Lights {
public:
    Lights(Context& ctx, entt::registry& reg);
    ~Lights();

    // Main thread, before the targets write their sets
    void update(uint32_t frame_index, float time);

    const LightsUBO& current() const { return last; }

private:
    Context& ctx;
    entt::registry& reg;

    struct PerFrame {
        VmaBuffer buffer;
        LightsUBO* pointer = nullptr;
    };
    std::vector<PerFrame> per_frame;
    LightsUBO last {};
    uint64_t frames = 0;
};

// ViewUBO::lighting of a target, from the RenderSettings
glm::vec4 lightingView(const entt::registry& reg, RenderTarget target);

}

#endif
