#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#include "logic/animals/animal.h"
#include "logic/animals/herd.h"
#include "logic/animals/steer.h"
#include "logic/components/transform.h"
#include "logic/player/mount.h"

using namespace dy;

namespace {

// A library of one animal : a chain of three joints along -z, a quad on it, and a walk of three frames that takes
// its root one metre forward while the middle joint turns
std::filesystem::path writeLibrary(const std::string& name, uint32_t bad_index = 0) {

    auto dir = std::filesystem::temp_directory_path() / "dynamical_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "species");

    nlohmann::json library = { { "format_version", 1 }, { "species", { {
        { "id", "worm" }, { "name", "Worm" }, { "height", 0.2f }, { "length", 2.f }, { "width", 0.2f }, { "joints", 3 }, { "triangles", 2 },
        { "lods", 1 }, { "clips", 2 }, { "walk", "walk" }, { "walk_speed", 1.f } } } } };
    std::ofstream(dir / "library.json") << library.dump();

    const uint32_t joints = 3, frames = 3;
    std::vector<uint8_t> anim;
    auto floats = [&](std::initializer_list<float> values) {
        for(float v : values) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            anim.insert(anim.end(), p, p + 4);
        }
    };
    auto quat = [&](float x, float y, float z, float w) {
        for(float v : { x, y, z, w }) {
            int16_t q = (int16_t) std::lround(v * 32767.f);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&q);
            anim.insert(anim.end(), p, p + 2);
        }
    };
    // walk : root translation animated, middle joint rotation animated
    anim.insert(anim.end(), { 1, 2, 0, 0 });
    floats({ 0, 0, 0,  0, 0, -0.5f,  0, 0, -1 });
    quat(0, 0, 0, 1);
    floats({ 1, 1, 1 });
    floats({ 0, 0, -1 });
    const float half = std::sqrt(0.5f);
    quat(0, 0, 0, 1); quat(0, half, 0, half); quat(0, 0, 0, 1);
    floats({ 1, 1, 1 });
    floats({ 0, 0, -1 });
    quat(0, 0, 0, 1);
    floats({ 1, 1, 1 });
    const size_t walk_size = anim.size();
    // idle : nothing moves
    anim.insert(anim.end(), { 0, 0, 0, 0 });
    for(uint32_t j = 0; j < joints; j++) {
        floats({ 0, 0, j == 0 ? 0.f : -1.f });
        quat(0, 0, 0, 1);
        floats({ 1, 1, 1 });
    }
    std::ofstream(dir / "species" / "worm.anim", std::ios::binary).write(reinterpret_cast<const char*>(anim.data()), (std::streamsize) anim.size());

    nlohmann::json species = {
        { "id", "worm" }, { "bounds", { { "min", { -0.1, 0, -2 } }, { "max", { 0.1, 0.2, 0 } } } },
        { "vertex_count", 4 }, { "index_count", 6 },
        { "joints", { { { "name", "root" }, { "parent", -1 } }, { { "name", "middle" }, { "parent", 0 } }, { { "name", "tip" }, { "parent", 1 } } } },
        { "materials", { { { "name", "skin" }, { "color", nullptr }, { "normal", nullptr }, { "tint", { 1, 0.5, 0.25, 1 } }, { "alpha_cutoff", 0 }, { "two_sided", false } } } },
        { "lods", { { { "screen_height", 0 }, { "triangles", 2 }, { "draws", { { { "material", 0 }, { "first_index", 0 }, { "index_count", 6 } } } } } } },
        { "clips", {
            { { "name", "walk" }, { "kind", "walk" }, { "frames", frames }, { "fps", 2 }, { "loop", true }, { "offset", 0 }, { "size", walk_size },
              { "motion_joint", 0 }, { "root_motion", { 0, 0, -1 } }, { "travel", { 0, 0, -1 } }, { "speed", 1 } },
            { { "name", "idle" }, { "kind", "idle" }, { "frames", 1 }, { "fps", 30 }, { "loop", true }, { "offset", walk_size }, { "size", anim.size() - walk_size },
              { "motion_joint", -1 }, { "root_motion", { 0, 0, 0 } }, { "travel", { 0, 0, 0 } }, { "speed", 0 } } } },
        { "roles", { { "idle", "idle" }, { "walk", "walk" }, { "trot", nullptr }, { "run", nullptr }, { "eat", nullptr }, { "rest", nullptr } } } };
    std::ofstream(dir / "species" / "worm.json") << species.dump();

    std::vector<AnimalVertex> vertices(4);
    for(size_t i = 0; i < vertices.size(); i++) {
        vertices[i] = {};
        vertices[i].position = glm::vec3(i % 2 ? 0.1f : -0.1f, 0.f, i < 2 ? 0.f : -2.f);
        vertices[i].joints[0] = i < 2 ? 0 : 2;
        vertices[i].weights[0] = 255;
    }
    std::vector<uint32_t> indices { 0, 1, 2, 2, 1, 3 };
    indices[5] += bad_index;
    std::ofstream bin(dir / "species" / "worm.bin", std::ios::binary);
    bin.write(reinterpret_cast<const char*>(vertices.data()), (std::streamsize) (vertices.size() * sizeof(AnimalVertex)));
    bin.write(reinterpret_cast<const char*>(indices.data()), (std::streamsize) (indices.size() * sizeof(uint32_t)));
    // Inverse bind matrices (3 rows of 4) : the joints are at z = 0, -1, -2
    for(uint32_t j = 0; j < joints; j++) {
        float m[12] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, (float) j };
        bin.write(reinterpret_cast<const char*>(m), sizeof(m));
    }
    for(uint32_t j = 0; j < joints; j++) {
        float r[10] = { 0, 0, j == 0 ? 0.f : -1.f,  0, 0, 0, 1,  1, 1, 1 };
        bin.write(reinterpret_cast<const char*>(r), sizeof(r));
    }

    return dir;

}

float identityError(const std::vector<glm::mat4>& matrices) {
    float error = 0.f;
    for(auto& m : matrices) {
        for(int c = 0; c < 4; c++) {
            for(int r = 0; r < 4; r++) error = std::max(error, std::abs(m[c][r] - (c == r ? 1.f : 0.f)));
        }
    }
    return error;
}

}

TEST_SUITE("unit") {

TEST_CASE("ANIMAL.1 the library, a species and its geometry load; a bad index is refused") {

    AnimalLibrary library;
    CHECK_FALSE(library.load(std::filesystem::temp_directory_path() / "dynamical_tests" / "no_such_animals"));

    REQUIRE(library.load(writeLibrary("animals_load")));
    REQUIRE(library.entries.size() == 1);
    CHECK(library.find("worm") == 0);
    CHECK(library.find("snake") == -1);
    CHECK(library.entries[0].walk == "walk");

    auto species = library.species(0);
    REQUIRE(species);
    CHECK(species->parents == std::vector<int32_t> { -1, 0, 1 });
    CHECK(species->clips.size() == 2);
    CHECK(species->walk == 0);
    CHECK(species->idle == 1);
    CHECK(species->run == -1);
    CHECK(species->clips[0].duration() == doctest::Approx(1.f));
    CHECK(species->findJoint("tip") == 2);

    std::vector<uint32_t> indices;
    auto vertices = AnimalLibrary::loadGeometry(library.directory, *species, indices);
    CHECK(vertices.size() == 4);
    CHECK(indices.size() == 6);

    AnimalLibrary broken;
    REQUIRE(broken.load(writeLibrary("animals_bad_index", 10)));
    auto bad = broken.species(0);
    REQUIRE(bad);
    CHECK_THROWS(AnimalLibrary::loadGeometry(broken.directory, *bad, indices));

}

TEST_CASE("ANIMAL.2 the bind pose skins to the identity, a clip moves the joints as its keys say") {

    AnimalLibrary library;
    REQUIRE(library.load(writeLibrary("animals_pose")));
    auto species = library.species(0);
    REQUIRE(species);

    std::vector<JointPose> pose;
    std::vector<glm::mat4> world, skin;
    species->sample(-1, 0.f, true, pose);
    species->worldMatrices(pose, world);
    species->skinMatrices(world, skin);
    CHECK(identityError(skin) < 1e-5f);
    CHECK(world[2][3].z == doctest::Approx(-2.f));

    // Half way between the first two keys : the root a quarter of a metre on, the middle joint turned 45 degrees
    species->sample(0, 0.25f, false, pose);
    CHECK(pose[0].translation.z == doctest::Approx(-0.25f));
    CHECK(glm::degrees(glm::angle(pose[1].rotation)) == doctest::Approx(45.f).epsilon(0.02));
    species->worldMatrices(pose, world);
    // The tip is one metre from the middle joint, in the direction the middle joint turned -z to
    glm::vec3 middle(world[1][3]), tip(world[2][3]);
    CHECK(glm::distance(middle, tip) == doctest::Approx(1.f));
    CHECK(std::abs(tip.x - middle.x) == doctest::Approx(std::sqrt(0.5f)).epsilon(0.02));

    // The clip loops : a round later is the same pose, with the travel of its root taken out when it plays in place
    std::vector<JointPose> later;
    species->sample(0, 1.25f, false, later);
    CHECK(later[0].translation.z == doctest::Approx(-0.25f));
    species->sample(0, 0.75f, true, pose);
    CHECK(pose[0].translation.z == doctest::Approx(0.f).epsilon(1e-4));
    species->sample(0, 0.75f, false, pose);
    CHECK(pose[0].translation.z == doctest::Approx(-0.75f));

}

TEST_CASE("ANIMAL.3 AnimalAnimation advances the clips and blends from the clip an animal comes from") {

    entt::registry reg;
    auto& library = reg.ctx().emplace<AnimalLibrary>();
    REQUIRE(library.load(writeLibrary("animals_system")));

    AnimalAnimationSys sys(reg);
    sys.preinit();

    auto entity = reg.create();
    reg.emplace<Transform>(entity);
    auto& animal = reg.emplace<Animal>(entity);
    animal.clip = 0;
    animal.time = 0.25f;
    sys.tick(0.);
    auto& pose = reg.get<AnimalPose>(entity);
    REQUIRE(pose.skin.size() == 3);
    float turned = glm::degrees(glm::angle(pose.local[1].rotation));
    CHECK(turned == doctest::Approx(45.f).epsilon(0.02));

    // To the idle (nothing turned) over a second : half way after half a second
    reg.get<Animal>(entity).rate = 0.f;
    reg.get<Animal>(entity).play(1, 1.f);
    reg.get<Animal>(entity).previous_rate = 0.f;
    sys.tick(0.5);
    float half = glm::degrees(glm::angle(reg.get<AnimalPose>(entity).local[1].rotation));
    CHECK(half > 15.f);
    CHECK(half < 30.f);
    sys.tick(0.6);
    CHECK(glm::degrees(glm::angle(reg.get<AnimalPose>(entity).local[1].rotation)) < 0.5f);

    reg.get<Animal>(entity).rate = 1.f;
    float before = reg.get<Animal>(entity).time;
    sys.tick(0.1);
    CHECK(reg.get<Animal>(entity).time == doctest::Approx(before + 0.1f));

}

TEST_CASE("HERD.1 herds are the same for a seed, stand on the ground, walk no faster than their walk and keep apart") {

    const nlohmann::json definition = {
        { "cell", 60 }, { "density", 1.0 }, { "radius", 100 }, { "range", 40 }, { "graze", { 1, 2 } },
        { "herds", { { { "name", "worms" }, { "count", { 5, 8 } }, { "spacing", 3 }, { "members", { { "worm", 1 } } } } } } };

    auto run = [&](uint32_t seed, int ticks) {
        entt::registry reg;
        auto& library = reg.ctx().emplace<AnimalLibrary>();
        REQUIRE(library.load(writeLibrary("animals_herd")));
        HerdSys herds(reg, { { "definition", definition }, { "seed", seed }, { "centre", { 0, 0 } } });
        AnimalAnimationSys animation(reg);
        herds.preinit();
        animation.preinit();
        herds.init();

        std::map<entt::entity, glm::vec3> start;
        float fastest = 0.f;
        bool walked = false;
        for(int i = 0; i < ticks; i++) {
            herds.tick(1. / 60.);
            animation.tick(1. / 60.);
            for(auto [entity, transform, tag] : reg.view<Transform, HerdAnimal>().each()) {
                start.emplace(entity, transform.position);
                fastest = std::max(fastest, tag.speed);
                walked = walked || reg.get<Animal>(entity).clip == 0;
            }
        }

        std::vector<glm::vec3> positions;
        float moved = 0.f;
        for(auto [entity, transform, tag] : reg.view<Transform, HerdAnimal>().each()) {
            positions.push_back(transform.position);
            moved = std::max(moved, glm::distance(transform.position, start.at(entity)));
            // A level plane at y = 0
            CHECK(std::abs(transform.position.y) < 1e-3f);
        }
        CHECK(reg.ctx().get<HerdStats>().animals == positions.size());
        // The walk clip covers 1 m/s; an animal that catches up walks 1.3 times that, scaled by its size
        CHECK(fastest <= 1.3f * 1.08f + 1e-3f);
        if(ticks > 600) {
            CHECK(moved > 3.f);
            CHECK(walked);
        }
        herds.finish();
        CHECK(reg.view<HerdAnimal>().empty());
        return positions;
    };

    auto a = run(3, 1200), b = run(3, 1200), c = run(4, 1200);
    REQUIRE(a.size() >= 5);
    REQUIRE(a.size() == b.size());
    for(size_t i = 0; i < a.size(); i++) CHECK(glm::distance(a[i], b[i]) < 1e-4f);
    bool same = a.size() == c.size();
    for(size_t i = 0; same && i < a.size(); i++) same = glm::distance(a[i], c[i]) < 1e-3f;
    CHECK_FALSE(same);

}

TEST_CASE("HERD.2 a herd lets go of an animal that is steered, takes it back when it is not, and leaves it behind when the herd goes") {

    const nlohmann::json definition = {
        { "cell", 60 }, { "density", 1.0 }, { "radius", 100 }, { "range", 40 }, { "graze", { 1, 2 } },
        { "herds", { { { "name", "worms" }, { "count", { 5, 8 } }, { "spacing", 3 }, { "members", { { "worm", 1 } } } } } } };

    entt::registry reg;
    auto& library = reg.ctx().emplace<AnimalLibrary>();
    REQUIRE(library.load(writeLibrary("animals_steer")));
    HerdSys herds(reg, { { "definition", definition }, { "seed", 3 }, { "centre", { 0, 0 } } });
    SteeredWalkSys steered(reg, nlohmann::json::object());
    AnimalAnimationSys animation(reg);
    MountablesSys mountables(reg, { { "species", { "worm" } } });
    herds.preinit();
    steered.preinit();
    animation.preinit();
    mountables.preinit();
    herds.init();
    steered.init();
    auto frames = [&](int n) {
        for(int i = 0; i < n; i++) {
            herds.tick(1. / 60.);
            steered.tick(1. / 60.);
            animation.tick(1. / 60.);
            mountables.tick(1. / 60.);
        }
    };
    frames(60);

    auto members = reg.view<HerdAnimal>();
    REQUIRE(members.begin() != members.end());
    const entt::entity one = *members.begin();

    // Animals of the species asked for can be ridden: a saddle on their skin, the speeds of their clips
    REQUIRE(reg.all_of<Mountable>(one));
    {
        const auto& mountable = reg.get<Mountable>(one);
        MESSAGE("saddle " << mountable.saddle.x << " " << mountable.saddle.y << " " << mountable.saddle.z << ", joint " << mountable.joint
            << ", walks " << mountable.walk_speed << " m/s, runs " << mountable.run_speed);
        CHECK(mountable.walk_speed == doctest::Approx(2.f));
        CHECK(mountable.run_speed > mountable.walk_speed);
        CHECK((mountable.collider == entt::null));      // nobody around to bump into it
        // On the joint that carries it, the saddle is where it is in the model
        glm::vec3 seat = saddleOf(reg, one);
        const auto& transform = reg.get<Transform>(one);
        glm::vec3 fixed = transform.position + transform.rotation * (mountable.saddle * transform.scale);
        CHECK(glm::distance(seat, fixed) < 0.5f);
    }

    // Steered toward +x: it goes there, whatever its herd does
    const glm::vec3 from = reg.get<Transform>(one).position;
    auto& steer = reg.emplace<AnimalSteer>(one);
    steer.yaw = glm::radians(-90.f);
    steer.speed = 0.8f;
    frames(600);
    glm::vec3 to = reg.get<Transform>(one).position;
    MESSAGE("steered from " << from.x << " " << from.z << " to " << to.x << " " << to.z);
    CHECK(to.x - from.x > 6.f);
    CHECK(std::abs(headingOf(reg.get<Transform>(one).rotation) - glm::radians(-90.f)) < glm::radians(3.f));
    CHECK(reg.all_of<SteeredWalk>(one));
    CHECK(reg.get<HerdAnimal>(one).speed == 0.f);
    CHECK(reg.get<Animal>(one).clip == 0);      // its walk

    // Let go: it halts, then its herd has it walk again
    reg.remove<AnimalSteer>(one);
    frames(300);
    CHECK(!reg.all_of<SteeredWalk>(one));
    const glm::vec3 halted = reg.get<Transform>(one).position;
    bool walked = false;
    for(int i = 0; i < 1200; i++) {
        frames(1);
        walked = walked || reg.get<HerdAnimal>(one).moving;
    }
    CHECK(walked);
    CHECK(glm::distance(reg.get<Transform>(one).position, halted) > 2.f);

    // Its herd goes while someone has it: it stays, nobody's
    reg.emplace<AnimalSteer>(one);
    herds.finish();
    CHECK(reg.valid(one));
    CHECK(!reg.all_of<HerdAnimal>(one));
    CHECK(reg.view<HerdAnimal>().empty());
    mountables.finish();

}

TEST_CASE("ANIMAL.library every animal of the generated library loads, skins its bind pose in place and samples its clips") {

    AnimalLibrary library;
    if(!library.load(animalsDirectory())) {
        MESSAGE("no animal library (tools/animal_import.py builds it) : skipped");
        return;
    }
    REQUIRE(library.entries.size() > 0);

    size_t clips = 0;
    for(uint32_t i = 0; i < library.entries.size(); i++) {
        CAPTURE(library.entries[i].id);
        auto species = library.species(i);
        REQUIRE(species);

        std::vector<JointPose> pose;
        std::vector<glm::mat4> world, skin;
        species->sample(-1, 0.f, true, pose);
        species->worldMatrices(pose, world);
        species->skinMatrices(world, skin);
        // Joints of rigs in centimetres carry a scale of 0.01 and its inverse : the product is the identity to 1e-3
        CHECK(identityError(skin) < 2e-3f);

        std::vector<uint32_t> indices;
        auto vertices = AnimalLibrary::loadGeometry(library.directory, *species, indices);
        CHECK(vertices.size() == species->vertex_count);
        for(auto& vertex : vertices) {
            int weight = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] + vertex.weights[3];
            if(weight != 255) {
                FAIL_CHECK("weights that do not sum to 255");
                break;
            }
        }

        for(size_t c = 0; c < species->clips.size(); c++) {
            const AnimalClip& clip = species->clips[c];
            for(float t : { 0.f, 0.37f * clip.duration(), clip.duration() }) {
                species->sample((int) c, t, true, pose);
                bool finite = true;
                for(auto& joint : pose) {
                    finite = finite && std::isfinite(joint.translation.x + joint.translation.y + joint.translation.z)
                        && std::abs(glm::length(joint.rotation) - 1.f) < 1e-3f && std::isfinite(joint.scale.x + joint.scale.y + joint.scale.z);
                }
                if(!finite) {
                    CAPTURE(clip.name);
                    FAIL_CHECK("a clip with a joint that is not finite");
                }
            }
            clips++;
        }
    }
    MESSAGE(library.entries.size() << " animals, " << clips << " clips");

}

}
