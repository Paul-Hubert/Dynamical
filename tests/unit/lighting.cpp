#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "logic/sky/sky.h"

using namespace dy;

namespace {

float luminance(glm::vec3 c) {
    return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

}

TEST_SUITE("unit") {

TEST_CASE("LIGHT.1 the sun of a scene without a Sky system is where the shaders had it, it rises in the east and sets in the west") {

    // The constant every shader had before the sky
    Sky sky;
    CHECK(sky.day_length == 0.f);
    glm::vec3 before = glm::normalize(glm::vec3(0.5f, 1.f, 0.5f));
    CHECK(glm::distance(sunDirection(sky), before) < 2e-3f);

    // East is +X, north is -Z. In the northern summer : up in the north east, highest in the south, down in the north west
    float highest = -1.f, highest_time = 0.f;
    float rise = -1.f, set = -1.f;
    for(float time = 0.f; time < 24.f; time += 1.f / 60.f) {
        sky.time = time;
        glm::vec3 sun = sunDirection(sky);
        CHECK(std::abs(glm::length(sun) - 1.f) < 1e-4f);
        if(sun.y > highest) {
            highest = sun.y;
            highest_time = time;
        }
        if(sun.y > 0.f && rise < 0.f) rise = time;
        if(sun.y > 0.f) set = time;
    }
    CHECK(std::abs(highest_time - 12.f) < 0.05f);
    CHECK(std::abs(glm::degrees(std::asin(highest)) - (90.f - sky.latitude + sky.declination)) < 0.1f);
    CHECK(rise > 4.5f);
    CHECK(rise < 6.f);
    CHECK(std::abs((12.f - rise) - (set - 12.f)) < 0.05f);

    sky.time = rise + 0.2f;
    glm::vec3 morning = sunDirection(sky);
    CHECK(morning.x > 0.5f);
    CHECK(morning.z < 0.f);
    sky.time = 12.f;
    glm::vec3 noon = sunDirection(sky);
    CHECK(std::abs(noon.x) < 1e-3f);
    CHECK(noon.z > 0.f);
    sky.time = set - 0.2f;
    CHECK(sunDirection(sky).x < -0.5f);

}

TEST_CASE("LIGHT.2 one light at a time : white at noon, orange and dim on the horizon, the moon at night, nothing between the two") {

    Sky sky;

    sky.time = 12.f;
    SkyLight noon = skyLight(sky);
    CHECK(noon.is_sun);
    CHECK(glm::distance(noon.direction, noon.sun) < 1e-6f);
    CHECK(luminance(noon.color) > 0.6f);
    CHECK(noon.color.b / noon.color.r > 0.85f);
    CHECK(noon.night == 0.f);

    // Low sun : less light, and red before blue
    sky.time = 18.7f;
    SkyLight evening = skyLight(sky);
    REQUIRE(evening.sun.y > 0.f);
    REQUIRE(evening.sun.y < 0.12f);
    CHECK(evening.is_sun);
    CHECK(luminance(evening.color) < 0.6f * luminance(noon.color));
    CHECK(evening.color.b / evening.color.r < 0.5f);

    sky.time = 0.5f;
    SkyLight night = skyLight(sky);
    CHECK(!night.is_sun);
    CHECK(glm::distance(night.direction, night.moon) < 1e-6f);
    CHECK(night.direction.y > 0.f);
    CHECK(luminance(night.color) > 0.f);
    CHECK(luminance(night.color) < 0.15f * luminance(noon.color));
    CHECK(night.night == 1.f);
    CHECK(luminance(night.zenith) < 0.05f * luminance(noon.zenith));

    // The light never jumps : through a whole day its colour and its direction change little in a minute,
    // except the direction where there is next to no light (the sun hands over to the moon)
    float darkest = 1e9f;
    SkyLight last = skyLight(sky);
    for(int minute = 0; minute <= 24 * 60; minute++) {
        sky.time = std::fmod(0.5f + minute / 60.f, 24.f);
        SkyLight now = skyLight(sky);
        if(luminance(now.color) > 0.004f) CHECK(now.direction.y > -0.05f);
        CHECK(glm::distance(now.color, last.color) < 0.03f);
        CHECK(glm::distance(now.ambient_sky, last.ambient_sky) < 0.02f);
        if(glm::distance(now.direction, last.direction) > 0.02f) {
            CHECK(luminance(now.color) < 0.004f);
            CHECK(luminance(last.color) < 0.004f);
        }
        darkest = std::min(darkest, luminance(now.color));
        last = now;
    }
    CHECK(darkest < 1e-4f);

}

TEST_CASE("LIGHT.3 the sky : blue over a paler horizon by day, a glow around the low sun, what lights the ground follows") {

    Sky sky;
    SkyLight day = skyLight(sky);

    glm::vec3 up = skyColor(day, glm::vec3(0.f, 1.f, 0.f));
    glm::vec3 north = skyColor(day, glm::normalize(glm::vec3(0.f, 0.02f, -1.f)));
    CHECK(up.b > up.r * 2.f);
    CHECK(luminance(north) > luminance(up));

    // The fog of the shaders before the sky
    CHECK(glm::distance(day.horizon, glm::vec3(0.45f, 0.6f, 0.8f)) < 0.02f);

    // The light of the sky on the ground is of the order of what the shaders had as ambient light,
    // and what bounces is less
    CHECK(luminance(day.ambient_sky) > 0.2f);
    CHECK(luminance(day.ambient_sky) < 0.45f);
    CHECK(day.ambient_sky.b > day.ambient_sky.r);
    CHECK(luminance(day.ambient_ground) < luminance(day.ambient_sky));
    CHECK(luminance(skyRadiance(day, glm::vec3(0.f, 1.f, 0.f))) < luminance(up));

    // At sunset the sky is warm towards the sun and not away from it
    sky.time = 18.9f;
    SkyLight dusk = skyLight(sky);
    REQUIRE(dusk.sun.y > -0.05f);
    REQUIRE(dusk.sun.y < 0.1f);
    glm::vec3 flat = glm::normalize(glm::vec3(dusk.sun.x, 0.05f, dusk.sun.z));
    glm::vec3 toward = skyColor(dusk, flat), away = skyColor(dusk, glm::vec3(-flat.x, flat.y, -flat.z));
    CHECK(toward.r > toward.b * 1.5f);
    CHECK(toward.r > away.r * 1.3f);

}

TEST_CASE("LIGHT.4 the Sky system starts where the scene says and turns the sun a day in day_length") {

    entt::registry reg;
    SkySys system(reg, nlohmann::json { { "time", 6.f }, { "day_length", 240.f } });
    Sky& sky = reg.ctx().get<Sky>();
    CHECK(sky.time == doctest::Approx(6.f));
    for(int i = 0; i < 60; i++) system.tick(1.);
    CHECK(sky.time == doctest::Approx(12.f).epsilon(1e-4));
    for(int i = 0; i < 150; i++) system.tick(1.);
    CHECK(sky.time == doctest::Approx(3.f).epsilon(1e-3));

    // Stopped
    SkySys still(reg, nlohmann::json { { "time", 9.f }, { "day_length", 0.f } });
    still.tick(100.);
    CHECK(reg.ctx().get<Sky>().time == doctest::Approx(9.f));

    // Left out : a day of 24 minutes
    SkySys usual(reg, nlohmann::json::object());
    CHECK(reg.ctx().get<Sky>().day_length == doctest::Approx(Sky::default_day_length));
    CHECK(reg.ctx().get<Sky>().time == doctest::Approx(Sky::default_time));

}

}
