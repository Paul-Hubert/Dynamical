#include "train_host.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <thread>

#include "logic/agents/agent_io.h"
#include "logic/animals/animal_agent.h"
#ifdef DYNAMICAL_ONNX
#include "onnx/policy_runner.h"
#endif
#include "process_control.h"
#include "util/log.h"

using namespace dy;

// The animals' side of the training host (docs/physical-agents/reports/animal-agents.md): what a recording was
// checked for, and the eval of a policy

bool TrainHost::animalWorlds() {
    return runner.worldCount() > 0 && runner.world(0).reg.ctx().find<AnimalBot>() != nullptr;
}

// A recording whose walk cannot be played (no such clip, a clip the gait analysis does not read) never ends: say why
std::string TrainHost::animalDemoProblem() {
    if(!animalWorlds()) return {};
    auto* walk = runner.world(0).reg.ctx().find<AnimalWalkState>();
    if(walk && !walk->ready) return walk->problem.empty() ? std::string("the walk is not ready") : walk->problem;
    return {};
}

// var.replay=<prefix> on a recording: the segments at every recorded decision, as the eval writes them
void TrainHost::recordAnimalReplay() {
    if(!animalWorlds()) return;
    const auto& vars = runner.scene().resolved["variables"];
    if(!vars.contains("replay") || vars["replay"]["default"].get<std::string>().empty()) return;
    const auto& bot = runner.world(0).reg.ctx().get<AnimalBot>();
    for(size_t s = 0; s < bot.position.size(); s++) {
        const glm::vec3 p = bot.position[s];
        const glm::quat q = bot.rotation[s];
        animal_replay.insert(animal_replay.end(), { p.x, p.y, p.z, q.x, q.y, q.z, q.w });
    }
}

void TrainHost::writeAnimalChecks() {
    auto& reg = runner.world(0).reg;
    auto* checks = reg.ctx().find<AnimalDemoChecks>();
    auto* bot = reg.ctx().find<AnimalBot>();
    if(!checks || !bot) return;
    if(!animal_replay.empty()) {
        const std::string prefix = runner.scene().resolved["variables"]["replay"]["default"].get<std::string>();
        const size_t segments = bot->position.size();
        std::ofstream(prefix + ".replay.bin", std::ios::binary).write(reinterpret_cast<const char*>(animal_replay.data()), (std::streamsize) (animal_replay.size() * sizeof(float)));
        const auto& clock = reg.ctx().get<WorldClock>();
        nlohmann::json header = { { "species", bot->body->species }, { "segments", segments }, { "frames", animal_replay.size() / (segments * 7) },
            { "dt", clock.dt * clock.decision_period }, { "walk_decisions", bot->spawn.recording_decisions }, { "model", "the procedural walk" } };
        std::ofstream(prefix + ".replay.json") << header.dump(1) << "\n";
    }
    if(!checks->slides.empty()) {
        std::vector<float> sorted(checks->slides);
        std::sort(sorted.begin(), sorted.end());
        checks->foot_slide = sorted[(size_t) (0.99f * (float) (sorted.size() - 1))];
    }
    if(checks->walk_commanded > 0.) checks->speed_error = std::max(checks->speed_error, (float) std::abs(checks->walk_progress / checks->walk_commanded - 1.));
    nlohmann::json j = checks->json();
    const AnimalBody& body = *bot->body;
    j["species"] = body.species;
    j["walks"] = bot->walks;
    j["height"] = body.height;
    j["walk_speed"] = body.walk_speed;
    j["observations"] = bot->observationSize();
    j["actions"] = bot->actionSize();
    if(checks->limit_joint >= 0) j["limit_joint_name"] = body.def.joints[checks->limit_joint].name;
    if(checks->fall_segment >= 0) j["fall_segment_name"] = body.def.segments[checks->fall_segment].name;
    auto segment_name = [&](int s) { return s >= 0 ? body.def.segments[s].name : std::string(); };
    j["axis_segment_name"] = segment_name(checks->axis_segment);
    j["velocity_segment_name"] = segment_name(checks->velocity_segment);
    j["lowest_segment_name"] = segment_name(checks->lowest_segment);
    j["gait"] = bot->spawn.gait;
    j["ranges"] = bot->spawn.ranges;
    dy::log(dy::info) << "demo checks " << j.dump() << "\n";
    const auto& vars = runner.scene().resolved["variables"];
    std::string path = vars.contains("checks") ? vars["checks"]["default"].get<std::string>() : "";
    if(!path.empty()) std::ofstream(path) << j.dump(1) << "\n";
}

int TrainHost::evalAnimals() {

#ifndef DYNAMICAL_ONNX
    dy::log(dy::error) << "mode=eval needs a build with ONNX Runtime\n";
    return 1;
#else
    const auto& vars = runner.scene().resolved["variables"];
    std::string model = vars.contains("model") ? vars["model"]["default"].get<std::string>() : "";
    std::string out_path = vars.contains("out") ? vars["out"]["default"].get<std::string>() : "";
    int episodes = vars.contains("episodes") ? vars["episodes"]["default"].get<int>() : 100;
    bool stochastic = vars.contains("stochastic") && vars["stochastic"]["default"].get<int>() != 0;
    auto& first = runner.world(0).reg.ctx().get<AnimalBot>();
    PolicyHandle policy;
    // A walker with orders has its order sensor next to its observation: the handle takes what the agent has
    if(!model.empty()) policy = makePolicyHandle(model, runner.world(0).reg.ctx().get<AgentIO>(), !stochastic);
    else dy::log(dy::info) << "eval without var.model: zero actions (the standing pose held)\n";
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        if(policy.runner || policy.agent) reg.ctx().insert_or_assign(policy);
        reg.ctx().get<AgentIO>().reset_requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
    int period = (int) runner.world(0).reg.ctx().get<WorldClock>().decision_period;
    tf::Executor executor(std::max(1u, std::thread::hardware_concurrency() / 2));

    // var.replay=<prefix>: the segments of world 0 at every decision, for `replay_seconds` (a film without the viewer):
    // <prefix>.replay.json and .bin (float32: per decision, per segment, position x y z and rotation x y z w)
    std::string replay = vars.contains("replay") ? vars["replay"]["default"].get<std::string>() : "";
    const double replay_seconds = vars.contains("replay_seconds") ? vars["replay_seconds"]["default"].get<double>() : 30.;
    std::vector<float> replay_data;
    std::vector<int> replay_resets;
    const double decision_seconds = runner.world(0).reg.ctx().get<WorldClock>().dt * period;
    const size_t replay_floats = replay.empty() ? 0 : (size_t) (replay_seconds / decision_seconds) * first.position.size() * 7;

    std::vector<AnimalEpisode> done;
    while((int) done.size() < episodes || replay_data.size() < replay_floats) {
        runner.step(period, &executor);
        if(stopRequested()) return 1;
        if(replay_data.size() < replay_floats) {
            if(runner.world(0).reg.ctx().get<AgentIO>().done) replay_resets.push_back((int) (replay_data.size() / (first.position.size() * 7)));
            for(size_t s = 0; s < first.position.size(); s++) {
                const glm::vec3 p = first.position[s];
                const glm::quat q = first.rotation[s];
                replay_data.insert(replay_data.end(), { p.x, p.y, p.z, q.x, q.y, q.z, q.w });
            }
        }
        for(size_t i = 0; i < runner.worldCount(); i++) {
            auto& bot = runner.world(i).reg.ctx().get<AnimalBot>();
            for(auto& e : bot.episodes) done.push_back(e);
            bot.episodes.clear();
        }
    }

    const AnimalBody& body = *first.body;
    const double dt = runner.world(0).reg.ctx().get<WorldClock>().dt;
    double ticks = 0., falls = 0., distance = 0., progress = 0., commanded = 0., heading = 0., low = 0., touch = 0.;
    std::map<std::string, int> touched;
    for(auto& e : done) {
        ticks += e.ticks;
        falls += e.fell ? 1 : 0;
        distance += e.distance;
        progress += e.progress;
        commanded += e.commanded;
        heading += e.heading_error;
        if(e.fell && e.cause == 1) low += 1;
        if(e.fell && e.cause == 2) {
            touch += 1;
            if(e.segment >= 0) touched[body.def.segments[e.segment].name]++;
        }
    }
    const double n = (double) done.size();
    nlohmann::json summary = {
        { "species", body.species }, { "model", model }, { "episodes", done.size() }, { "fall_rate", falls / n },
        { "falls_trunk_low", low }, { "falls_body_touch", touch }, { "touched", touched },
        { "mean_length_seconds", ticks * dt / n }, { "episode_seconds", first.params.max_frames_60hz / 60. },
        { "mean_speed", distance / std::max(ticks * dt, 1e-9) }, { "progress_speed", progress / std::max(ticks * dt, 1e-9) },
        { "commanded_speed", commanded / std::max(ticks * dt, 1e-9) }, { "speed_ratio", commanded > 0. ? progress / commanded : 0. },
        { "walk_speed", body.walk_speed }, { "falls_per_100_lengths", distance > 0. ? falls / (distance / (100. * body.length)) : 0. },
        { "mean_heading_error_deg", heading / n * 57.2957795 }, { "stochastic", stochastic },
        { "observations", first.observationSize() }, { "actions", first.actionSize() } };
    // What it does when its command stands, walks and runs
    summary["run_speed"] = body.run_speed;
    static const char* const kinds[3] = { "idle", "walk", "run" };
    for(int k = 0; k < 3; k++) {
        double seconds = 0., covered = 0., along = 0., asked = 0.;
        for(auto& e : done) {
            seconds += e.kind_seconds[k];
            covered += e.kind_distance[k];
            along += e.kind_progress[k];
            asked += e.kind_commanded[k];
        }
        if(seconds <= 0.) continue;
        summary[kinds[k]] = { { "seconds", seconds }, { "mean_speed", covered / seconds }, { "progress_speed", along / seconds },
            { "commanded_speed", asked / seconds }, { "speed_ratio", asked > 0. ? along / asked : 0. } };
    }
    if(!replay.empty()) {
        std::ofstream(replay + ".replay.bin", std::ios::binary).write(reinterpret_cast<const char*>(replay_data.data()), (std::streamsize) (replay_data.size() * sizeof(float)));
        nlohmann::json header = { { "species", body.species }, { "segments", first.position.size() }, { "frames", replay_data.size() / (first.position.size() * 7) },
            { "dt", decision_seconds }, { "episode_ends", replay_resets }, { "model", model } };
        std::ofstream(replay + ".replay.json") << header.dump(1) << "\n";
    }
    dy::log(dy::info) << "eval " << summary.dump() << "\n";
    if(!out_path.empty()) std::ofstream(out_path) << summary.dump(2) << "\n";
    return 0;
#endif

}
