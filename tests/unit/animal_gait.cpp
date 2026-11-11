#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "logic/agents/ground_field.h"
#include "logic/animals/animal.h"
#include "logic/animals/gait.h"
#include "logic/animals/gait_dataset.h"
#include "logic/animals/gait_scene.h"
#include "logic/components/transform.h"

using namespace dy;

namespace {

// One animal of the generated library walking a path on a level plane, and what its feet did
struct Walk {
    bool ran = false;
    size_t legs = 0;
    float slide = 0.f;          // m/s : how fast planted feet moved over the ground at most (the 99th percentile)
    float reach_error = 0.f;    // metres : how far the feet were from where the walk wanted them, at most
    float lowest_planted = 0.f; // height of the foot joints while planted, over their height on hard ground (mean)
    float highest = 0.f;        // how high the feet were lifted over the ground at most (minus their planted height)
    float buried_travel = 0.f;  // metres a foot moved sideways while it was under the surface, leaving (mean per step)
    float bend = 0.f;           // radians, mean over the walk
    float speed = 0.f;
    float body_height = 0.f;    // mean height of the joint the legs hang from
};

Walk walk(const std::string& id, AnimalWalker::Path path, const GaitStyle& style, float seconds = 12.f, float radius = 2.f, bool left = true) {

    Walk out;
    entt::registry reg;
    auto& library = reg.ctx().emplace<AnimalLibrary>();
    if(!library.load(animalsDirectory())) return out;
    int index = library.find(id);
    if(index < 0) return out;
    auto species = library.species((uint32_t) index);
    if(!species || species->walk < 0) return out;

    reg.ctx().emplace<std::shared_ptr<GroundField>>(std::make_shared<PlaneField>());
    AnimalAnimationSys animation(reg);
    AnimalGaitSys gait_sys(reg);
    animation.preinit();
    gait_sys.preinit();
    gait_sys.init();
    auto& ground = *reg.ctx().get<std::shared_ptr<GroundField>>();

    const auto& entry = library.entries[index];
    auto entity = reg.create();
    auto& transform = reg.emplace<Transform>(entity);
    auto& animal = reg.emplace<Animal>(entity);
    animal.species = (uint32_t) index;
    animal.clip = species->walk;
    AnimalWalker walker;
    walker.path = path;
    // A man is not long : his height counts
    walker.radius = radius * std::max(entry.length, 0.6f * entry.height) * (left ? 1.f : -1.f);
    walker.speed = species->clips[species->walk].speed;
    reg.emplace<AnimalGait>(entity).style = style;

    const GaitModel& model = reg.ctx().get<GaitModels>().get(*species, species->walk);
    if(!model.valid) return out;
    out.legs = model.legs.size();
    out.ran = true;

    const float dt = 1.f / 60.f;
    std::vector<glm::vec3> last(model.legs.size());
    std::vector<bool> was_planted(model.legs.size(), false);
    std::vector<float> slides;
    std::vector<float> buried(model.legs.size(), 0.f);
    float buried_total = 0.f, planted_height = 0.f, bend = 0.f, body = 0.f;
    int steps = 0, planted_count = 0, frames = 0;

    for(int frame = 0; frame < (int) (seconds / dt); frame++) {
        // Circles of a negative radius turn right
        float r = walker.radius;
        walker.radius = std::abs(r);
        float turn = walkerCurvature(walker) * (r < 0.f ? -1.f : 1.f);
        walker.radius = r;
        walker.yaw += turn * walker.speed * dt;
        transform.position += glm::vec3(-std::sin(walker.yaw), 0.f, -std::cos(walker.yaw)) * walker.speed * dt;
        transform.position.y = -species->min.y;
        transform.rotation = glm::angleAxis(walker.yaw, glm::vec3(0.f, 1.f, 0.f));
        walker.travelled += walker.speed * dt;

        animation.tick(dt);
        gait_sys.tick(dt);

        const auto& state = reg.get<AnimalGait>(entity);
        const auto& pose = reg.get<AnimalPose>(entity);
        if(frame < 120 || state.feet.size() != model.legs.size()) continue;     // the walk has faded in

        const glm::mat4 to_world = transform.matrix();
        for(size_t i = 0; i < model.legs.size(); i++) {
            const auto& foot = state.feet[i];
            glm::vec3 joint = glm::vec3(to_world * pose.world[model.legs[i].chain.back()][3]);
            out.reach_error = std::max(out.reach_error, glm::distance(joint, foot.now));
            float over = joint.y - model.legs[i].sole;
            if(foot.planted && was_planted[i]) {
                slides.push_back(glm::length(glm::vec2(joint.x - last[i].x, joint.z - last[i].z)) / dt);
                if(foot.since > style.sink_time + 0.05f) {
                    planted_height += over;
                    planted_count++;
                }
            }
            if(!foot.planted) {
                out.highest = std::max(out.highest, over);
                if(was_planted[i]) buried[i] = 0.f;
                else if(over < -1e-3f) buried[i] += glm::length(glm::vec2(joint.x - last[i].x, joint.z - last[i].z));
            } else if(!was_planted[i]) {
                buried_total += buried[i];
                steps++;
            }
            was_planted[i] = foot.planted;
            last[i] = joint;
        }
        bend += state.bend;
        out.speed += state.speed;
        body += (to_world * pose.world[std::max(model.legs[0].attach, 0)][3]).y;
        frames++;
    }

    if(!slides.empty()) {
        std::sort(slides.begin(), slides.end());
        out.slide = slides[(size_t) (0.99f * (float) (slides.size() - 1))];
    }
    out.lowest_planted = planted_count ? planted_height / (float) planted_count : 0.f;
    out.buried_travel = steps ? buried_total / (float) steps : 0.f;
    out.bend = frames ? bend / (float) frames : 0.f;
    out.speed = frames ? out.speed / (float) frames : 0.f;
    out.body_height = frames ? body / (float) frames : 0.f;
    (void) ground;
    return out;

}

// Animals of several packs and makes : four legs, two legs, a man
const std::vector<std::pair<const char*, size_t>> walkers = {
    { "horse", 4 }, { "deer_red", 4 }, { "wolf_2", 4 }, { "rhinoceros", 4 }, { "bearbrown", 4 }, { "lioness", 4 }, { "cat", 4 }, { "buffalo", 4 },
    { "reindeer", 4 }, { "boar", 4 }, { "crow", 2 }, { "homo_heidelbergensis_walking", 2 },
};

}

TEST_SUITE("unit") {

TEST_CASE("AGAIT.1 a walk clip is read as legs : four for the quadrupeds, two for a bird and for a man") {

    AnimalLibrary library;
    if(!library.load(animalsDirectory())) {
        MESSAGE("no animal library (tools/animal_import.py builds it) : skipped");
        return;
    }
    for(auto& [id, legs] : walkers) {
        CAPTURE(id);
        int index = library.find(id);
        REQUIRE(index >= 0);
        auto species = library.species((uint32_t) index);
        REQUIRE(species);
        GaitModel model = analyseGait(*species, species->walk);
        CAPTURE(model.problem);
        REQUIRE(model.valid);
        std::string legs_found;
        for(auto& leg : model.legs) {
            legs_found += "\n    ";
            for(int joint : leg.chain) legs_found += species->joint_names[joint] + " > ";
            legs_found += " reach " + std::to_string(leg.reach) + " sole " + std::to_string(leg.sole) + " apex " + std::to_string(leg.apex)
                + " down " + std::to_string(leg.touch) + " to " + std::to_string(leg.lift) + " stand " + std::to_string(leg.stand.x) + " " + std::to_string(leg.stand.z);
        }
        CAPTURE(legs_found);
        CHECK(model.legs.size() == legs);
        int left = 0, front = 0;
        for(auto& leg : model.legs) {
            CHECK(leg.chain.size() >= 3);
            CHECK(leg.duty > 0.3f);
            CHECK(leg.duty < 0.9f);
            CHECK(leg.apex > 0.01f * model.height);
            left += leg.left;
            front += leg.front;
        }
        CHECK(left * 2 == (int) legs);
        if(legs == 4) CHECK(front == 2);
    }

    // How many of the library's animals walk procedurally
    size_t with_walk = 0, valid = 0;
    std::string without;
    for(uint32_t i = 0; i < library.entries.size(); i++) {
        auto species = library.species(i);
        if(!species || species->walk < 0) continue;
        with_walk++;
        GaitModel model = analyseGait(*species, species->walk);
        if(model.valid) valid++;
        else without += "\n    " + library.entries[i].id + " : " + model.problem;
    }
    MESSAGE(valid << " of the " << with_walk << " animals that have a walk get a procedural one; not :" << without);
    // The others are the animals of one pack, whose rig holds the feet with controls and not with the legs
    CHECK(valid * 10 >= with_walk * 7);

}

TEST_CASE("AGAIT.2 through a turn the planted feet stay where they are, and the body bends into the curve") {

    if(!walk("deer_red", AnimalWalker::Path::Circle, GaitStyle()).ran) {
        MESSAGE("no animal library : skipped");
        return;
    }
    GaitStyle clip_only;
    clip_only.ik = 0.f;
    for(auto& [id, legs] : walkers) {
        CAPTURE(id);
        Walk planted = walk(id, AnimalWalker::Path::Circle, GaitStyle());
        Walk sliding = walk(id, AnimalWalker::Path::Circle, clip_only);
        REQUIRE(planted.ran);
        CAPTURE(planted.slide);
        CAPTURE(sliding.slide);
        CAPTURE(planted.reach_error);
        CAPTURE(planted.speed);
        // The clip alone drags its feet through the turn; planted, they move 3 times less at least, and under 15 cm/s
        // (99 frames in 100 : a leg at the end of its reach lets its foot go)
        CHECK(planted.slide < 0.15f);
        CHECK(planted.slide * 3.f < sliding.slide);
        // The legs reach where the walk wants the feet, to 3 % of the height of the animal
        AnimalLibrary library;
        library.load(animalsDirectory());
        CHECK(planted.reach_error < 0.03f * library.entries[library.find(id)].height + 0.005f);
        // A left turn bends the body to the left, a right turn to the right
        CHECK(planted.bend > glm::radians(4.f));
        Walk right = walk(id, AnimalWalker::Path::Circle, GaitStyle(), 8.f, 2.f, false);
        CHECK(right.bend < -glm::radians(4.f));
        CHECK(sliding.bend == doctest::Approx(0.f));
    }

}

TEST_CASE("AGAIT.3 in soft ground the feet sink, are pulled out and lifted over it; straight up when asked") {

    if(!walk("deer_red", AnimalWalker::Path::Straight, GaitStyle()).ran) {
        MESSAGE("no animal library : skipped");
        return;
    }
    for(const char* id : { "deer_red", "horse", "wolf_2", "crow", "homo_heidelbergensis_walking" }) {
        CAPTURE(id);
        AnimalLibrary library;
        library.load(animalsDirectory());
        const float height = library.entries[library.find(id)].height;
        const float depth = 0.12f * height;

        Walk hard = walk(id, AnimalWalker::Path::Straight, GaitStyle());
        GaitStyle soft;
        soft.soft_depth = depth;
        soft.clearance = 0.04f * height;
        soft.exit_vertical = 1.f;
        Walk up = walk(id, AnimalWalker::Path::Straight, soft);
        soft.exit_vertical = 0.f;
        Walk along = walk(id, AnimalWalker::Path::Straight, soft);

        CAPTURE(hard.lowest_planted);
        CAPTURE(up.lowest_planted);
        CAPTURE(up.highest);
        CAPTURE(up.buried_travel);
        CAPTURE(along.buried_travel);
        // Planted feet are on the ground when it is hard, at the depth asked when it is soft
        CHECK(std::abs(hard.lowest_planted) < 0.02f * height);
        CHECK(up.lowest_planted == doctest::Approx(-depth).epsilon(0.15));
        // Lifted over the surface, by the clearance asked at least
        CHECK(up.highest > 0.9f * soft.clearance);
        // Pulled straight up, a foot hardly moves sideways before it is out; along its swing, it does
        CHECK(up.buried_travel < 0.4f * depth);
        CHECK(along.buried_travel > 2.f * up.buried_travel);
        // The body comes down with the ground
        CHECK(up.body_height < hard.body_height - 0.4f * depth);
    }

}

TEST_CASE("AGAIT.4 the steps follow the speed and the style : longer steps are slower ones") {

    if(!walk("deer_red", AnimalWalker::Path::Straight, GaitStyle()).ran) {
        MESSAGE("no animal library : skipped");
        return;
    }
    entt::registry reg;
    auto& library = reg.ctx().emplace<AnimalLibrary>();
    REQUIRE(library.load(animalsDirectory()));
    reg.ctx().emplace<std::shared_ptr<GroundField>>(std::make_shared<PlaneField>());
    AnimalAnimationSys animation(reg);
    AnimalGaitSys gait_sys(reg);
    animation.preinit();
    gait_sys.preinit();
    gait_sys.init();

    int index = library.find("deer_red");
    auto species = library.species((uint32_t) index);
    const float clip_speed = species->clips[species->walk].speed;

    auto rate = [&](float speed, float stride) {
        auto entity = reg.create();
        auto& transform = reg.emplace<Transform>(entity);
        auto& animal = reg.emplace<Animal>(entity);
        animal.species = (uint32_t) index;
        animal.clip = species->walk;
        reg.emplace<AnimalGait>(entity).style.stride = stride;
        for(int frame = 0; frame < 240; frame++) {
            transform.position.z -= speed / 60.f;
            animation.tick(1. / 60.);
            gait_sys.tick(1. / 60.);
        }
        float result = reg.get<Animal>(entity).rate;
        reg.destroy(entity);
        return result;
    };

    CHECK(rate(clip_speed, 1.f) == doctest::Approx(1.f).epsilon(0.03));
    CHECK(rate(1.4f * clip_speed, 1.f) == doctest::Approx(1.4f).epsilon(0.03));
    CHECK(rate(clip_speed, 1.25f) == doctest::Approx(0.8f).epsilon(0.03));

    // The style goes to and from json whole
    GaitStyle style;
    style.soft_depth = 0.2f;
    style.exit_vertical = 0.3f;
    GaitStyle back = GaitStyle::parse(style.json());
    CHECK(back.values() == style.values());
    CHECK(GaitStyle::names().size() == style.values().size());

}

TEST_CASE("AGAIT.5 demonstrations are walks drawn from ranges : written whole, the same for a seed, feet planted") {

    AnimalLibrary library;
    if(!library.load(animalsDirectory())) {
        MESSAGE("no animal library : skipped");
        return;
    }
    auto directory = std::filesystem::temp_directory_path() / "dynamical_tests" / "gait_demos";
    std::filesystem::remove_all(directory);

    GaitDemoOptions options;
    options.species = "deer_red";
    options.samples = 4;
    options.seconds = 3.f;
    options.terrain = false;
    options.ranges = GaitRanges::parse({ { "soft", 1.0 }, { "stride", { 0.9, 1.1 } } });
    GaitDemoResult result = writeGaitDemos(library, options, directory);
    CAPTURE(result.error);
    REQUIRE(result.ok);
    CHECK(result.legs == 4);
    CHECK(result.frames == 90);
    CHECK(std::filesystem::file_size(result.data) == (size_t) result.samples * (result.sample_floats + (size_t) result.frames * result.frame_floats) * sizeof(float));
    // A walk has its feet down about half of the time or more, and they hold
    CHECK(result.planted > 0.45f);
    CHECK(result.planted < 0.9f);
    CHECK(result.slide < 0.25f);
    CHECK(result.mean_speed > 0.3f);

    auto header = nlohmann::json::parse(std::ifstream(result.header));
    CHECK(header.at("samples") == 4);
    CHECK(header.at("joints").size() == (size_t) result.joints);
    CHECK(header.at("legs").size() == 4);
    CHECK(header.at("sample_values").size() == result.sample_floats);

    // Every walk within the ranges asked : strides, and a soft ground of 2 to 15 % of the height
    std::ifstream data(result.data, std::ios::binary);
    std::vector<float> first;
    const float height = header.at("height").get<float>();
    for(int sample = 0; sample < result.samples; sample++) {
        std::vector<float> values(result.sample_floats);
        data.read(reinterpret_cast<char*>(values.data()), (std::streamsize) (values.size() * sizeof(float)));
        data.seekg((std::streamoff) ((size_t) result.frames * result.frame_floats * sizeof(float)), std::ios::cur);
        if(sample == 0) first = values;
        CHECK(values[0] >= 0.9f);
        CHECK(values[0] <= 1.1f);
        CHECK(values[3] >= 0.02f * height - 1e-4f);
        CHECK(values[3] <= 0.15f * height + 1e-4f);
    }

    auto again = std::filesystem::temp_directory_path() / "dynamical_tests" / "gait_demos_again";
    std::filesystem::remove_all(again);
    GaitDemoResult second = writeGaitDemos(library, options, again);
    REQUIRE(second.ok);
    std::ifstream a(result.data, std::ios::binary), b(second.data, std::ios::binary);
    std::vector<char> bytes_a((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>()), bytes_b((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    CHECK(bytes_a == bytes_b);

    options.seed = 2;
    GaitDemoResult other = writeGaitDemos(library, options, again);
    REQUIRE(other.ok);
    std::ifstream c(other.data, std::ios::binary);
    std::vector<float> values(other.sample_floats);
    c.read(reinterpret_cast<char*>(values.data()), (std::streamsize) (values.size() * sizeof(float)));
    CHECK(values != first);

    options.species = "no_such_animal";
    CHECK_FALSE(writeGaitDemos(library, options, again).ok);

}

}
