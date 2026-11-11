#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "logic/lighting/probes.h"
#include "logic/sky/sky.h"

#include <set>
#include <tuple>

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

TEST_CASE("LIGHT.5 the light of a probe : two bands per colour give back the light of a sky and of a lamp on a surface") {

    const int rays = 4096;

    // A sky of one colour over a black ground : a surface facing up sees all of it, a wall half, the
    // underside of a roof nothing
    {
        ProbeLight light;
        const glm::vec3 sky(0.3f, 0.5f, 0.8f);
        for(int i = 0; i < rays; i++) {
            glm::vec3 d = fibonacciDirection(i, rays);
            CHECK(std::abs(glm::length(d) - 1.f) < 1e-4f);
            light.add(d, d.y > 0.f ? sky : glm::vec3(0.f), rays);
        }
        CHECK(glm::distance(light.irradiance(glm::vec3(0.f, 1.f, 0.f)), sky) < 0.01f);
        CHECK(glm::distance(light.irradiance(glm::vec3(1.f, 0.f, 0.f)), 0.5f * sky) < 0.01f);
        CHECK(glm::length(light.irradiance(glm::vec3(0.f, -1.f, 0.f))) < 0.01f);
    }

    // The same light from everywhere : the same on any surface
    {
        ProbeLight light;
        for(int i = 0; i < rays; i++) light.add(fibonacciDirection(i, rays), glm::vec3(0.4f), rays);
        for(glm::vec3 n : { glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, -1.f, 0.f), glm::normalize(glm::vec3(1.f, 1.f, -1.f)) }) {
            CHECK(glm::distance(light.irradiance(n), glm::vec3(0.4f)) < 0.005f);
        }
    }

    // A lamp in one direction : brightest on the surface that faces it, never negative behind
    {
        ProbeLight light;
        glm::vec3 lamp = glm::normalize(glm::vec3(1.f, 0.5f, 0.2f));
        for(int i = 0; i < rays; i++) {
            glm::vec3 d = fibonacciDirection(i, rays);
            light.add(d, glm::dot(d, lamp) > 0.95f ? glm::vec3(10.f, 5.f, 0.f) : glm::vec3(0.f), rays);
        }
        glm::vec3 facing = light.irradiance(lamp), side = light.irradiance(glm::normalize(glm::cross(lamp, glm::vec3(0.f, 1.f, 0.f))));
        glm::vec3 behind = light.irradiance(-lamp);
        CHECK(facing.r > 2.f * side.r);
        CHECK(facing.r == doctest::Approx(2.f * facing.g).epsilon(0.01));
        CHECK(facing.b == 0.f);
        CHECK(behind.r >= 0.f);
        CHECK(behind.r < 0.2f * facing.r);
    }

}

TEST_CASE("LIGHT.6 the probes of a chunk : traced in the air near the ground and the plants, filled just under the ground, left alone elsewhere") {

    // Flat ground at 100 m in a chunk at the origin : sample y of the volume is at (y - border) * 4 m
    const float ground = 100.f;
    std::vector<float> densities(chunk::num_values);
    for(int x = 0; x < chunk::values; x++) {
        for(int z = 0; z < chunk::values; z++) {
            for(int y = 0; y < chunk::values; y++) {
                densities[chunk::index(x, y, z)] = ground - (float) ((y - chunk::border) * chunk::cube_size) - 0.5f;
            }
        }
    }

    ProbeClasses flat = classifyProbes(densities, glm::vec3(0.f), {});
    REQUIRE(flat.classes.size() == probes::count);

    auto at = [&](const ProbeClasses& c, int x, int y, int z) { return c.classes[probes::index(x, y, z)]; };
    // Probes at 96 m and under are in the ground (density > 0), from 100 m in the air
    for(int x : { 0, 31, 64 }) {
        for(int z : { 0, 17, 64 }) {
            CHECK(at(flat, x, 20, z) == probes::Rock);
            CHECK(at(flat, x, 23, z) == probes::Rock);
            CHECK(at(flat, x, 24, z) == probes::Fill);      // 96 m : next to the air
            CHECK(at(flat, x, 25, z) == probes::Active);    // 100 m
            CHECK(at(flat, x, 26, z) == probes::Active);    // 104 m : 8 m over the last probe of the ground
            CHECK(at(flat, x, 27, z) == probes::Open);
            CHECK(at(flat, x, 60, z) == probes::Open);
        }
    }
    CHECK(flat.active.size() == (size_t) 2 * probes::size * probes::size);
    CHECK(flat.fill.size() == (size_t) probes::size * probes::size);
    for(uint32_t i : flat.active) CHECK(flat.classes[i] == probes::Active);
    for(uint32_t i : flat.fill) CHECK(flat.classes[i] == probes::Fill);

    // A tree of 20 m : the air around its crown is traced too, further up than over the bare ground
    PlantBounds tree { glm::vec3(128.f, ground, 128.f), 6.f, 20.f };
    ProbeClasses planted = classifyProbes(densities, glm::vec3(0.f), { tree });
    CHECK(at(planted, 32, 29, 32) == probes::Active);       // 116 m, in the crown
    CHECK(at(planted, 32, 31, 32) == probes::Active);       // 124 m : under 8 m over its top
    CHECK(at(planted, 32, 33, 32) == probes::Open);         // 132 m
    CHECK(at(planted, 35, 29, 32) == probes::Active);       // 12 m aside : within 8 m of its bounds
    CHECK(at(planted, 36, 29, 32) == probes::Open);         // 16 m aside
    CHECK(at(planted, 32, 20, 32) == probes::Rock);         // the ground stays the ground
    CHECK(planted.active.size() > flat.active.size());

    // The origin of the chunk moves the plants, not the densities
    ProbeClasses moved = classifyProbes(densities, glm::vec3(256.f, 0.f, 0.f), { tree });
    CHECK(moved.active.size() == flat.active.size());

}

TEST_CASE("LIGHT.7 the bricks of the chunks : eight to a page, found through a table that wraps") {

    ProbeBricks bricks(2);
    CHECK(bricks.pages() == 0);

    // The first eight chunks fill a page, each at its corner of it
    std::set<std::tuple<int, int, int>> corners;
    for(int i = 0; i < 8; i++) {
        int brick = bricks.allocate(glm::ivec3(i, 0, -i));
        CHECK(brick == i);
        CHECK(ProbeBricks::page(brick) == 0);
        glm::ivec3 offset = ProbeBricks::offset(brick);
        CHECK(offset.x % probes::size == 0);
        CHECK(glm::all(glm::lessThan(offset, glm::ivec3(ProbeBricks::page_side * probes::size))));
        corners.insert({ offset.x, offset.y, offset.z });
    }
    CHECK(corners.size() == 8);
    CHECK(bricks.pages() == 1);
    CHECK(bricks.allocate(glm::ivec3(3, 0, -3)) == 3);      // the same chunk : the same brick

    // The table : brick + 1 at the cell of the chunk, also for negative coordinates
    const auto& table = bricks.indirection();
    REQUIRE(table.size() == (size_t) 16 * 16 * 16);
    CHECK(table[ProbeBricks::cell(glm::ivec3(5, 0, -5))] == 6);
    CHECK(ProbeBricks::cell(glm::ivec3(5, 0, -5)) == (size_t) (11 * 16 + 0) * 16 + 5);
    CHECK(ProbeBricks::cell(glm::ivec3(-1, -1, -1)) == (size_t) (15 * 16 + 15) * 16 + 15);
    CHECK(table[ProbeBricks::cell(glm::ivec3(9, 9, 9))] == 0);

    // A chunk 16 further shares the cell of another : no brick while that one has its own
    CHECK(bricks.allocate(glm::ivec3(5 + 16, 0, -5)) == -1);
    bricks.release(glm::ivec3(5, 0, -5));
    CHECK(bricks.find(glm::ivec3(5, 0, -5)) == -1);
    CHECK(table[ProbeBricks::cell(glm::ivec3(5, 0, -5))] == 0);
    CHECK(bricks.allocate(glm::ivec3(5 + 16, 0, -5)) == 5);  // the brick that was freed

    // Two pages and no more
    for(int i = 8; i < 16; i++) CHECK(bricks.allocate(glm::ivec3(0, 1, i - 8)) == i);
    CHECK(bricks.pages() == 2);
    CHECK(ProbeBricks::page(15) == 1);
    CHECK(bricks.allocate(glm::ivec3(7, 7, 7)) == -1);
    CHECK(bricks.used() == 16);

}

}
