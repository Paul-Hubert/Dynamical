#include "sky.h"

#include <algorithm>
#include <cmath>

#include "logic/components/camera.h"
#include "logic/components/point_light.h"
#include "logic/components/transform.h"
#include "scene/run_args.h"
#include "scene/system_registry.h"

using namespace dy;

namespace {

constexpr float pi = 3.14159265359f;

float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float luminance(glm::vec3 c) {
    return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

// A body of the sky at an hour angle (degrees, 0 at its highest, negative before) and a declination,
// seen from a latitude : east along +X, north along -Z
glm::vec3 celestial(float hour_angle, float declination, float latitude) {
    float h = glm::radians(hour_angle), d = glm::radians(declination), l = glm::radians(latitude);
    float east = -std::cos(d) * std::sin(h);
    float north = std::cos(l) * std::sin(d) - std::sin(l) * std::cos(d) * std::cos(h);
    float up = std::sin(l) * std::sin(d) + std::cos(l) * std::cos(d) * std::cos(h);
    return glm::normalize(glm::vec3(east, up, -north));
}

// Air a ray of the sun crosses, 1 from the zenith (Kasten and Young)
float airMass(float sin_elevation) {
    float elevation = glm::degrees(std::asin(std::clamp(sin_elevation, 0.f, 1.f)));
    return 1.f / (std::sin(glm::radians(elevation)) + 0.50572f * std::pow(elevation + 6.07995f, -1.6364f));
}

}

glm::vec3 dy::sunDirection(const Sky& sky) {
    return celestial((sky.time - 12.f) * 15.f, sky.declination, sky.latitude);
}

// A full moon : it rises as the sun sets
glm::vec3 dy::moonDirection(const Sky& sky) {
    return celestial((sky.time - 12.f) * 15.f + 165.f, -sky.declination, sky.latitude);
}

SkyLight dy::skyLight(const Sky& sky) {

    SkyLight light;
    light.sun = sunDirection(sky);
    light.moon = moonDirection(sky);

    float sun_up = light.sun.y, moon_up = light.moon.y;

    // The sun through the air : white high up, orange and dim on the horizon, gone a little under it
    const glm::vec3 sun_white(0.71f, 0.69f, 0.66f);
    const glm::vec3 air(0.05f, 0.10f, 0.22f);
    glm::vec3 sun_color = sun_white * glm::exp(-air * (airMass(sun_up) - 1.f)) * smooth(-0.035f, 0.03f, sun_up);

    // The moon lights once the sun is well under : the light goes through nothing between the two
    const glm::vec3 moon_white(0.09f, 0.11f, 0.16f);
    glm::vec3 moon_color = moon_white * smooth(-0.02f, 0.12f, moon_up) * (1.f - smooth(-0.14f, -0.035f, sun_up));

    light.is_sun = luminance(sun_color) >= luminance(moon_color);
    light.direction = light.is_sun ? light.sun : light.moon;
    light.color = light.is_sun ? sun_color : moon_color;

    float day = smooth(-0.14f, 0.1f, sun_up);
    float low = 1.f - smooth(0.f, 0.3f, sun_up);
    light.night = 1.f - day;

    const glm::vec3 zenith_day(0.18f, 0.36f, 0.72f), zenith_night(0.010f, 0.016f, 0.034f);
    const glm::vec3 horizon_day(0.45f, 0.6f, 0.8f), horizon_low(0.62f, 0.5f, 0.45f), horizon_night(0.02f, 0.028f, 0.05f);
    light.zenith = glm::mix(zenith_night, zenith_day, day);
    light.horizon = glm::mix(horizon_night, glm::mix(horizon_day, horizon_low, low), day);

    float seen = smooth(-0.14f, 0.f, sun_up);
    light.glow = glm::mix(glm::vec3(0.22f, 0.2f, 0.14f), glm::vec3(0.95f, 0.45f, 0.12f), low) * seen;
    light.glow_power = glm::mix(48.f, 12.f, low);
    light.sun_disc = smooth(-0.03f, 0.f, sun_up);
    light.moon_disc = smooth(-0.03f, 0.f, moon_up) * (0.25f + 0.75f * light.night);

    // The light of the whole sky on a surface facing up
    glm::vec3 sum(0.f);
    float weights = 0.f;
    const int rings = 12, sectors = 24;
    for(int i = 0; i < rings; i++) {
        float theta = (i + 0.5f) / rings * 0.5f * pi;   // from the zenith
        float weight = std::cos(theta) * std::sin(theta);
        for(int j = 0; j < sectors; j++) {
            float phi = (j + 0.5f) / sectors * 2.f * pi;
            glm::vec3 direction(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            sum += skyRadiance(light, direction) * weight;
            weights += weight;
        }
    }
    light.ambient_sky = sum / weights;

    const glm::vec3 ground(0.25f, 0.25f, 0.2f);
    light.ambient_ground = ground * (light.color * std::max(light.direction.y, 0.f) + light.ambient_sky);

    return light;

}

glm::vec3 dy::skyColor(const SkyLight& light, glm::vec3 direction) {
    float up = std::clamp(direction.y, 0.f, 1.f);
    glm::vec3 color = glm::mix(light.horizon, light.zenith, std::sqrt(up));
    float toward = std::max(glm::dot(direction, light.sun), 0.f);
    float low = (1.f - up) * (1.f - up);
    return color + light.glow * (std::pow(toward, light.glow_power) + 0.35f * low * toward * toward);
}

glm::vec3 dy::skyRadiance(const SkyLight& light, glm::vec3 direction) {
    glm::vec3 color = skyColor(light, direction);
    return glm::mix(color, glm::vec3(luminance(color)), 0.5f) * sky_light_scale;
}

SkySys::SkySys(entt::registry& reg, const nlohmann::json& params) : System(reg) {

    Sky sky;
    sky.time = params.value("time", sky.time);
    sky.latitude = params.value("latitude", sky.latitude);
    sky.declination = params.value("declination", sky.declination);

    // Captures and probes are compared with what they gave before : their sun does not move unless they ask
    float day_length = params.value("day_length", -1.f);
    if(day_length < 0.f) {
        auto* args = reg.ctx().find<RunArgs>();
        day_length = args && args->deterministic() ? 0.f : Sky::default_day_length;
    }
    sky.day_length = day_length;

    reg.ctx().insert_or_assign(sky);

}

void SkySys::tick(double dt) {
    auto& sky = reg.ctx().get<Sky>();
    if(sky.day_length > 0.f) {
        sky.time = std::fmod(sky.time + (float) (dt * 24. / sky.day_length), 24.f);
        if(sky.time < 0.f) sky.time += 24.f;
    }
}

TorchSys::TorchSys(entt::registry& reg, const nlohmann::json& params) : System(reg), params(params) {

}

void TorchSys::init() {

    PointLight light;
    if(auto it = params.find("color"); it != params.end() && it->is_array() && it->size() == 3) {
        light.color = glm::vec3(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>());
    }
    light.intensity = params.value("intensity", light.intensity);
    light.radius = params.value("radius", light.radius);

    carried = params.value("carried", true);
    glm::vec3 position(0.f);
    for(const char* key : { "offset", "position" }) {
        auto it = params.find(key);
        if(it == params.end() || !it->is_array() || it->size() != 3) continue;
        glm::vec3 value(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>());
        (std::string(key) == "offset" ? offset : position) = value;
    }

    torch = reg.create();
    reg.emplace<Transform>(torch, position);
    reg.emplace<PointLight>(torch, light);

}

void TorchSys::tick(double dt) {
    if(!carried || !reg.valid(torch)) return;
    auto* camera = reg.ctx().find<Camera>();
    if(!camera) return;
    glm::vec3 forward = camera->forward(), right = camera->right();
    reg.get<Transform>(torch).position = camera->position + right * offset.x + glm::cross(right, forward) * offset.y + forward * offset.z;
}

void TorchSys::finish() {
    if(reg.valid(torch)) reg.destroy(torch);
}

void dy::registerSky() {
    registerSystem("Sky", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<SkySys>(reg, params); },
        SystemScope::Host });
    registerSystem("Torch", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<TorchSys>(reg, params); },
        SystemScope::Host });
}
