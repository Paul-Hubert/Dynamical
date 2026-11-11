#include "gait_dataset.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "logic/agents/ground_field.h"
#include "logic/animals/gait_scene.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"

using namespace dy;

namespace {

const float pi = 3.14159265f;

// PCG, 24 bits a draw
float unit(uint32_t& state) {
    state = state * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word ^= word >> 22u;
    return (float) (word >> 8) / 16777216.f;
}

float between(uint32_t& state, const float* ends) {
    return ends[0] + (ends[1] - ends[0]) * unit(state);
}

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    r[0] = glm::normalize(r[0]);
    r[1] = glm::normalize(r[1]);
    r[2] = glm::normalize(r[2]);
    return glm::normalize(glm::quat_cast(r));
}

void pair(const nlohmann::json& j, const char* name, float* ends) {
    if(auto it = j.find(name); it != j.end() && it->is_array() && it->size() == 2) {
        ends[0] = it->at(0).get<float>();
        ends[1] = it->at(1).get<float>();
    }
}

// Parameters of GaitStyle given in heights of the animal
bool inHeights(const std::string& name) {
    return name == "clearance" || name == "soft_depth" || name == "crouch" || name == "spread";
}

}

GaitRanges GaitRanges::defaults() {
    GaitRanges r;
    r.low.stride = 0.8f;            r.high.stride = 1.25f;
    r.low.step_height = 0.8f;       r.high.step_height = 1.6f;
    r.low.clearance = 0.f;          r.high.clearance = 0.08f;
    r.low.soft_depth = 0.02f;       r.high.soft_depth = 0.15f;
    r.low.sink_time = 0.06f;        r.high.sink_time = 0.2f;
    r.low.exit_vertical = 0.f;      r.high.exit_vertical = 1.f;
    r.low.entry_vertical = 0.f;     r.high.entry_vertical = 1.f;
    r.low.exit_part = 0.2f;         r.high.exit_part = 0.4f;
    r.low.body_sink = 0.5f;         r.high.body_sink = 0.9f;
    r.low.crouch = 0.f;             r.high.crouch = 0.04f;
    r.low.spread = -0.01f;          r.high.spread = 0.03f;
    r.low.bend = 0.6f;              r.high.bend = 1.2f;
    r.low.max_bend = 40.f;          r.high.max_bend = 40.f;
    r.low.head_lead = 0.2f;         r.high.head_lead = 1.f;
    r.low.bank = 0.2f;              r.high.bank = 0.8f;
    r.low.turn_step = 1.f;          r.high.turn_step = 1.f;
    r.low.ik = 1.f;                 r.high.ik = 1.f;
    return r;
}

GaitRanges GaitRanges::parse(const nlohmann::json& j, GaitRanges r) {
    if(!j.is_object()) return r;
    nlohmann::json low = r.low.json(), high = r.high.json();
    for(auto& name : GaitStyle::names()) {
        auto it = j.find(name);
        if(it == j.end()) continue;
        if(it->is_array() && it->size() == 2) {
            low[name] = it->at(0);
            high[name] = it->at(1);
        } else if(it->is_number()) {
            low[name] = high[name] = *it;
        }
    }
    r.low = GaitStyle::parse(low);
    r.high = GaitStyle::parse(high);
    pair(j, "speed", r.speed);
    pair(j, "radius", r.radius);
    pair(j, "scale", r.scale);
    pair(j, "segment", r.segment);
    r.straight = j.value("straight", r.straight);
    r.soft = j.value("soft", r.soft);
    return r;
}

nlohmann::json GaitRanges::json() const {
    nlohmann::json j = nlohmann::json::object();
    auto lows = low.values(), highs = high.values();
    for(size_t i = 0; i < lows.size(); i++) j[GaitStyle::names()[i]] = { lows[i], highs[i] };
    j["speed"] = { speed[0], speed[1] };
    j["radius"] = { radius[0], radius[1] };
    j["scale"] = { scale[0], scale[1] };
    j["segment"] = { segment[0], segment[1] };
    j["straight"] = straight;
    j["soft"] = soft;
    return j;
}

GaitDemoResult dy::writeGaitDemos(AnimalLibrary& library, const GaitDemoOptions& options, const std::filesystem::path& directory) {

    GaitDemoResult result;
    int index = library.find(options.species);
    auto species = index >= 0 ? library.species((uint32_t) index) : nullptr;
    if(!species) {
        result.error = "no animal " + options.species + " in the library";
        return result;
    }
    int clip = options.gait == "run" ? species->run : options.gait == "trot" ? species->trot : species->walk;
    if(clip < 0) {
        result.error = options.species + " has no " + options.gait;
        return result;
    }
    const GaitModel model = analyseGait(*species, clip);
    if(!model.valid) {
        result.error = options.species + " / " + species->clips[clip].name + " : " + model.problem;
        return result;
    }
    const auto& entry = library.entries[index];
    const float height = std::max(entry.height, 0.01f), length = std::max(entry.length, 0.6f * entry.height);
    const float clip_speed = species->clips[clip].speed > 0.02f ? species->clips[clip].speed : model.speed;

    std::shared_ptr<TerrainGenerator> generator;
    std::shared_ptr<GroundField> ground;
    try {
        if(options.terrain) {
            generator = std::make_shared<TerrainGenerator>(options.terrain_seed);
            ground = std::make_shared<TerrainField>(generator, std::make_shared<MarchingCubes>());
        }
    } catch(const std::exception& e) {
        result.error = std::string("terrain : ") + e.what();
        return result;
    }
    if(!ground) ground = std::make_shared<PlaneField>();

    const size_t joints = species->parents.size(), legs = model.legs.size();
    const int frames = std::max((int) std::lround(options.seconds * (float) options.fps), 1);
    const std::vector<std::string> sample_names = [&] {
        std::vector<std::string> names = GaitStyle::names();
        names.insert(names.end(), { "scale", "start_x", "start_y", "start_z", "start_yaw" });
        return names;
    }();
    result.sample_floats = sample_names.size();
    result.frame_floats = 11 + 5 * legs + 7 * joints;

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::string base = options.species + "_" + options.gait;
    result.header = directory / (base + ".json");
    result.data = directory / (base + ".bin");
    std::ofstream data(result.data, std::ios::binary);
    if(!data) {
        result.error = "cannot write " + result.data.string();
        return result;
    }

    const float dt = 1.f / 60.f;
    const int every = std::max(60 / std::max(options.fps, 1), 1);
    std::vector<float> slides;
    double planted = 0., foot_frames = 0., speed_sum = 0.;
    std::vector<float> record;

    for(int sample = 0; sample < options.samples; sample++) {
        uint32_t rng = (options.seed * 0x9E3779B9u) ^ ((uint32_t) sample * 0x85EBCA6Bu + 0x1B873593u);
        unit(rng);

        // The style of this walk
        GaitStyle style;
        {
            nlohmann::json j = nlohmann::json::object();
            auto lows = options.ranges.low.values(), highs = options.ranges.high.values();
            bool soft = unit(rng) < options.ranges.soft;
            for(size_t i = 0; i < lows.size(); i++) {
                const std::string& name = GaitStyle::names()[i];
                float value = lows[i] + (highs[i] - lows[i]) * unit(rng);
                if(inHeights(name)) value *= height;
                if(name == "soft_depth" && !soft) value = 0.f;
                j[name] = value;
            }
            style = GaitStyle::parse(j);
        }
        const float scale = between(rng, options.ranges.scale);

        // Where it starts : on gentle land when there is a terrain
        Transform transform;
        transform.scale = glm::vec3(scale);
        float yaw = unit(rng) * 2.f * pi;
        if(generator) {
            for(int attempt = 0; attempt < 40; attempt++) {
                glm::vec2 p((unit(rng) - 0.5f) * 2000.f, (unit(rng) - 0.5f) * 2000.f);
                float dx = (generator->surfaceHeight(p.x + 6.f, p.y) - generator->surfaceHeight(p.x - 6.f, p.y)) / 12.f;
                float dz = (generator->surfaceHeight(p.x, p.y + 6.f) - generator->surfaceHeight(p.x, p.y - 6.f)) / 12.f;
                transform.position = glm::vec3(p.x, 0.f, p.y);
                if(std::sqrt(dx * dx + dz * dz) < 0.2f) break;
            }
        }
        transform.rotation = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f));

        Animal animal;
        animal.species = (uint32_t) index;
        animal.clip = clip;
        animal.time = unit(rng) * model.period;
        AnimalGait gait;
        gait.style = style;
        gait.model = clip;
        gait.weight = 1.f;
        AnimalPose pose;
        std::vector<JointPose> scratch;
        AnimalWalker walker;
        walker.path = AnimalWalker::Path::Free;
        const bool grounded = std::abs(species->min.y) < 0.2f * (species->max.y - species->min.y);
        const float sole = grounded ? species->min.y * scale : 0.f;

        float until = 0.f, wanted_speed = clip_speed * scale, wanted_turn = 0.f;
        std::vector<glm::vec3> last_foot(legs);
        std::vector<bool> was_planted(legs, false);
        const int warm = (int) std::lround(options.warmup * 60.f);
        const size_t start = record.size();
        record.insert(record.end(), sample_names.size(), 0.f);

        for(int tick = 0; tick < warm + frames * every; tick++) {
            const float time = (float) tick * dt;
            if(time >= until) {
                until = time + between(rng, options.ranges.segment);
                wanted_speed = clip_speed * scale * between(rng, options.ranges.speed);
                float radius = between(rng, options.ranges.radius) * length * scale;
                wanted_turn = unit(rng) < options.ranges.straight ? 0.f : (unit(rng) < 0.5f ? -1.f : 1.f) / std::max(radius, 0.1f);
            }
            // A second to go from one speed and one turn to the next
            walker.speed += (wanted_speed - walker.speed) * std::min(2.f * dt, 1.f);
            walker.turn += (wanted_turn - walker.turn) * std::min(2.f * dt, 1.f);
            if(tick == 0) {
                walker.speed = wanted_speed;
                walker.turn = wanted_turn;
            }
            stepWalker(walker, transform, length * scale, sole, ground.get(), dt);

            // As AnimalAnimation then AnimalGait do
            animal.time += dt * animal.rate;
            evaluate(*species, animal, pose, scratch);
            const float stride = std::max(model.speed, 0.05f * model.height) * model.period * scale * std::max(style.stride, 0.1f);
            gait.period = std::clamp(stride / std::max(std::abs(gait.speed), 1e-3f), 0.55f * model.period, 1.8f * model.period);
            animal.rate = model.period / gait.period;
            proceduralWalk(*species, model, transform, animal.time, dt, ground.get(), gait, pose);

            if(tick == warm) {
                float* values = &record[start];
                auto style_values = style.values();
                std::copy(style_values.begin(), style_values.end(), values);
                values += style_values.size();
                const float begin[5] = { scale, transform.position.x, transform.position.y, transform.position.z, walker.yaw };
                std::copy(begin, begin + 5, values);
            }
            if(tick < warm) continue;

            const glm::mat4 to_world = transform.matrix();
            for(size_t i = 0; i < legs; i++) {
                glm::vec3 joint = glm::vec3(to_world * pose.world[model.legs[i].chain.back()][3]);
                if(gait.feet[i].planted && was_planted[i]) slides.push_back(glm::length(glm::vec2(joint.x - last_foot[i].x, joint.z - last_foot[i].z)) / dt);
                was_planted[i] = gait.feet[i].planted;
                last_foot[i] = joint;
            }
            if((tick - warm) % every) continue;

            record.push_back((float) (tick - warm) * dt);
            record.insert(record.end(), { transform.position.x, transform.position.y, transform.position.z });
            record.insert(record.end(), { transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w });
            record.insert(record.end(), { gait.speed, gait.turn, gait.phase });
            for(size_t i = 0; i < legs; i++) {
                const auto& foot = gait.feet[i];
                record.insert(record.end(), { foot.planted ? 1.f : 0.f, foot.now.x, foot.now.y, foot.now.z, foot.planted ? foot.ground : foot.target.y });
                planted += foot.planted;
                foot_frames += 1.;
            }
            for(size_t j = 0; j < joints; j++) {
                const glm::vec3 p(pose.world[j][3]);
                const glm::quat q = rotationOf(pose.world[j]);
                record.insert(record.end(), { p.x, p.y, p.z, q.x, q.y, q.z, q.w });
            }
            speed_sum += gait.speed;
        }

        data.write(reinterpret_cast<const char*>(record.data()), (std::streamsize) (record.size() * sizeof(float)));
        record.clear();
    }
    data.close();

    nlohmann::json header = {
        { "format_version", 1 },
        { "species", options.species },
        { "gait", options.gait },
        { "clip", species->clips[clip].name },
        { "clip_speed", clip_speed },
        { "period", model.period },
        { "height", height },
        { "length", length },
        { "fps", 60 / every },
        { "seconds", options.seconds },
        { "samples", options.samples },
        { "frames", frames },
        { "seed", options.seed },
        { "terrain", options.terrain },
        { "ranges", options.ranges.json() },
        { "lengths_in_heights", { "clearance", "soft_depth", "crouch", "spread" } },
        { "sample_floats", result.sample_floats },
        { "frame_floats", result.frame_floats },
        { "sample_values", sample_names },
        { "frame_layout", {
            { { "name", "time" }, { "size", 1 } },
            { { "name", "root_position" }, { "size", 3 }, { "space", "world" } },
            { { "name", "root_rotation" }, { "size", 4 }, { "order", "x y z w" } },
            { { "name", "speed" }, { "size", 1 }, { "unit", "m/s along the heading" } },
            { { "name", "turn" }, { "size", 1 }, { "unit", "rad/s" } },
            { { "name", "phase" }, { "size", 1 }, { "unit", "of the cycle, 0 to 1" } },
            { { "name", "legs" }, { "size", 5 * legs }, { "each", { "planted", "foot_x", "foot_y", "foot_z", "ground" } }, { "space", "world" } },
            { { "name", "joints" }, { "size", 7 * joints }, { "each", { "x", "y", "z", "qx", "qy", "qz", "qw" } }, { "space", "model : metres, y up, the animal looks down -z" } } } },
    };
    for(size_t j = 0; j < joints; j++) header["joints"].push_back({ { "name", species->joint_names[j] }, { "parent", species->parents[j] } });
    for(auto& leg : model.legs) {
        header["legs"].push_back({ { "foot", species->joint_names[leg.chain.back()] }, { "chain", leg.chain }, { "front", leg.front }, { "left", leg.left },
            { "touch", leg.touch }, { "lift", leg.lift }, { "sole", leg.sole }, { "reach", leg.reach } });
    }
    std::ofstream(result.header) << header.dump(1);

    result.samples = options.samples;
    result.frames = frames;
    result.joints = (int) joints;
    result.legs = (int) legs;
    result.planted = foot_frames > 0. ? (float) (planted / foot_frames) : 0.f;
    result.mean_speed = (float) (speed_sum / std::max((double) options.samples * frames, 1.));
    if(!slides.empty()) {
        std::sort(slides.begin(), slides.end());
        result.slide = slides[(size_t) (0.99f * (float) (slides.size() - 1))];
    }
    result.ok = true;
    return result;

}
