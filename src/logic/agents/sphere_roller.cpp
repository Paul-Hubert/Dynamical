#include "sphere_roller.h"

#include <algorithm>
#include <cmath>

#include "agent_io.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/systems/system_set.h"
#include "physics/physics_sys.h"
#include "physics/terrain_colliders.h"
#include "physics/physics_world.h"
#include "scene/component_registry.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"

using namespace dy;

namespace {

constexpr float pi = 3.14159265358979f;
constexpr float two_pi = 6.28318530717959f;

float wrapPi(float a) {
    a = std::fmod(a + pi, two_pi);
    if(a < 0.f) a += two_pi;
    return a - pi;
}

entt::entity findAgent(entt::registry& reg) {
    auto view = reg.view<SphereAgent, Transform, RigidBody>();
    return view.begin() != view.end() ? *view.begin() : entt::null;
}

float drawSpeed(const SphereAgent& a, WorldRng& rng) {
    if(rng.uniform() < a.stop_chance) return 0.f;
    return (float) rng.uniform(a.speed_range.x, a.speed_range.y);
}

// A new goal for the target to turn and accelerate toward; back toward the spawn point when far from it
void drawGoal(SphereAgent& a, WorldRng& rng, glm::vec3 position) {
    glm::vec2 p(position.x - a.home.x, position.z - a.home.z);
    if(a.home_radius > 0.f && glm::length(p) > a.home_radius) {
        a.goal_heading = std::atan2(-p.y, -p.x) + (float) rng.uniform(-0.25 * pi, 0.25 * pi);
    } else {
        a.goal_heading = a.heading + (float) rng.uniform(-a.max_goal_turn, a.max_goal_turn);
    }
    a.goal_speed = drawSpeed(a, rng);
    a.goal_timer = (float) rng.uniform(a.goal_interval.x, a.goal_interval.y);
}

// Zero when there is no ground below (off the field)
glm::vec3 groundNormal(PhysicsWorld& world, glm::vec3 at, float above) {
    auto hit = world.groundBelow(glm::vec3(at.x, at.y + above, at.z), above + 50.f);
    return hit ? hit->normal : glm::vec3(0.f);
}

void put(std::vector<float>& out, int index, glm::vec3 v) {
    out[index] = v.x;
    out[index + 1] = v.y;
    out[index + 2] = v.z;
}

void park(entt::registry& reg, bool value) {
    if(auto* parked = reg.ctx().find<Parked>()) parked->value = value;
}

}

GoalFrame GoalFrame::fromForward(glm::vec3 forward) {
    GoalFrame f;
    f.y = glm::vec3(0.f, 1.f, 0.f);
    glm::vec3 flat(forward.x, 0.f, forward.z);
    float length = glm::length(flat);
    f.z = length > 1e-6f ? flat / length : glm::vec3(1.f, 0.f, 0.f);
    f.x = glm::cross(f.y, f.z);
    return f;
}

glm::vec3 dy::headingDirection(float heading) {
    return glm::vec3(std::cos(heading), 0.f, std::sin(heading));
}

float dy::sphereReward(glm::vec3 velocity, glm::vec3 target, float scale) {
    glm::vec3 error(velocity.x - target.x, 0.f, velocity.z - target.z);
    return std::exp(-glm::length(error) / std::max(scale, 1e-6f));
}

void dy::steerSphere(SphereAgent& a, glm::vec3 velocity) {
    float speed = glm::length(glm::vec2(velocity.x, velocity.z));
    if(speed > 1e-4f) a.goal_heading = std::atan2(velocity.z, velocity.x);
    a.goal_speed = speed;
}

float dy::leanSpeed(float lean, float deadzone, float range, float top_speed) {
    if(lean <= deadzone) return 0.f;
    if(lean >= range) return top_speed;
    return top_speed * std::log(lean / deadzone) / std::log(range / deadzone);
}

std::vector<float> dy::sphereHeuristic(const std::vector<float>& obs, float gain) {
    // Rolling without slipping: v = r (w x up), so a torque along up x dv accelerates the sphere along dv
    glm::vec3 velocity(obs[SphereObs::velocity], 0.f, obs[SphereObs::velocity + 2]);
    glm::vec3 target(0.f, 0.f, obs[SphereObs::target_speed]);
    glm::vec3 torque = gain * glm::cross(glm::vec3(0.f, 1.f, 0.f), target - velocity);
    return { std::clamp(torque.x, -1.f, 1.f), std::clamp(torque.y, -1.f, 1.f), std::clamp(torque.z, -1.f, 1.f) };
}

void dy::resetSphereEpisode(entt::registry& reg) {

    entt::entity e = findAgent(reg);
    if(e == entt::null) return;
    auto& a = reg.get<SphereAgent>(e);
    auto& transform = reg.get<Transform>(e);
    auto& body = reg.get<RigidBody>(e);
    auto& rng = reg.ctx().get<WorldRng>();
    auto& world = physicsWorld(reg);

    // Spawn at rest on the ground, within spawn_radius of the world's spawn centre (x = index * spawn_spacing).
    // Marching cubes ground is built on demand: the column around the spawn point first, from the generator's
    // height range there
    double r = a.spawn_radius * std::sqrt(rng.uniform());
    double angle = rng.uniform(0., two_pi);
    float centre = 0.f;
    if(auto* info = reg.ctx().find<WorldInfo>()) centre = (float) info->index * a.spawn_spacing;
    glm::vec3 p(centre + (float) (r * std::cos(angle)), 0.f, (float) (r * std::sin(angle)));
    float top = 1000.f, depth = 2000.f;
    if(auto* terrain = reg.ctx().find<std::shared_ptr<TerrainColliders>>()) {
        const auto& clock = reg.ctx().get<WorldClock>();
        float surface = (*terrain)->surfaceHeight(p.x, p.z), spread = (*terrain)->overhangAmplitude() + 8.f;
        (*terrain)->require(Bounds { glm::vec3(p.x - 2.f, surface - spread, p.z - 2.f), glm::vec3(p.x + 2.f, surface + spread, p.z + 2.f) },
            clock.tick * clock.dt);
        top = surface + spread;
        depth = 2.f * spread;
    }
    auto hit = world.groundBelow(glm::vec3(p.x, top, p.z), depth);
    p.y = (hit ? hit->point.y : 0.f) + body.half_extent.x + 0.002f;
    a.home = p;
    transform.position = p;
    transform.rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    if(body.created()) {
        world.bodies().SetPositionRotationAndVelocity(JPH::BodyID(body.body), JPH::RVec3(toJolt(p)), JPH::Quat::sIdentity(), JPH::Vec3::sZero(), JPH::Vec3::sZero());
    }
    if(auto* pose = reg.try_get<PhysicsPose>(e)) *pose = PhysicsPose { p, p, transform.rotation, transform.rotation };

    if(a.steered) {
        // At rest, facing where the player last steered, until they steer again
        a.heading = a.goal_heading;
        a.speed = a.goal_speed = 0.f;
    } else {
        // The first target holds for one goal interval, then drifts
        a.heading = (float) rng.uniform(-pi, pi);
        a.speed = drawSpeed(a, rng);
        a.goal_heading = a.heading;
        a.goal_speed = a.speed;
        a.goal_timer = (float) rng.uniform(a.goal_interval.x, a.goal_interval.y);
    }

    a.decision_target = glm::vec3(0.f);
    a.decision_heading = a.heading;
    a.has_decision = false;
    a.torque = glm::vec3(0.f);
    a.reward_sum = 0.;
    a.error_sum = 0.;
    a.reward_ticks = 0;
    a.decisions = 0;
    a.ended = false;
    a.out_of_bounds = false;
    a.pending_reset = false;
    park(reg, false);

    auto& io = agentIO(reg);
    std::fill(io.action.begin(), io.action.end(), 0.f);
    io.reward = 0.f;
    io.done = false;
    io.truncated = false;
    io.stats.clear();

}



// SphereControl

void SphereControlSys::preinit() {
    agentIO(reg);
}

void SphereControlSys::init() {
    auto view = reg.view<SphereAgent, Transform, RigidBody>();
    size_t count = std::distance(view.begin(), view.end());
    if(count != 1) {
        dy::log(dy::error) << "SphereControl: expected one entity with SphereAgent, Transform and RigidBody, found " << count << "\n";
        if(count == 0) return;
    }
    agentIO(reg).configure(reg.get<SphereAgent>(*view.begin()).behavior, SphereObs::size, sphere_actions);
    resetSphereEpisode(reg);
}

void SphereControlSys::tick(double h) {

    entt::entity e = findAgent(reg);
    if(e == entt::null) return;
    auto& a = reg.get<SphereAgent>(e);
    auto& body = reg.get<RigidBody>(e);
    const auto& clock = reg.ctx().get<WorldClock>();

    if(clock.tick % clock.decision_period == 0) {
        if(a.pending_reset) resetSphereEpisode(reg);
        // The action answers the last observation: it is a torque in that observation's goal frame
        auto& io = agentIO(reg);
        glm::vec3 local(0.f);
        if(a.has_decision && (int) io.action.size() >= sphere_actions) {
            for(int i = 0; i < 3; i++) local[i] = std::isfinite(io.action[i]) ? std::clamp(io.action[i], -1.f, 1.f) : 0.f;
        }
        a.torque = a.max_torque * GoalFrame::fromForward(headingDirection(a.decision_heading)).toWorld(local);
    }

    if(body.created() && a.torque != glm::vec3(0.f)) {
        physicsWorld(reg).bodies().AddTorque(JPH::BodyID(body.body), toJolt(a.torque));
    }

}



// SphereTask

void SphereTaskSys::tick(double h) {

    entt::entity e = findAgent(reg);
    if(e == entt::null) return;
    auto& a = reg.get<SphereAgent>(e);
    auto& body = reg.get<RigidBody>(e);
    if(!body.created()) return;
    auto& world = physicsWorld(reg);
    glm::vec3 p = reg.get<Transform>(e).position;
    glm::vec3 v = toGlm(world.bodies().GetLinearVelocity(JPH::BodyID(body.body)));

    // Against the target the policy saw when it chose this window's action, not the live one
    if(a.has_decision) {
        glm::vec3 error(v.x - a.decision_target.x, 0.f, v.z - a.decision_target.z);
        a.reward_sum += sphereReward(v, a.decision_target, a.reward_scale);
        a.error_sum += glm::length(error);
        a.reward_ticks++;
    }

    // The live target turns and accelerates toward its goal at bounded rates
    if(!a.steered) {
        a.goal_timer -= (float) h;
        if(a.goal_timer <= 0.f) drawGoal(a, reg.ctx().get<WorldRng>(), p);
    }
    float max_turn = a.turn_rate * (float) h;
    a.heading = wrapPi(a.heading + std::clamp(wrapPi(a.goal_heading - a.heading), -max_turn, max_turn));
    float max_change = a.acceleration * (float) h;
    a.speed += std::clamp(a.goal_speed - a.speed, -max_change, max_change);

    // Off the field or fallen: the episode ends with this window (parked until the Observe phase)
    bool outside = a.bounds > 0.f && (std::abs(p.x) > a.bounds || std::abs(p.z) > a.bounds);
    if(outside || p.y < a.home.y - a.fall_depth) {
        a.ended = true;
        a.out_of_bounds = true;
        park(reg, true);
    }

}



// SphereObserve

void SphereObserveSys::tick(double h) {

    entt::entity e = findAgent(reg);
    if(e == entt::null) return;
    auto& io = agentIO(reg);
    auto& a = reg.get<SphereAgent>(e);

    if(io.reset_requested) {
        io.reset_requested = false;
        resetSphereEpisode(reg);
        // Agents that all start together would all end, and rebuild their ground, together
        if(a.stagger) a.decisions = (int) (reg.ctx().get<WorldRng>().uniform() * a.max_decisions);
    } else if(!a.pending_reset) {
        io.reward = a.reward_ticks > 0 ? (float) (a.reward_sum / a.reward_ticks) : 0.f;
        io.stats.clear();
        if(a.reward_ticks > 0) io.stats.emplace_back("Sphere/Speed error", (float) (a.error_sum / a.reward_ticks));
        a.decisions++;
        if(!a.steered && a.decisions >= a.max_decisions) a.ended = true;
        // Every end is a limit (time, bounds): the policy sees neither the clock nor its position
        io.done = a.ended;
        io.truncated = a.ended;
        if(a.ended) {
            io.stats.emplace_back("Sphere/Out of bounds", a.out_of_bounds ? 1.f : 0.f);
            a.pending_reset = true;
            park(reg, false);
        }
        a.reward_sum = 0.;
        a.error_sum = 0.;
        a.reward_ticks = 0;
    }

    // The observation of the live target
    auto& body = reg.get<RigidBody>(e);
    auto& world = physicsWorld(reg);
    glm::vec3 p = reg.get<Transform>(e).position;
    glm::vec3 v(0.f), w(0.f);
    if(body.created()) {
        v = toGlm(world.bodies().GetLinearVelocity(JPH::BodyID(body.body)));
        w = toGlm(world.bodies().GetAngularVelocity(JPH::BodyID(body.body)));
    }
    GoalFrame frame = GoalFrame::fromForward(headingDirection(a.heading));
    glm::vec3 horizontal(v.x, 0.f, v.z);
    float speed = glm::length(horizontal);
    glm::vec3 along = speed > 0.05f ? horizontal / speed : frame.z;
    const float above = body.half_extent.x + 2.f;

    put(io.observation, SphereObs::normal_below, frame.toLocal(groundNormal(world, p, above)));
    put(io.observation, SphereObs::normal_velocity, frame.toLocal(groundNormal(world, p + along * a.probe_distance, above)));
    put(io.observation, SphereObs::normal_target, frame.toLocal(groundNormal(world, p + frame.z * a.probe_distance, above)));
    put(io.observation, SphereObs::velocity, frame.toLocal(v));
    put(io.observation, SphereObs::angular_velocity, frame.toLocal(w));
    io.observation[SphereObs::target_speed] = a.speed;

    a.decision_target = a.targetVelocity();
    a.decision_heading = a.heading;
    a.has_decision = !a.ended;

}



void SphereHeuristicSys::tick(double h) {
    auto* io = reg.ctx().find<AgentIO>();
    if(io && (int) io->observation.size() == SphereObs::size) io->action = sphereHeuristic(io->observation);
}



void dy::registerSphereRoller() {

    registerComponent("SphereAgent", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            SphereAgent a;
            JsonFields f(j);
            f.read("behavior", a.behavior);
            f.read("max_torque", a.max_torque);
            f.read("speed_range", a.speed_range);
            f.read("stop_chance", a.stop_chance);
            f.read("turn_rate", a.turn_rate);
            f.read("acceleration", a.acceleration);
            f.read("goal_interval", a.goal_interval);
            f.read("max_goal_turn", a.max_goal_turn);
            f.read("home_radius", a.home_radius);
            f.read("bounds", a.bounds);
            f.read("fall_depth", a.fall_depth);
            f.read("spawn_radius", a.spawn_radius);
            f.read("spawn_spacing", a.spawn_spacing);
            f.read("stagger", a.stagger);
            f.read("reward_scale", a.reward_scale);
            f.read("probe_distance", a.probe_distance);
            f.read("max_decisions", a.max_decisions);
            f.read("steered", a.steered);
            f.finish();
            if(a.behavior.empty()) throw ComponentError("/behavior", "a behavior name is required");
            if(a.max_decisions < 1) throw ComponentError("/max_decisions", "must be at least 1");
            if(a.speed_range.x < 0.f || a.speed_range.y < a.speed_range.x) throw ComponentError("/speed_range", "expected 0 <= min <= max");
            if(a.goal_interval.x <= 0.f || a.goal_interval.y < a.goal_interval.x) throw ComponentError("/goal_interval", "expected 0 < min <= max");
            if(a.reward_scale <= 0.f) throw ComponentError("/reward_scale", "must be positive");
            reg.emplace_or_replace<SphereAgent>(e, a);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& a = reg.get<SphereAgent>(e);
            return nlohmann::json { { "behavior", a.behavior }, { "max_torque", a.max_torque }, { "speed_range", { a.speed_range.x, a.speed_range.y } },
                { "stop_chance", a.stop_chance }, { "turn_rate", a.turn_rate }, { "acceleration", a.acceleration },
                { "goal_interval", { a.goal_interval.x, a.goal_interval.y } }, { "max_goal_turn", a.max_goal_turn }, { "home_radius", a.home_radius },
                { "bounds", a.bounds }, { "fall_depth", a.fall_depth }, { "spawn_radius", a.spawn_radius }, { "spawn_spacing", a.spawn_spacing }, { "stagger", a.stagger }, { "reward_scale", a.reward_scale }, { "probe_distance", a.probe_distance },
                { "max_decisions", a.max_decisions }, { "steered", a.steered } };
        }
    });

    registerSystem("SphereControl", systemInfo<SphereControlSys>(SystemScope::World));
    registerSystem("SphereTask", systemInfo<SphereTaskSys>(SystemScope::World));
    registerSystem("SphereObserve", systemInfo<SphereObserveSys>(SystemScope::World));
    registerSystem("TrainerActions", systemInfo<TrainerActionsSys>(SystemScope::World));
    registerSystem("SphereHeuristic", systemInfo<SphereHeuristicSys>(SystemScope::World));

}
