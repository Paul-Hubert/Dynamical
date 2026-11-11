#ifndef DY_SKY_H
#define DY_SKY_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "logic/systems/system.h"

namespace dy {

// Context variable : the time of the day. The sun, the moon and the sky follow from it (docs/lighting.md).
// The world has its east along +X and its north along -Z.
//
// Without the Sky system of a scene the sun does not move : it stays where the shaders had it before
// (normalize(0.5, 1, 0.5), which is the middle of an August morning at this latitude).
struct Sky {
    static constexpr float default_time = 10.3349f;
    static constexpr float default_day_length = 1440.f;

    float time = default_time;      // hours, 0 to 24
    float day_length = 0.f;         // seconds of play for a day (1440 : an hour a minute), 0 : the sun is stopped
    float latitude = 42.8f;         // degrees (Tautavel)
    float declination = 14.787f;    // degrees : the season. 23.4 midsummer, 0 the equinoxes, -23.4 midwinter
};

// What lights the world at a time of the day : mirrored by include/sky.glsl
struct SkyLight {
    glm::vec3 sun = glm::vec3(0.f, 1.f, 0.f);       // towards the sun
    glm::vec3 moon = glm::vec3(0.f, -1.f, 0.f);     // towards the moon

    // The one directional light : the brighter of the sun and the moon. Its strength goes through zero when
    // one takes over from the other
    glm::vec3 direction = glm::vec3(0.f, 1.f, 0.f); // towards it
    glm::vec3 color = glm::vec3(0.f);               // its light on a surface facing it
    float radius = 0.00465f;                        // of its disc, radians
    bool is_sun = true;

    // The sky : skyColor()
    glm::vec3 zenith = glm::vec3(0.f);
    glm::vec3 horizon = glm::vec3(0.f);
    glm::vec3 glow = glm::vec3(0.f);                // around the sun
    float glow_power = 64.f;
    float night = 0.f;                              // 0 the day, 1 the night
    float sun_disc = 0.f, moon_disc = 0.f;          // how bright the discs are drawn

    // Without probes : the light of the sky on a surface facing up, and what the ground sends back up
    glm::vec3 ambient_sky = glm::vec3(0.f);
    glm::vec3 ambient_ground = glm::vec3(0.f);
};

// How much of the colour of the sky lights the world (skyLight() of the shaders is skyColor() times this,
// half way to grey)
constexpr float sky_light_scale = 0.75f;

glm::vec3 sunDirection(const Sky& sky);
glm::vec3 moonDirection(const Sky& sky);
SkyLight skyLight(const Sky& sky);

// Colour of the sky in a direction, without the discs of the sun and of the moon
glm::vec3 skyColor(const SkyLight& light, glm::vec3 direction);
// What a ray that misses everything brings back
glm::vec3 skyRadiance(const SkyLight& light, glm::vec3 direction);

// "Sky" : sets the Sky of the scene and moves its time. Params : time (hours), day_length (seconds, 0 stops
// the sun; left out or negative : 1440, and 0 in the runs with a fixed frame clock, captures and probes),
// latitude, declination (degrees)
class SkySys : public System {
public:
    SkySys(entt::registry& reg, const nlohmann::json& params);
    void tick(double dt) override;
    const char* name() override { return "Sky"; }
};

// "Torch" : a point light of the scene. Params : color, intensity, radius (PointLight); carried (true : it stays
// `offset` from the desktop camera, x to its right, y up, z ahead; false : it is left at `position`)
class TorchSys : public System {
public:
    TorchSys(entt::registry& reg, const nlohmann::json& params);
    void init() override;
    void tick(double dt) override;
    void finish() override;
    const char* name() override { return "Torch"; }

private:
    nlohmann::json params;
    entt::entity torch = entt::null;
    bool carried = true;
    glm::vec3 offset = glm::vec3(0.35f, -0.25f, 0.8f);
};

void registerSky();

}

#endif
