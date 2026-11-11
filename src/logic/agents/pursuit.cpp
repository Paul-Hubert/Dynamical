#include "pursuit.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "agent_io.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"

using namespace dy;

namespace {

constexpr float two_pi = 6.28318530717959f;

void place(PursuitDot& dot, const PursuitParams& p, WorldRng& rng) {
    dot.position = glm::vec2((float) rng.uniform(-p.field, p.field), (float) rng.uniform(-p.field, p.field));
    dot.velocity = glm::vec2(0.f);
    dot.yaw = (float) rng.uniform(0., two_pi);
    dot.stamina = 1.f;
    dot.speed = dot.turn = 0.f;
    dot.sprint = false;
    dot.decisions = 0;
    dot.reward = 0.;
    dot.catches = 0;
    dot.fresh = true;
    dot.caught = false;
    dot.ended = false;
}

entt::entity spawn(entt::registry& reg, const PursuitParams& p, bool chaser) {
    auto& rng = reg.ctx().get<WorldRng>();
    entt::entity e = reg.create();
    auto& dot = reg.emplace<PursuitDot>(e);
    dot.chaser = chaser;
    place(dot, p, rng);
    dot.phase = (int) (rng.uniform() * p.runner_every);
    auto& io = reg.emplace<AgentIO>(e);
    io.configure(chaser ? "Chaser" : "Runner", PursuitObs::size, pursuit_actions);
    io.addBranch(2);
    io.addBuffer("Others", p.others, PursuitObs::row);
    return e;
}

void populate(entt::registry& reg, const PursuitParams& p) {
    std::vector<entt::entity> old;
    for(auto e : reg.view<PursuitDot>()) old.push_back(e);
    for(auto e : old) reg.destroy(e);
    for(int i = 0; i < p.chasers; i++) spawn(reg, p, true);
    for(int i = 0; i < p.runners; i++) spawn(reg, p, false);
}

// Dots in entity order: what a view gives changes with every destroyed entity
std::vector<entt::entity> dots(entt::registry& reg) {
    std::vector<entt::entity> list;
    for(auto e : reg.view<PursuitDot, AgentIO>()) list.push_back(e);
    std::sort(list.begin(), list.end());
    return list;
}

void observe(entt::registry& reg, const std::vector<entt::entity>& all, entt::entity self, const PursuitParams& p) {

    const auto& dot = reg.get<PursuitDot>(self);
    auto& io = reg.get<AgentIO>(self);
    const float top = p.speed * p.sprint;

    io.observation[PursuitObs::position] = dot.position.x / p.field;
    io.observation[PursuitObs::position + 1] = dot.position.y / p.field;
    io.observation[PursuitObs::heading] = std::sin(dot.yaw);
    io.observation[PursuitObs::heading + 1] = std::cos(dot.yaw);
    io.observation[PursuitObs::speed] = glm::length(dot.velocity) / top;
    io.observation[PursuitObs::stamina] = dot.stamina;

    struct Seen { float distance; entt::entity entity; };
    std::vector<Seen> seen;
    for(auto e : all) {
        if(e == self) continue;
        const auto& other = reg.get<PursuitDot>(e);
        if(other.caught) continue;
        seen.push_back({ glm::distance(other.position, dot.position), e });
    }
    std::stable_sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.distance < b.distance; });

    auto& buffer = io.sensors[0];
    buffer.clear();
    const glm::vec2 f = dot.forward(), l = dot.left();
    for(const Seen& s : seen) {
        float* row = buffer.add();
        if(!row) break;
        const auto& other = reg.get<PursuitDot>(s.entity);
        glm::vec2 offset = other.position - dot.position, velocity = other.velocity - dot.velocity;
        row[PursuitObs::other_position] = glm::dot(offset, f) / (2.f * p.field);
        row[PursuitObs::other_position + 1] = glm::dot(offset, l) / (2.f * p.field);
        row[PursuitObs::other_velocity] = glm::dot(velocity, f) / top;
        row[PursuitObs::other_velocity + 1] = glm::dot(velocity, l) / top;
        row[PursuitObs::other_chaser] = other.chaser ? 1.f : 0.f;
        row[PursuitObs::other_runner] = other.chaser ? 0.f : 1.f;
    }

}

}

glm::vec2 PursuitDot::forward() const {
    return glm::vec2(-std::sin(yaw), -std::cos(yaw));
}

glm::vec2 PursuitDot::left() const {
    return glm::vec2(-std::cos(yaw), std::sin(yaw));
}

void dy::pursuitHeuristic(AgentIO& io, bool chaser) {
    io.action.assign(pursuit_actions, 0.f);
    io.discrete.assign(io.branches.size(), 0);
    const auto& buffer = io.sensors[0];
    const int wanted = chaser ? PursuitObs::other_runner : PursuitObs::other_chaser;
    for(int r = 0; r < buffer.count; r++) {
        const float* row = buffer.data.data() + (size_t) r * buffer.size;
        if(row[wanted] < 0.5f) continue;
        float forward = row[PursuitObs::other_position], left = row[PursuitObs::other_position + 1];
        if(!chaser) {
            forward = -forward;
            left = -left;
        }
        io.action[0] = 1.f;
        io.action[1] = std::clamp(2.f * std::atan2(left, forward), -1.f, 1.f);
        if(!io.discrete.empty()) io.discrete[0] = std::hypot(forward, left) < 0.2f ? 1 : 0;
        return;
    }
    // Nobody to chase or to flee: walk on
    io.action[0] = chaser ? 1.f : 0.f;
}



PursuitSys::PursuitSys(entt::registry& reg, const nlohmann::json& j) : System(reg) {
    PursuitParams& p = params;
    p.field = j.value("field", p.field);
    p.chasers = j.value("chasers", p.chasers);
    p.runners = j.value("runners", p.runners);
    p.catch_radius = j.value("catch_radius", p.catch_radius);
    p.speed = j.value("speed", p.speed);
    p.sprint = j.value("sprint", p.sprint);
    p.turn_rate = j.value("turn_rate", p.turn_rate);
    p.stamina_drain = j.value("stamina_drain", p.stamina_drain);
    p.stamina_gain = j.value("stamina_gain", p.stamina_gain);
    p.chaser_decisions = j.value("chaser_decisions", p.chaser_decisions);
    p.runner_decisions = j.value("runner_decisions", p.runner_decisions);
    p.runner_every = std::max(1, j.value("runner_every", p.runner_every));
    p.alive_reward = j.value("alive_reward", p.alive_reward);
    p.others = std::max(1, j.value("others", p.others));
    p.runners_still = j.value("runners_still", p.runners_still);
    p.approach_reward = j.value("approach_reward", p.approach_reward);
}

void PursuitSys::preinit() {
    reg.ctx().emplace<PursuitField>().params = params;
    if(!reg.ctx().find<AgentsReset>()) reg.ctx().emplace<AgentsReset>();
}

void PursuitSys::init() {
    populate(reg, params);
}

void PursuitSys::tick(double h) {

    auto& field = reg.ctx().get<PursuitField>();
    const PursuitParams& p = field.params;
    const auto& clock = reg.ctx().get<WorldClock>();
    auto& rng = reg.ctx().get<WorldRng>();
    std::vector<entt::entity> all = dots(reg);

    if(clock.tick % clock.decision_period == 0) {

        // Those whose terminal step went out with the last window
        bool changed = false;
        for(auto e : all) {
            auto& dot = reg.get<PursuitDot>(e);
            if(!dot.ended) continue;
            if(dot.chaser) {
                auto& io = reg.get<AgentIO>(e);
                place(dot, p, rng);
                io.done = io.truncated = false;
                io.reward = 0.f;
                std::fill(io.action.begin(), io.action.end(), 0.f);
                std::fill(io.discrete.begin(), io.discrete.end(), 0);
            } else {
                reg.destroy(e);
                spawn(reg, p, false);
                field.births++;
                changed = true;
            }
        }
        if(changed) all = dots(reg);

        for(auto e : all) {
            auto& dot = reg.get<PursuitDot>(e);
            const auto& io = reg.get<AgentIO>(e);
            float speed = io.action.size() > 0 && std::isfinite(io.action[0]) ? io.action[0] : -1.f;
            float turn = io.action.size() > 1 && std::isfinite(io.action[1]) ? io.action[1] : 0.f;
            dot.speed = std::clamp(0.5f * (speed + 1.f), 0.f, 1.f);
            dot.turn = std::clamp(turn, -1.f, 1.f);
            dot.sprint = !io.discrete.empty() && io.discrete[0] == 1;
            // No decision yet in this episode: at rest
            if(dot.fresh || (p.runners_still && !dot.chaser)) dot.speed = dot.turn = 0.f;
        }

    }

    const float dt = (float) h;
    for(auto e : all) {
        auto& dot = reg.get<PursuitDot>(e);
        if(dot.caught) {
            dot.velocity = glm::vec2(0.f);
            continue;
        }
        bool sprinting = dot.sprint && dot.stamina > 0.f;
        dot.stamina = std::clamp(dot.stamina + (sprinting ? -p.stamina_drain : p.stamina_gain) * dt, 0.f, 1.f);
        dot.yaw = std::fmod(dot.yaw + dot.turn * p.turn_rate * dt + two_pi, two_pi);
        glm::vec2 before = dot.position;
        dot.position += dot.forward() * (dot.speed * p.speed * (sprinting ? p.sprint : 1.f) * dt);
        dot.position = glm::clamp(dot.position, glm::vec2(-p.field), glm::vec2(p.field));
        dot.velocity = (dot.position - before) / dt;
    }

    for(auto c : all) {
        auto& chaser = reg.get<PursuitDot>(c);
        if(!chaser.chaser) continue;
        for(auto r : all) {
            auto& runner = reg.get<PursuitDot>(r);
            if(runner.chaser || runner.caught) continue;
            if(glm::distance(chaser.position, runner.position) >= p.catch_radius) continue;
            runner.caught = true;
            runner.reward -= 1.;
            chaser.reward += 1.;
            chaser.catches++;
            field.catches++;
        }
    }

}



void PursuitObserveSys::tick(double h) {

    auto& field = reg.ctx().get<PursuitField>();
    const PursuitParams& p = field.params;

    if(auto* reset = reg.ctx().find<AgentsReset>(); reset && reset->requested) {
        reset->requested = false;
        populate(reg, p);
    }
    field.windows++;

    std::vector<entt::entity> all = dots(reg);
    for(auto e : all) {

        auto& dot = reg.get<PursuitDot>(e);
        auto& io = reg.get<AgentIO>(e);
        if(dot.ended) continue;
        bool due = dot.fresh || dot.chaser || (field.windows + dot.phase) % p.runner_every == 0;
        if(!due && !dot.caught) continue;

        // How far the nearest runner is now (a chaser's approach reward)
        float nearest = -1.f;
        if(dot.chaser && p.approach_reward != 0.f) {
            for(auto other : all) {
                const auto& runner = reg.get<PursuitDot>(other);
                if(runner.chaser || runner.caught || runner.ended) continue;
                float d = glm::distance(runner.position, dot.position);
                if(nearest < 0.f || d < nearest) nearest = d;
            }
        }

        if(dot.fresh) {
            // The first observation of an episode rates no decision: what happened before it is paid with the next
            dot.fresh = false;
            dot.nearest = nearest;
            io.reward = 0.f;
            io.done = io.truncated = false;
            observe(reg, all, e, p);
            io.requested = true;
            continue;
        } else {
            dot.decisions++;
            bool old = dot.decisions >= (dot.chaser ? p.chaser_decisions : p.runner_decisions);
            io.reward = (float) dot.reward + (dot.chaser || dot.caught ? 0.f : p.alive_reward);
            // Not across a catch: the nearest runner is then another one
            if(dot.chaser && dot.catches == 0 && dot.nearest >= 0.f && nearest >= 0.f) io.reward += p.approach_reward * (dot.nearest - nearest);
            dot.nearest = nearest;
            io.done = dot.caught || old;
            io.truncated = !dot.caught && old;
            if(dot.chaser) {
                io.stats.emplace_back("Pursuit/Catches per decision", (float) dot.catches);
            } else if(io.done) {
                io.stats.emplace_back("Pursuit/Runner decisions", (float) dot.decisions);
                io.stats.emplace_back("Pursuit/Runner caught", dot.caught ? 1.f : 0.f);
            }
            dot.ended = io.done;
        }
        dot.reward = 0.;
        dot.catches = 0;
        observe(reg, all, e, p);
        io.requested = true;

    }

}



void dy::registerPursuit() {
    registerSystem("Pursuit", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<PursuitSys>(reg, params); },
        SystemScope::World });
    registerSystem("PursuitObserve", systemInfo<PursuitObserveSys>(SystemScope::World));
}
