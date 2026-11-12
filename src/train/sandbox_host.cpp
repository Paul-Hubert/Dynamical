#include "train_host.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <thread>

#include "logic/agents/humanbot.h"
#include "logic/sandbox/sandbox.h"
#include "process_control.h"
#include "util/log.h"
#include "util/profile_workers.h"

using namespace dy;

// The sandbox's side of the training host (docs/action-agents.md): mode=eval on a scene of sandbox worlds runs them
// for var.seconds with whatever answers their agents (the scene's Policies, the scripted minds) and writes what the
// animals did; mode=signals prints what a reward file can read

bool TrainHost::sandboxWorlds() {
    return runner.worldCount() > 0 && runner.world(0).reg.ctx().find<Sandbox>() != nullptr;
}

int TrainHost::signals() {
    std::string line;
    for(const std::string& name : sandboxSignalNames()) line += (line.empty() ? "" : " ") + name;
    dy::log(dy::info) << "signals " << line << "\n";
    return 0;
}

int TrainHost::evalSandbox() {

    const auto& vars = runner.scene().resolved["variables"];
    const double seconds = vars.contains("seconds") ? vars["seconds"]["default"].get<double>() : 300.;
    const std::string out_path = vars.contains("out") ? vars["out"]["default"].get<std::string>() : "";
    const std::string replay = vars.contains("replay") ? vars["replay"]["default"].get<std::string>() : "";

    const auto& clock = runner.world(0).reg.ctx().get<WorldClock>();
    const int period = (int) clock.decision_period;
    const int64_t windows = (int64_t) std::llround(seconds / (clock.dt * period));
    tf::Executor executor(std::max(1u, std::min((unsigned) runner.worldCount(), std::thread::hardware_concurrency() / 2)), profile::namedWorkers());

    // Ten times a second, the animals of world 0 from above: per frame a count, then per animal 12 floats
    // (group, species, dead, male, x, z, heading, energy, health, size, mode, where the head looks)
    const int replay_every = std::max(1, (int) std::lround(0.1 / (clock.dt * period)));
    std::vector<float> replay_data;
    int replay_frames = 0;
    nlohmann::json series = nlohmann::json::array();
    const int64_t series_every = std::max<int64_t>(1, (int64_t) std::llround(10. / (clock.dt * period)));

    for(int64_t w = 0; w < windows; w++) {
        runner.step(period, &executor);
        if(stopRequested()) return 1;
        if(!replay.empty() && w % replay_every == 0) {
            auto& reg = runner.world(0).reg;
            std::vector<entt::entity> list;
            for(auto e : reg.view<SandboxAnimal, AnimalBot>()) list.push_back(e);
            std::sort(list.begin(), list.end());
            replay_data.push_back((float) list.size());
            const auto& sb = reg.ctx().get<Sandbox>();
            for(auto e : list) {
                const auto& a = reg.get<SandboxAnimal>(e);
                const auto& bot = reg.get<AnimalBot>(e);
                HeadingFrame frame = headingFrame(bot.rotation[0]);
                const glm::vec3 head = sb.puppet ? bot.askedHeadDirection(bot.head) : bot.headDirection();
                replay_data.insert(replay_data.end(), { (float) a.group, (float) a.species, a.dead ? 1.f : 0.f, a.male ? 1.f : 0.f, bot.position[0].x, bot.position[0].z,
                    std::atan2(-frame.x.x, -frame.x.z), a.energy, a.health, sb.species[a.species].stage_scale[a.stage], (float) bot.mode, std::atan2(-head.x, -head.z) });
            }
            replay_frames++;
        }
        if(w % series_every == series_every - 1) {
            nlohmann::json point = { { "seconds", (double) (w + 1) * clock.dt * period } };
            for(size_t s = 0; s < runner.world(0).reg.ctx().get<Sandbox>().species.size(); s++) {
                double alive = 0., energy = 0.;
                std::string id;
                for(size_t i = 0; i < runner.worldCount(); i++) {
                    auto& reg = runner.world(i).reg;
                    const auto& sb = reg.ctx().get<Sandbox>();
                    id = sb.species[s].life.id;
                    alive += sb.species[s].alive;
                    for(auto [e, a] : reg.view<SandboxAnimal>().each()) if(!a.dead && a.species == (int) s) energy += a.energy;
                }
                point[id] = { { "alive", alive / (double) runner.worldCount() }, { "energy", alive > 0. ? energy / alive : 0. } };
            }
            series.push_back(point);
        }
    }

    nlohmann::json summary = { { "scene", runner.scene().name }, { "worlds", runner.worldCount() }, { "seconds", seconds } };
    const auto& first = runner.world(0).reg.ctx().get<Sandbox>();
    summary["puppet"] = first.puppet;
    summary["life_scale"] = first.params.life_scale;
    double grass = 0.;
    uint64_t contacts = 0, blows = 0;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        const auto& sb = runner.world(i).reg.ctx().get<Sandbox>();
        grass += sb.grass.standing();
        contacts += sb.contacts;
        blows += sb.blows;
    }
    summary["grass_standing"] = grass / (double) runner.worldCount();
    summary["contacts"] = contacts;
    summary["blows"] = blows;
    for(size_t s = 0; s < first.species.size(); s++) {
        double alive = 0., born = 0., spawned = 0., starved = 0., killed = 0., aged = 0., kills = 0., falls = 0., energy = 0., health = 0., lifetime = 0., lives = 0.;
        double work = 0., moved = 0., ticks = 0.;
        for(size_t i = 0; i < runner.worldCount(); i++) {
            auto& reg = runner.world(i).reg;
            const SandboxSpecies& species = reg.ctx().get<Sandbox>().species[s];
            alive += species.alive;
            born += (double) species.born;
            spawned += (double) species.spawned;
            starved += (double) species.starved;
            killed += (double) species.killed;
            aged += (double) species.aged;
            kills += (double) species.kills;
            falls += (double) species.falls;
            lifetime += species.lifetime;
            lives += (double) species.lives;
            work += species.work;
            moved += species.moved;
            ticks += (double) species.ticks;
            for(auto [e, a] : reg.view<SandboxAnimal>().each()) {
                if(a.dead || a.species != (int) s) continue;
                energy += a.energy;
                health += a.health;
            }
        }
        const double n = (double) runner.worldCount();
        summary["species"][first.species[s].life.id] = {
            { "alive", alive / n }, { "born", born / n }, { "brought_in", spawned / n }, { "starved", starved / n }, { "killed", killed / n }, { "old", aged / n },
            { "kills", kills / n }, { "falls", falls / n }, { "falls_per_animal_minute", alive > 0. ? falls / n / std::max(alive / n, 1.) / (seconds / 60.) : 0. },
            { "mean_energy", alive > 0. ? energy / alive : 0. }, { "mean_health", alive > 0. ? health / alive : 0. },
            { "mean_lifetime", lives > 0. ? lifetime / lives : 0. }, { "deaths", lives / n },
            // Positive mechanical power over the reference (0.1 m g v_walk): what a store pays for, next to the basal rate
            { "mean_work", ticks > 0. ? work / ticks : 0. }, { "mean_speed", ticks > 0. ? moved / (ticks * clock.dt) : 0. } };
    }
    summary["series"] = series;

    if(!replay.empty()) {
        std::ofstream(replay + ".sandbox.bin", std::ios::binary).write(reinterpret_cast<const char*>(replay_data.data()), (std::streamsize) (replay_data.size() * sizeof(float)));
        nlohmann::json header = { { "frames", replay_frames }, { "dt", replay_every * clock.dt * period }, { "arena", first.params.arena },
            { "record", { "group", "species", "dead", "male", "x", "z", "heading", "energy", "health", "size", "mode", "head" } },
            { "modes", { "move", "feed", "attack", "mate", "rest" } } };
        for(const auto& s : first.species) header["species"].push_back({ { "id", s.life.id }, { "predator", s.life.predator }, { "length", s.bodies.back()->length }, { "sight", s.life.sight }, { "fov", s.life.fov } });
        // The grass as it stood at the start and stands at the end, a value a cell
        const int side = first.grass.size();
        const float cell = 2.f * first.params.arena / (float) side;
        std::vector<float> full((size_t) side * side), now((size_t) side * side);
        for(int z = 0; z < side; z++) {
            for(int x = 0; x < side; x++) {
                const float px = -first.params.arena + (x + 0.5f) * cell, pz = -first.params.arena + (z + 0.5f) * cell;
                full[(size_t) z * side + x] = first.grass.full(px, pz);
                now[(size_t) z * side + x] = first.grass.height(px, pz);
            }
        }
        header["grass_side"] = side;
        std::ofstream grass_file(replay + ".grass.bin", std::ios::binary);
        grass_file.write(reinterpret_cast<const char*>(full.data()), (std::streamsize) (full.size() * sizeof(float)));
        grass_file.write(reinterpret_cast<const char*>(now.data()), (std::streamsize) (now.size() * sizeof(float)));
        std::ofstream(replay + ".sandbox.json") << header.dump(1) << "\n";
    }

    nlohmann::json brief = summary;
    brief.erase("series");
    dy::log(dy::info) << "eval " << brief.dump() << "\n";
    if(!out_path.empty()) std::ofstream(out_path) << summary.dump(1) << "\n";
    return 0;

}
