#include "walk_routes.h"

#include <algorithm>
#include <cmath>

#include "agent_io.h"
#include "gait.h"
#include "ground_field.h"
#include "humanbot.h"
#include "scene/component_registry.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"

using namespace dy;

namespace {

constexpr float two_pi = 6.28318530717959f;
const glm::vec3 up(0.f, 1.f, 0.f);

// The surface under (x, z) near the height `near_y`: a walker does not step further up or down than this
std::optional<GroundField::Hit> near(const GroundField& ground, float x, float z, float near_y) {
    return ground.below(glm::vec3(x, near_y + 2.5f, z), 6.f);
}

}

RouteSlope dy::measureRoute(const GroundField& ground, glm::vec3 from, glm::vec3 direction, float length, float half_width, float step,
    const WalkRoutes* limits) {

    RouteSlope out;
    const glm::vec3 dir = safeNormalize(horizontal(direction));
    const glm::vec3 right(-dir.z, 0.f, dir.x);
    const int lanes = half_width > 0.f ? 3 : 1;
    const float offsets[3] = { 0.f, -half_width, half_width };
    const int count = std::max(1, (int) std::ceil(length / step));

    float previous[3] = { from.y, from.y, from.y };
    float start = from.y, sum_across = 0.f;
    int samples = 0;

    for(int i = 0; i <= count; i++) {
        const float s = std::min(i * step, length);
        const float ds = i == 0 ? 0.f : s - std::min((i - 1) * step, length);
        float height[3] = { 0.f, 0.f, 0.f };
        for(int k = 0; k < lanes; k++) {
            glm::vec3 p = from + dir * s + right * offsets[k];
            auto hit = near(ground, p.x, p.z, i == 0 ? (k == 0 ? from.y : height[0]) : previous[k]);
            if(!hit) {
                out.ground = false;
                return out;
            }
            height[k] = hit->point.y;
            const glm::vec3 n = hit->normal;
            float pitch = std::atan2(-glm::dot(n, dir), n.y), roll = std::atan2(-glm::dot(n, right), n.y);
            out.up = std::max(out.up, pitch);
            out.down = std::max(out.down, -pitch);
            out.across = std::max(out.across, std::abs(roll));
            if(k == 0) {
                sum_across += roll;
                samples++;
            }
            if(ds > 1e-4f) {
                float climb = std::atan2(height[k] - previous[k], ds);
                out.up = std::max(out.up, climb);
                out.down = std::max(out.down, -climb);
            }
        }
        if(lanes == 3) {
            out.across = std::max(out.across, std::abs(std::atan2(height[2] - height[0], half_width)));
            out.across = std::max(out.across, std::abs(std::atan2(height[0] - height[1], half_width)));
        }
        if(i == 0) start = height[0];
        out.rise = height[0] - start;
        for(int k = 0; k < lanes; k++) previous[k] = height[k];
        if(limits && !inBand(out, *limits)) break;
    }
    out.mean_across = samples > 0 ? sum_across / samples : 0.f;
    return out;

}

bool dy::inBand(const RouteSlope& slope, const WalkRoutes& routes) {
    return slope.ground && slope.up <= routes.up && slope.down <= routes.down && slope.across <= routes.across;
}

std::optional<glm::vec3> dy::drawDirection(const GroundField& ground, WorldRng& rng, const WalkRoutes& routes, glm::vec3 from) {
    for(int t = 0; t < routes.direction_tries; t++) {
        float angle = (float) rng.uniform(0., two_pi);
        glm::vec3 direction(std::cos(angle), 0.f, std::sin(angle));
        if(inBand(measureRoute(ground, from, direction, routes.length, routes.half_width, 0.5f, &routes), routes)) return direction;
    }
    return std::nullopt;
}

RouteStart dy::drawStart(const GroundField& ground, WorldRng& rng, const WalkRoutes& routes) {

    RouteStart start;
    // Around a spawn point the walker turns, whatever way it faces: no steeper there than the band allows in any direction
    const float gentlest = std::min({ routes.up, routes.down, routes.across });
    const float steepest = std::tan(gentlest);
    for(int t = 0; t < std::max(routes.spawn_tries, 1); t++) {
        start.tries = t + 1;
        float r = routes.spawn_radius * (float) std::sqrt(rng.uniform());
        float angle = (float) rng.uniform(0., two_pi);
        float x = routes.centre.x + r * std::cos(angle), z = routes.centre.y + r * std::sin(angle);
        auto hit = ground.top(x, z);
        if(!hit) continue;
        start.point = hit->point;
        if(hit->normal.y < std::cos(gentlest)) continue;

        // Around the point: the walker turns there, whatever way it faces
        bool clear = true;
        for(int ring = 1; ring <= 2 && clear; ring++) {
            const float radius = routes.clear * ring * 0.5f;
            for(int k = 0; k < 8 && clear; k++) {
                float a = two_pi * k / 8.f;
                auto side = near(ground, x + radius * std::cos(a), z + radius * std::sin(a), hit->point.y);
                clear = side && side->normal.y >= std::cos(gentlest) && std::abs(side->point.y - hit->point.y) <= steepest * radius + 1e-3f;
            }
        }
        if(!clear) continue;

        if(auto direction = drawDirection(ground, rng, routes, start.point)) {
            start.direction = *direction;
            start.found = true;
            return start;
        }
    }
    return start;

}

// GaitRoutes

GaitRoutesSys::GaitRoutesSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    warmup = params.value("warmup", warmup);
    jitter = std::max(0.f, params.value("jitter", jitter));
    decisions = std::max(1, params.value("decisions", decisions));
}

void GaitRoutesSys::start(GaitRoute& route) {

    entt::entity e = ensureRig(reg);
    auto& bot = reg.ctx().get<HumanBot>();
    const auto& clock = reg.ctx().get<WorldClock>();
    auto& rng = reg.ctx().get<WorldRng>();
    auto& command = reg.get_or_emplace<GaitCommand>(e);
    const uint64_t period = clock.decision_period;

    const float warm = warmup + jitter * (float) rng.uniform();
    route.warm_ticks = (uint64_t) std::ceil(warm / (clock.dt * period)) * period;
    WalkRoutes routes = reg.all_of<WalkRoutes>(e) ? reg.get<WalkRoutes>(e) : WalkRoutes {};
    routes.length = command.speed * (float) ((route.warm_ticks + decisions * period) * clock.dt) + 1.5f;
    RouteStart drawn = drawStart(groundField(reg), rng, routes);
    if(!drawn.found) dy::log(dy::warning) << "GaitRoutes: no route in the slope band after " << drawn.tries << " points, walking from the last one\n";
    route.found = drawn.found;
    route.from = drawn.point;
    route.direction = drawn.direction;
    command.start = glm::vec2(drawn.point.x, drawn.point.z);
    command.heading = std::atan2(-drawn.direction.x, -drawn.direction.z);

    // The gait starts over, standing at the new place
    if(auto* state = reg.try_get<GaitState>(e)) {
        for(FootState& f : state->foot) {
            if(reg.valid(f.target)) reg.destroy(f.target);
            if(reg.valid(f.chain)) reg.destroy(f.chain);
        }
    }
    reg.emplace_or_replace<GaitState>(e);
    reg.remove<BalanceState>(e);
    if(auto* report = reg.try_get<GaitReport>(e)) {
        report->is_planted = {};
        report->settle_time = -1.;
    }
    bot.state.history_valid = false;

    route.start_tick = clock.tick;
    route.episode++;

}

void GaitRoutesSys::tick(double h) {

    auto* bot = reg.ctx().find<HumanBot>();
    if(!bot || !bot->built) return;
    const auto& clock = reg.ctx().get<WorldClock>();
    auto& io = agentIO(reg);
    const uint64_t period = clock.decision_period;
    entt::entity e = ensureRig(reg);
    if(e == entt::null) return;
    auto& route = reg.get_or_emplace<GaitRoute>(e);

    if(route.episode == 0 || (clock.tick % period == 0 && clock.tick >= route.start_tick + route.warm_ticks + (uint64_t) decisions * period)) start(route);

    io.recordable = false;
    io.done = io.truncated = false;
    const uint64_t since = clock.tick - route.start_tick;
    if(clock.tick % period == period - 1 && since >= route.warm_ticks) {
        const uint64_t k = (since - route.warm_ticks) / period;
        io.recordable = true;
        // As every episode of the trainer and of the track demo: the older half of the stack starts empty
        if(k == 0) bot->previous_frame.fill(0.f);
        io.done = io.truncated = k + 1 >= (uint64_t) decisions;
    }

}

void dy::registerWalkRoutes() {

    registerComponent("WalkRoutes", {
        [](entt::registry& reg, entt::entity e, const nlohmann::json& j) {
            WalkRoutes r;
            JsonFields f(j);
            float up = glm::degrees(r.up), down = glm::degrees(r.down), across = glm::degrees(r.across);
            f.read("up", up);
            f.read("down", down);
            f.read("across", across);
            f.read("length", r.length);
            f.read("half_width", r.half_width);
            f.read("clear", r.clear);
            f.read("centre", r.centre);
            f.read("spawn_radius", r.spawn_radius);
            f.read("direction_tries", r.direction_tries);
            f.read("spawn_tries", r.spawn_tries);
            f.finish();
            if(up <= 0.f || up >= 80.f) throw ComponentError("/up", "degrees, between 0 and 80");
            if(down <= 0.f || down >= 80.f) throw ComponentError("/down", "degrees, between 0 and 80");
            if(across <= 0.f || across >= 80.f) throw ComponentError("/across", "degrees, between 0 and 80");
            if(r.length <= 0.f) throw ComponentError("/length", "must be positive");
            if(r.spawn_radius < 0.f) throw ComponentError("/spawn_radius", "must not be negative");
            if(r.direction_tries < 1 || r.spawn_tries < 1) throw ComponentError("/direction_tries", "tries must be at least 1");
            r.up = glm::radians(up);
            r.down = glm::radians(down);
            r.across = glm::radians(across);
            reg.emplace_or_replace<WalkRoutes>(e, r);
        },
        [](const entt::registry& reg, entt::entity e) {
            auto& r = reg.get<WalkRoutes>(e);
            return nlohmann::json { { "up", glm::degrees(r.up) }, { "down", glm::degrees(r.down) }, { "across", glm::degrees(r.across) }, { "length", r.length }, { "half_width", r.half_width },
                { "clear", r.clear }, { "centre", { r.centre.x, r.centre.y } }, { "spawn_radius", r.spawn_radius }, { "direction_tries", r.direction_tries },
                { "spawn_tries", r.spawn_tries } };
        }
    });

    registerSystem("GaitRoutes", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<GaitRoutesSys>(reg, params); }, SystemScope::World });

}
