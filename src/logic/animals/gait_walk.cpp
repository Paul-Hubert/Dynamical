#include "gait_walk.h"

#include <algorithm>
#include <cmath>

#include "logic/agents/ground_field.h"
#include "scene/world.h"

using namespace dy;

namespace {

float between(WorldRng& rng, const float* ends) {
    return (float) rng.uniform(ends[0], std::max(ends[0], ends[1]));
}

bool inHeights(const std::string& name) {
    return name == "clearance" || name == "soft_depth" || name == "crouch" || name == "spread";
}

}

void dy::drawCommand(WalkCommand& command, const GaitRanges& ranges, float clip_speed, float length, WorldRng& rng) {
    command.until = command.time + between(rng, ranges.segment);
    // Stand, walk or run. Ranges that only walk draw what they always drew
    float idle = ranges.idle, run = command.run_speed > 0.f ? ranges.run : 0.f;
    if(command.only == 1) {
        idle = run < 1.f ? idle / (1.f - run) : 0.f;
        run = 0.f;
    } else if(command.only == 2) {
        idle = 0.f;
        run = 1.f;
    }
    command.kind = 1;
    if(idle + run > 0.f) {
        const float u = (float) rng.uniform();
        command.kind = u < idle ? 0 : u < idle + run ? 2 : 1;
    }
    const bool running = command.kind == 2;
    command.wanted_speed = running ? command.run_speed * between(rng, ranges.run_speed) : clip_speed * between(rng, ranges.speed);
    float radius = between(rng, running ? ranges.run_radius : ranges.radius) * length;
    bool straight = rng.uniform() < ranges.straight;
    bool left = rng.uniform() < 0.5;
    command.wanted_turn = straight ? 0.f : (left ? 1.f : -1.f) / std::max(radius, 0.1f);
    if(command.kind == 0) {
        command.wanted_speed = 0.f;
        command.wanted_turn = 0.f;
    }
}

int dy::commandKind(const GaitRanges& ranges, float speed, float walk_speed, float run_speed) {
    if(ranges.idle > 0.f && speed < 0.5f * ranges.speed[0] * walk_speed) return 0;
    if(run_speed > 0.f && ranges.run > 0.f && speed > 0.5f * (ranges.speed[1] * walk_speed + ranges.run_speed[0] * run_speed)) return 2;
    return 1;
}

void dy::startCommand(WalkCommand& command, const GaitRanges& ranges, float clip_speed, float length, float yaw, WorldRng& rng,
    float run_speed, int only) {
    command = WalkCommand();
    command.run_speed = run_speed;
    command.only = only;
    command.yaw = yaw;
    drawCommand(command, ranges, clip_speed, length, rng);
    command.speed = command.wanted_speed;
    command.turn = command.wanted_turn;
    command.started = true;
}

void dy::stepCommand(WalkCommand& command, const GaitRanges& ranges, float clip_speed, float length, WorldRng& rng, float dt) {
    command.time += dt;
    if(command.time >= command.until) drawCommand(command, ranges, clip_speed, length, rng);
    // A second to go from one speed and one turn to the next
    command.speed += (command.wanted_speed - command.speed) * std::min(2.f * dt, 1.f);
    command.turn += (command.wanted_turn - command.turn) * std::min(2.f * dt, 1.f);
    command.yaw += command.turn * command.speed * dt;
}

GaitStyle dy::drawStyle(const GaitRanges& ranges, float height, WorldRng& rng, int soft) {
    nlohmann::json j = nlohmann::json::object();
    auto lows = ranges.low.values(), highs = ranges.high.values();
    bool is_soft = rng.uniform() < ranges.soft;
    if(soft >= 0) is_soft = soft != 0;
    for(size_t i = 0; i < lows.size(); i++) {
        const std::string& name = GaitStyle::names()[i];
        float value = lows[i] + (highs[i] - lows[i]) * (float) rng.uniform();
        if(inHeights(name)) value *= height;
        if(name == "soft_depth" && !is_soft) value = 0.f;
        j[name] = value;
    }
    return GaitStyle::parse(j);
}

bool GaitWalk::setup(std::shared_ptr<const AnimalSpecies> of, int clip, float animal_length) {
    species = std::move(of);
    model = analyseGait(*species, clip);
    length = std::max(animal_length, 0.1f);
    const bool grounded = std::abs(species->min.y) < 0.2f * (species->max.y - species->min.y);
    sole = grounded ? species->min.y : 0.f;
    animal.clip = clip;
    return model.valid;
}

void GaitWalk::start(const GaitStyle& style, glm::vec3 position, float yaw, float phase) {
    transform = Transform();
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));
    int clip = animal.clip;
    animal = Animal();
    animal.clip = clip;
    animal.time = phase * model.period;
    gait = AnimalGait();
    gait.style = style;
    gait.model = clip;
    gait.weight = 1.f;
    walker = AnimalWalker();
    walker.path = AnimalWalker::Path::Free;
}

void GaitWalk::step(float speed, float turn, const GroundField* ground, float dt) {
    walker.speed = speed;
    walker.turn = turn;
    stepWalker(walker, transform, length, sole, ground, dt);
    // As AnimalAnimation then AnimalGait do
    animal.time += dt * animal.rate;
    // A gait is a cycle whatever its clip says: one that is not marked as a loop (many runs) would hold its last pose
    if(model.period > 0.f) animal.time -= std::floor(animal.time / model.period) * model.period;
    evaluate(*species, animal, pose, scratch);
    const float stride = std::max(model.speed, 0.05f * model.height) * model.period * std::max(gait.style.stride, 0.1f);
    gait.period = std::clamp(stride / std::max(std::abs(gait.speed), 1e-3f), 0.55f * model.period, 1.8f * model.period);
    animal.rate = model.period / gait.period;
    proceduralWalk(*species, model, transform, animal.time, dt, ground, gait, pose);
}
