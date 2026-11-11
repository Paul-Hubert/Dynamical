#include "sandbox.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>

#include "logic/agents/agent_env_params.h"
#include "logic/agents/agent_io.h"
#include "logic/agents/humanbot.h"
#include "logic/components/model_ref.h"
#include "logic/components/transform.h"
#include "physics/physics_sys.h"
#include "physics/physics_world.h"
#include "scene/system_registry.h"
#include "scene/world.h"
#include "util/log.h"
#include "util/paths.h"

using namespace dy;

namespace {

constexpr float pi = 3.14159265358979f;
constexpr float far_away = 1000.f;

float wrapAngle(float a) {
    while(a > pi) a -= 2.f * pi;
    while(a < -pi) a += 2.f * pi;
    return a;
}

float yawOf(glm::quat rotation) {
    HeadingFrame frame = headingFrame(rotation);
    return std::atan2(-frame.x.x, -frame.x.z);
}

// Animals in entity order: what a view gives changes with every destroyed entity
std::vector<entt::entity> animals(entt::registry& reg) {
    std::vector<entt::entity> list;
    for(auto e : reg.view<SandboxAnimal, AnimalBot>()) list.push_back(e);
    std::sort(list.begin(), list.end());
    return list;
}

bool isAdult(const Sandbox& sb, const SandboxAnimal& a) {
    const SandboxSpecies& s = sb.species[a.species];
    return a.stage + 1 >= (int) s.bodies.size() && a.age >= s.life.adult * sb.params.life_scale;
}

// In puppet worlds the bodies stand as they were put: the head is where its order asks, not where its joints are
glm::vec3 headDirection(const Sandbox& sb, const AnimalBot& bot) {
    return sb.puppet ? bot.askedHeadDirection(bot.head) : bot.headDirection();
}

glm::vec3 muzzlePoint(const Sandbox& sb, const AnimalBot& bot) {
    glm::vec3 p = bot.muzzlePoint();
    if(sb.puppet) p.y = std::max(0.f, p.y + std::sin(bot.head.y) * 1.2f * bot.body->stand_height);
    return p;
}

float bodyScale(const Sandbox& sb, const SandboxAnimal& a) {
    return sb.species[a.species].stage_scale[a.stage];
}

void applyEnv(entt::registry& reg, Sandbox& sb) {
    auto* env = reg.ctx().find<AgentEnvParams>();
    sb.params.life_scale = env ? env->get("lifeScale", sb.base.life_scale) : sb.base.life_scale;
    sb.params.walker_share = env ? env->get("walkerShare", sb.base.walker_share) : sb.base.walker_share;
    sb.params.reward_scale = env ? env->get("rewardScale", sb.base.reward_scale) : sb.base.reward_scale;
    sb.params.life_scale = std::max(sb.params.life_scale, 0.01f);
}

// The standing pose at bot.origin heading the command's way, without stopping the bodies (a puppet's step)
void placePuppet(entt::registry& reg, AnimalBot& bot) {
    const BodyDef& def = bot.body->def;
    auto& bodies = physicsWorld(reg).bodies();
    const glm::quat turn = glm::angleAxis(bot.command.yaw, glm::vec3(0.f, 1.f, 0.f));
    const glm::vec3 pivot(def.segments[0].spawn_position.x, 0.f, def.segments[0].spawn_position.z);
    for(size_t i = 0; i < def.segments.size(); i++) {
        glm::vec3 p = bot.origin + turn * (def.segments[i].spawn_position - pivot);
        glm::quat q = glm::normalize(turn * def.segments[i].spawn_rotation);
        bodies.SetPositionAndRotation(bot.parts.bodies[i], JPH::RVec3(toJolt(p)), toJolt(q), JPH::EActivation::DontActivate);
    }
}

glm::vec2 freePlace(entt::registry& reg, const Sandbox& sb, WorldRng& rng, bool edge) {
    const float a = sb.params.arena;
    glm::vec2 best(0.f);
    for(int attempt = 0; attempt < 40; attempt++) {
        glm::vec2 p;
        if(edge) {
            // On the border, a little inside
            const float along = (float) rng.uniform(-0.92, 0.92) * a, inset = 0.92f * a;
            switch((int) (rng.uniform() * 4.) & 3) {
            case 0: p = glm::vec2(along, -inset); break;
            case 1: p = glm::vec2(along, inset); break;
            case 2: p = glm::vec2(-inset, along); break;
            default: p = glm::vec2(inset, along); break;
            }
        } else {
            p = glm::vec2((float) rng.uniform(-0.85, 0.85) * a, (float) rng.uniform(-0.85, 0.85) * a);
        }
        best = p;
        bool free = true;
        for(auto [e, other, bot] : reg.view<SandboxAnimal, AnimalBot>().each()) {
            glm::vec2 q(bot.position[0].x, bot.position[0].z);
            if(glm::distance(p, q) < 1.5f * bot.body->length + 2.f) free = false;
        }
        if(free) break;
    }
    return best;
}

// The drawn bodies of an animal (Sandbox::draw): brown deer, grey wolves, the males darker
void removeShapes(entt::registry& reg, entt::entity animal) {
    std::vector<entt::entity> shapes;
    for(auto [e, shape] : reg.view<SandboxShape>().each()) if(shape.animal == animal) shapes.push_back(e);
    for(auto e : shapes) reg.destroy(e);
}

void makeShapes(entt::registry& reg, const Sandbox& sb, entt::entity animal) {
    if(!sb.draw) return;
    removeShapes(reg, animal);
    const auto& a = reg.get<SandboxAnimal>(animal);
    const auto& bot = reg.get<AnimalBot>(animal);
    const BodyDef& def = bot.body->def;
    glm::vec3 colour = sb.species[a.species].life.predator ? glm::vec3(0.55f, 0.56f, 0.6f) : glm::vec3(0.62f, 0.42f, 0.22f);
    if(a.male) colour *= 0.75f;
    for(size_t i = 0; i < def.segments.size(); i++) {
        for(const ShapeDef& s : def.segments[i].shapes) {
            auto e = reg.create();
            reg.emplace<Transform>(e);
            reg.emplace<SandboxShape>(e, SandboxShape { animal });
            auto& shape = reg.emplace<WorldShape>(e);
            shape.segment = (int) i;
            shape.local_position = s.center;
            shape.local_rotation = s.rotation;
            if(s.type == ShapeDef::Type::Capsule) { shape.type = WorldShape::Type::Capsule; shape.half_extent = glm::vec3(s.radius, s.half_height, s.radius); }
            else if(s.type == ShapeDef::Type::Sphere) { shape.type = WorldShape::Type::Sphere; shape.half_extent = glm::vec3(s.radius); }
            else { shape.type = WorldShape::Type::Box; shape.half_extent = s.half_extents; }
            // The head a little lighter: where the animal looks shows
            const float shade = (int) i == bot.head_segment ? 1.35f : 0.9f + 0.2f * (float) ((i * 37) % 8) / 8.f;
            reg.emplace<ModelRef>(e, ModelRef { "", glm::vec4(glm::min(colour * shade, glm::vec3(1.f)), 1.f) });
        }
    }
}

void moveShapes(entt::registry& reg) {
    for(auto [e, link, shape, transform] : reg.view<SandboxShape, WorldShape, Transform>().each()) {
        const auto* bot = reg.try_get<AnimalBot>(link.animal);
        if(!bot || shape.segment >= (int) bot->position.size()) continue;
        const glm::vec3 position = bot->position[shape.segment] + bot->rotation[shape.segment] * shape.local_position;
        const glm::quat rotation = glm::normalize(bot->rotation[shape.segment] * shape.local_rotation);
        auto* pose = reg.try_get<PhysicsPose>(e);
        if(!pose) pose = &reg.emplace<PhysicsPose>(e, PhysicsPose { position, position, rotation, rotation });
        pose->prev_position = pose->position;
        pose->prev_rotation = pose->rotation;
        pose->position = position;
        pose->rotation = rotation;
        transform.position = position;
        transform.rotation = rotation;
    }
}

void removeAnimal(entt::registry& reg, Sandbox& sb, entt::entity e) {
    auto& a = reg.get<SandboxAnimal>(e);
    auto& bot = reg.get<AnimalBot>(e);
    if(sb.draw) removeShapes(reg, e);
    if(reg.valid(a.mind)) reg.destroy(a.mind);
    destroyAnimalRagdoll(reg, bot);
    sb.by_group.erase(a.group);
    reg.destroy(e);
}

void populate(entt::registry& reg, Sandbox& sb) {
    for(auto e : animals(reg)) removeAnimal(reg, sb, e);
    for(auto& s : sb.species) {
        s.alive = 0;
        s.born = s.spawned = s.starved = s.killed = s.aged = s.kills = s.falls = 0;
        s.lives = 0;
        s.lifetime = 0.;
        s.work = s.moved = 0.;
        s.ticks = 0;
    }
    auto& rng = reg.ctx().get<WorldRng>();
    for(size_t s = 0; s < sb.species.size(); s++) {
        for(int i = 0; i < sb.species[s].life.start; i++) {
            glm::vec2 at = freePlace(reg, sb, rng, false);
            spawnSandboxAnimal(reg, (int) s, at, (float) rng.uniform(0., 2. * pi), -1.f, i % 2);
        }
    }
}

// The animal an entity is, alive
SandboxAnimal* living(entt::registry& reg, entt::entity e) {
    if(e == entt::null || !reg.valid(e)) return nullptr;
    auto* a = reg.try_get<SandboxAnimal>(e);
    return a && !a->dead ? a : nullptr;
}

void die(entt::registry& reg, Sandbox& sb, entt::entity e, int cause) {
    auto& a = reg.get<SandboxAnimal>(e);
    SandboxSpecies& species = sb.species[a.species];
    a.dead = true;
    a.cause = cause;
    a.reported = false;
    const float scale = bodyScale(sb, a);
    a.meat = species.life.meat * scale * scale * scale;
    a.rot = 0.f;
    a.pregnant = false;
    species.alive--;
    species.lives++;
    species.lifetime += a.lived;
    if(cause == SandboxAnimal::Starved) species.starved++;
    else if(cause == SandboxAnimal::Killed) species.killed++;
    else species.aged++;
    if(cause != SandboxAnimal::Old) a.objective -= 1.f;
    if(cause == SandboxAnimal::Killed && a.attacked < 10.f) {
        if(SandboxAnimal* killer = living(reg, a.attacker)) {
            killer->window.kills++;
            sb.species[killer->species].kills++;
        }
    }
}

void mindOrders(const Sandbox& sb, const SandboxAnimal& a, AnimalBot& bot, const AgentIO* mind) {
    const SpeciesLife& life = sb.species[a.species].life;
    const AnimalBody& body = *bot.body;
    auto value = [&](int i) {
        if(!mind || (int) mind->action.size() <= i || !std::isfinite(mind->action[i])) return 0.f;
        return std::clamp(mind->action[i], -1.f, 1.f);
    };
    int mode = mind && !mind->discrete.empty() ? std::clamp(mind->discrete[0], 0, animal_modes - 1) : 0;
    // A speed action under zero stands still: a mind that knows nothing yet stands half the time
    float speed = std::max(value(0), 0.f) * life.top_speed * body.walk_speed;
    float turn = value(1) / std::max(life.turn_radius * body.length, 0.1f);
    glm::vec2 head(value(2) * animal_head_yaw, value(3) >= 0.f ? value(3) * animal_head_up : value(3) * animal_head_down);

    if(a.stun > 0.f) {
        mode = (int) AnimalMode::Rest;
        head = glm::vec2(0.f);
    }
    if(mode == (int) AnimalMode::Mate || mode == (int) AnimalMode::Rest) speed = 0.f;
    else if(mode == (int) AnimalMode::Feed) speed = std::min(speed, 0.5f * life.feed_speed * body.walk_speed);

    bot.mode = mode;
    bot.head = clampHead(head);
    bot.command.wanted_speed = speed;
    bot.command.wanted_turn = turn;
    // The command turns from where the animal heads: a walker that did not follow is not asked to catch up
    if(!sb.puppet) {
        const float yaw = yawOf(bot.rotation[0]);
        if(std::abs(wrapAngle(bot.command.yaw - yaw)) > 0.5f) bot.command.yaw = yaw;
    }
}

double mechanicalEnergy(const AnimalBot& bot) {
    const AnimalBody& body = *bot.body;
    double e = 0.;
    for(size_t s = 0; s < bot.position.size(); s++) {
        const double mass = body.mass * body.def.segments[s].percentage / 100.;
        e += mass * (0.5 * glm::dot(bot.linear[s], bot.linear[s]) + 9.81 * bot.position[s].y);
    }
    return e;
}

std::string statKey(const char* group, const SandboxSpecies& species, const std::string& name) {
    return std::string(group) + "/" + species.life.id + "/" + name;
}

}

const std::vector<std::string>& dy::sandboxSignalNames() {
    static const std::vector<std::string> names = {
        "energy", "health", "age", "adult", "male", "pregnant", "energy_gain", "energy_spent", "damage_taken", "damage_dealt", "kills", "births", "falls",
        "speed", "mode_move", "mode_feed", "mode_attack", "mode_mate", "mode_rest", "feeding", "grass_here", "muzzle_height", "predator_distance",
        "prey_distance", "mate_distance", "carcass_distance", "herd_distance", "herd_count", "since_fed", "dt", "grass_near", "conceptions", "lived",
        "predator_closing", "prey_closing", "mate_closing", "carcass_closing", "mating" };
    return names;
}

RewardProgram RewardProgram::parse(const nlohmann::json& j) {
    RewardProgram p;
    p.round = j.value("round", 0);
    p.scale = j.value("scale", 1.f);
    p.notes = j.value("notes", "");
    if(auto it = j.find("terms"); it != j.end()) {
        if(!it->is_array()) throw std::runtime_error("rewards: terms must be a list");
        for(auto& t : *it) {
            Term term;
            term.name = t.value("name", "");
            if(term.name.empty()) throw std::runtime_error("rewards: a term has no name");
            for(auto& other : p.terms) if(other.name == term.name) throw std::runtime_error("rewards: two terms named " + term.name);
            try {
                term.expression = Expression::parse(t.value("expr", ""), sandboxSignalNames());
            } catch(const ExprError& e) {
                throw std::runtime_error("rewards: term " + term.name + ": " + e.what());
            }
            p.terms.push_back(std::move(term));
        }
    }
    return p;
}

RewardProgram RewardProgram::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) return {};
    RewardProgram p;
    try {
        p = parse(nlohmann::json::parse(in, nullptr, true, true));
    } catch(const nlohmann::json::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    } catch(const std::runtime_error& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
    p.file_hash = fileSha256(path);
    return p;
}

int Sandbox::speciesIndex(const std::string& id) const {
    for(size_t i = 0; i < species.size(); i++) if(species[i].life.id == id) return (int) i;
    return -1;
}

entt::entity dy::spawnSandboxAnimal(entt::registry& reg, int species_index, glm::vec2 at, float yaw, float age, int male) {

    auto& sb = reg.ctx().get<Sandbox>();
    auto& rng = reg.ctx().get<WorldRng>();
    SandboxSpecies& species = sb.species[species_index];
    const float scale = sb.params.life_scale;

    entt::entity e = reg.create();
    auto& a = reg.emplace<SandboxAnimal>(e);
    a.species = species_index;
    a.group = sb.next_group++;
    a.male = male < 0 ? rng.uniform() < 0.5 : male != 0;
    if(age < 0.f) {
        // An adult, not near its end, with most of a store
        a.age = (float) rng.uniform(species.life.adult, std::max(species.life.adult, 0.5f * species.life.old)) * scale;
        a.energy = (float) rng.uniform(0.6, 1.);
    } else {
        a.age = age;
        a.energy = species.life.newborn_energy;
    }
    a.stage = 0;
    for(size_t s = 0; s < species.stage_age.size(); s++) if(a.age >= species.stage_age[s] * scale) a.stage = (int) s;
    sb.by_group[a.group] = e;

    auto& bot = reg.emplace<AnimalBot>(e);
    bot.body = species.bodies[a.stage];
    bot.base_params = species.walker_params;
    bot.ranges = species.ranges;
    bot.kinematic = sb.puppet;
    bot.orders = true;
    bot.neck_may_touch = true;
    applyAnimalEnvParams(reg, bot);
    buildAnimalRagdoll(reg, bot, AnimalBuild { a.group, true });
    bot.origin = glm::vec3(at.x, 0.f, at.y);
    standAnimalBot(reg, bot, bot.origin, yaw, false);
    bot.command = WalkCommand();
    bot.command.yaw = yaw;
    bot.command.started = true;
    bot.snapshot_trunk = bot.position[0];
    bot.snapshot_direction = bot.command.direction();
    a.window.from = bot.position[0];

    if(!sb.puppet) {
        auto& io = reg.emplace<AgentIO>(e);
        io.configure(species.walker_behavior, bot.observationSize(), bot.actionSize());
        io.sensor_name = "StackingSensor_size2_VectorSensor_size" + std::to_string(bot.frame_size);
        io.addVector("nogail_order", animal_order_size);
    }

    a.mind = reg.create();
    auto& mind = reg.emplace<SandboxMind>(a.mind);
    mind.animal = e;
    mind.phase = (int) (rng.uniform() * sb.params.mind_every);
    auto& mio = reg.emplace<AgentIO>(a.mind);
    mio.configure(species.action_behavior, MindObs::size_total, mind_actions);
    mio.addBranch(animal_modes);
    mio.addBuffer("Interests", sb.params.interests, MindObs::row);

    species.alive++;
    makeShapes(reg, sb, e);
    return e;

}

void dy::sandboxSignals(entt::registry& reg, entt::entity e, float seconds, float* out) {

    const auto& sb = reg.ctx().get<Sandbox>();
    auto& a = reg.get<SandboxAnimal>(e);
    const auto& bot = reg.get<AnimalBot>(e);
    const SandboxSpecies& species = sb.species[a.species];
    const float scale = sb.params.life_scale;
    const float ticks = (float) std::max(a.window.ticks, 1);
    namespace S = SandboxSignal;

    out[S::energy] = a.energy;
    out[S::health] = a.health;
    out[S::age] = a.age / (species.life.old * scale);
    out[S::adult] = isAdult(sb, a) ? 1.f : 0.f;
    out[S::male] = a.male ? 1.f : 0.f;
    out[S::pregnant] = a.pregnant ? 1.f : 0.f;
    out[S::energy_gain] = a.window.energy_gain;
    out[S::energy_spent] = a.window.energy_spent;
    out[S::damage_taken] = a.window.damage_taken;
    out[S::damage_dealt] = a.window.damage_dealt;
    out[S::kills] = (float) a.window.kills;
    out[S::births] = (float) a.window.births;
    out[S::falls] = (float) a.window.falls;
    out[S::speed] = seconds > 0.f ? glm::length(horizontal(bot.position[0] - a.window.from)) / seconds / std::max(bot.body->walk_speed, 1e-3f) : 0.f;
    for(int m = 0; m < animal_modes; m++) out[S::mode_move + m] = (float) a.window.modes[m] / ticks;
    out[S::feeding] = (float) a.window.feeding / ticks;
    const glm::vec3 muzzle = muzzlePoint(sb, bot);
    out[S::grass_here] = sb.grass.height(muzzle.x, muzzle.z);
    out[S::muzzle_height] = muzzle.y / std::max(bot.body->stand_height, 1e-3f);

    float predator = far_away, prey = far_away, mate = far_away, carcass = far_away, herd = far_away;
    int herd_count = 0;
    const glm::vec3 here = bot.position[0];
    for(auto [other, b, other_bot] : reg.view<SandboxAnimal, AnimalBot>().each()) {
        if(other == e) continue;
        const float d = glm::length(horizontal(other_bot.position[0] - here));
        if(b.dead) {
            if(b.meat > 0.f) carcass = std::min(carcass, d);
            continue;
        }
        const bool other_predator = sb.species[b.species].life.predator;
        if(b.species == a.species) {
            herd = std::min(herd, d);
            if(d <= species.life.sight) herd_count++;
            if(b.male != a.male && isAdult(sb, b)) mate = std::min(mate, d);
        } else if(other_predator && !species.life.predator) {
            predator = std::min(predator, d);
        } else if(!other_predator && species.life.predator) {
            prey = std::min(prey, d);
        }
    }
    out[S::predator_distance] = predator;
    out[S::prey_distance] = prey;
    out[S::mate_distance] = mate;
    out[S::carcass_distance] = carcass;
    out[S::herd_distance] = herd;
    out[S::herd_count] = (float) herd_count;
    out[S::since_fed] = a.since_fed / (species.life.starve * scale);
    out[S::dt] = seconds;
    {
        HeadingFrame frame = headingFrame(bot.rotation[0]);
        float near = sb.grass.height(here.x, here.z);
        for(int d = 0; d < 8; d++) {
            const float angle = (float) d * (2.f * pi / 8.f);
            const glm::vec3 p = here + 3.f * (std::cos(angle) * frame.x - std::sin(angle) * frame.z);
            near = std::max(near, sb.grass.height(p.x, p.z));
        }
        out[S::grass_near] = near;
    }
    out[S::conceptions] = (float) a.window.conceptions;
    out[S::lived] = a.lived;
    out[S::mating] = std::min(a.mating / std::max(species.life.mate_time * scale, 1e-3f), 1.f);
    const float now[4] = { predator, prey, mate, carcass };
    for(int k = 0; k < 4; k++) {
        const bool has = now[k] < far_away;
        out[S::predator_closing + k] = has && a.nearest[k] >= 0.f ? std::clamp(a.nearest[k] - now[k], -3.f, 3.f) : 0.f;
        a.nearest[k] = has ? now[k] : -1.f;
    }

}

void dy::observeSandboxMind(entt::registry& reg, entt::entity e, AgentIO& io) {

    const auto& sb = reg.ctx().get<Sandbox>();
    const auto& a = reg.get<SandboxAnimal>(e);
    const auto& bot = reg.get<AnimalBot>(e);
    const SandboxSpecies& species = sb.species[a.species];
    const SpeciesLife& life = species.life;
    const AnimalBody& body = *bot.body;
    const float scale = sb.params.life_scale;
    namespace O = MindObs;

    HeadingFrame frame = headingFrame(bot.rotation[0]);
    const glm::vec3 here = bot.position[0];
    const glm::vec3 velocity = bot.origin_velocity[0];
    const float top = std::max(life.top_speed * body.walk_speed, 1e-3f);
    const glm::vec3 head = headDirection(sb, bot);
    const glm::vec3 muzzle = muzzlePoint(sb, bot);

    std::fill(io.observation.begin(), io.observation.end(), 0.f);
    float* o = io.observation.data();
    o[O::energy] = a.energy;
    o[O::health] = a.health;
    o[O::age] = std::min(a.age / (life.old * scale), 1.f);
    o[O::adult] = isAdult(sb, a) ? 1.f : 0.f;
    o[O::male] = a.male ? 1.f : 0.f;
    o[O::pregnant] = a.pregnant ? 1.f : 0.f;
    o[O::gestation] = a.pregnant ? std::min(a.gestation / (life.gestation * scale), 1.f) : 0.f;
    o[O::size] = bodyScale(sb, a);
    o[O::since_fed] = std::min(a.since_fed / (life.starve * scale), 1.f);
    o[O::forward_speed] = glm::dot(velocity, frame.x) / top;
    o[O::side_speed] = -glm::dot(velocity, frame.z) / top;
    // Where the head points, as the offsets that would ask for it
    {
        glm::vec3 local = frame.project(head);
        const float elevation = std::asin(std::clamp(local.y, -1.f, 1.f)) - std::asin(std::clamp(bot.head_stand.y, -1.f, 1.f));
        const float azimuth = wrapAngle(std::atan2(-local.z, local.x) - std::atan2(-bot.head_stand.z, bot.head_stand.x));
        o[O::head_yaw] = std::clamp(azimuth / animal_head_yaw, -1.5f, 1.5f);
        o[O::head_pitch] = std::clamp(elevation / animal_head_down, -1.5f, 1.5f);
    }
    o[O::muzzle_height] = muzzle.y / std::max(body.stand_height, 1e-3f);
    o[O::grass_here] = sb.grass.height(muzzle.x, muzzle.z);
    o[O::stunned] = a.stun > 0.f ? 1.f : 0.f;
    o[O::centre] = glm::dot(-here, frame.x) / sb.params.arena;
    o[O::centre + 1] = -glm::dot(-here, frame.z) / sb.params.arena;
    o[O::mode + std::clamp(bot.mode, 0, animal_modes - 1)] = 1.f;
    for(int ring = 0; ring < 2; ring++) {
        const float radius = ring == 0 ? 3.f : 10.f;
        for(int d = 0; d < 8; d++) {
            const float angle = (float) d * (2.f * pi / 8.f);
            // Turning left from straight ahead
            glm::vec3 p = here + radius * (std::cos(angle) * frame.x - std::sin(angle) * frame.z);
            o[O::grass + ring * 8 + d] = sb.grass.height(p.x, p.z);
        }
    }

    // What it perceives of the others: seen inside its field of view, around where the head points, or heard close by
    struct Seen {
        float distance;
        entt::entity entity;
        bool seen;
    };
    std::vector<Seen> perceived;
    glm::vec3 look = horizontal(head);
    look = glm::length(look) > 1e-4f ? glm::normalize(look) : frame.x;
    const float half_fov = glm::radians(0.5f * life.fov);
    std::vector<entt::entity> others;
    for(auto other : reg.view<SandboxAnimal, AnimalBot>()) others.push_back(other);
    std::sort(others.begin(), others.end());
    for(auto other : others) {
        if(other == e) continue;
        const auto& b = reg.get<SandboxAnimal>(other);
        const auto& other_bot = reg.get<AnimalBot>(other);
        if(b.dead && b.meat <= 0.f) continue;
        glm::vec3 offset = horizontal(other_bot.position[0] - here);
        const float d = glm::length(offset);
        if(d > life.sight) continue;
        const bool in_view = d < 1e-3f || std::acos(std::clamp(glm::dot(offset / std::max(d, 1e-3f), look), -1.f, 1.f)) <= half_fov;
        const bool heard = !b.dead && d <= life.hearing && glm::length(other_bot.origin_velocity[0]) > 0.3f;
        if(in_view) perceived.push_back({ d, other, true });
        else if(heard) perceived.push_back({ d, other, false });
    }
    std::stable_sort(perceived.begin(), perceived.end(), [](const Seen& x, const Seen& y) { return x.distance < y.distance; });

    auto& buffer = io.sensors[0];
    buffer.clear();
    for(const Seen& s : perceived) {
        float* row = buffer.add();
        if(!row) break;
        const auto& b = reg.get<SandboxAnimal>(s.entity);
        const auto& other_bot = reg.get<AnimalBot>(s.entity);
        glm::vec3 offset = horizontal(other_bot.position[0] - here);
        if(!s.seen) {
            // A direction and a rough distance: near, or as far as it hears
            const float rough = s.distance < 0.5f * life.hearing ? 0.5f * life.hearing : life.hearing;
            offset = offset / std::max(s.distance, 1e-3f) * rough;
            row[O::row_position] = glm::dot(offset, frame.x) / life.sight;
            row[O::row_position + 1] = -glm::dot(offset, frame.z) / life.sight;
            row[O::row_seen] = 0.5f;
            continue;
        }
        glm::vec3 relative = (b.dead ? glm::vec3(0.f) : other_bot.origin_velocity[0]) - velocity;
        row[O::row_position] = glm::dot(offset, frame.x) / life.sight;
        row[O::row_position + 1] = -glm::dot(offset, frame.z) / life.sight;
        row[O::row_velocity] = glm::dot(relative, frame.x) / 5.f;
        row[O::row_velocity + 1] = -glm::dot(relative, frame.z) / 5.f;
        const bool other_predator = sb.species[b.species].life.predator;
        if(b.dead) {
            row[O::row_carcass] = 1.f;
        } else {
            row[O::row_own] = b.species == a.species ? 1.f : 0.f;
            row[O::row_prey] = b.species != a.species && life.predator && !other_predator ? 1.f : 0.f;
            row[O::row_predator] = b.species != a.species && other_predator && !life.predator ? 1.f : 0.f;
            row[O::row_male] = b.male ? 1.f : 0.f;
            // A possible mate (an adult of its species and of the other sex, itself an adult): 0.5, 1 when it is in Mate mode
            if(b.species == a.species && b.male != a.male && isAdult(sb, b) && isAdult(sb, a)) row[O::row_mating] = other_bot.mode == (int) AnimalMode::Mate ? 1.f : 0.5f;
        }
        row[O::row_size] = std::min(other_bot.body->mass / (3.f * std::max(body.mass, 1e-3f)), 1.f);
        row[O::row_seen] = 1.f;
    }

}

void dy::sandboxHeuristic(const SandboxSpecies& species, AgentIO& io) {

    namespace O = MindObs;
    const float* o = io.observation.data();
    const auto& buffer = io.sensors[0];
    io.action.assign(mind_actions, 0.f);
    io.discrete.assign(1, (int) AnimalMode::Move);
    const SpeciesLife& life = species.life;

    // The nearest row of a kind: its direction (radians to the left) and distance (m), false without one
    auto nearest = [&](int flag, float want_male, float& angle, float& distance) {
        for(int r = 0; r < buffer.count; r++) {
            const float* row = buffer.data.data() + (size_t) r * buffer.size;
            if(row[flag] < 0.5f || row[O::row_seen] < 0.9f) continue;
            if(want_male >= 0.f && std::abs(row[O::row_male] - want_male) > 0.5f) continue;
            const float f = row[O::row_position] * life.sight, l = row[O::row_position + 1] * life.sight;
            angle = std::atan2(l, f);
            distance = std::hypot(f, l);
            return true;
        }
        return false;
    };
    auto go = [&](float angle, float speed) {
        io.action[0] = speed;
        io.action[1] = std::clamp(1.5f * angle, -1.f, 1.f);
        // Turning on the spot is not a thing a walker does: slow down for a sharp turn
        if(std::abs(angle) > 1.f) io.action[0] = std::min(speed, 0.4f);
    };
    auto feed = [&] {
        io.discrete[0] = (int) AnimalMode::Feed;
        io.action[0] = -1.f;
        io.action[3] = -1.f;
    };

    float angle = 0.f, distance = 0.f;
    const bool adult = o[O::adult] > 0.5f, male = o[O::male] > 0.5f, pregnant = o[O::pregnant] > 0.5f;

    if(!life.predator) {
        if(nearest(O::row_predator, -1.f, angle, distance) && distance < 0.35f * life.sight) {
            go(wrapAngle(angle + pi), 1.f);
            return;
        }
        if(o[O::energy] < 0.85f) {
            if(o[O::grass_here] > 0.12f) return feed();
            // To the tallest grass around
            int best = -1;
            float best_height = 0.1f;
            for(int i = 0; i < 16; i++) {
                if(o[O::grass + i] > best_height) {
                    best_height = o[O::grass + i];
                    best = i;
                }
            }
            if(best >= 0) return go(wrapAngle((float) (best % 8) * (2.f * pi / 8.f)), 0.45f);
            // None in sight: toward the middle of the arena
            return go(std::atan2(o[O::centre + 1], o[O::centre]), 0.5f);
        }
    } else {
        if(o[O::energy] < 0.9f) {
            if(nearest(O::row_carcass, -1.f, angle, distance)) {
                if(distance < 1.2f) {
                    feed();
                    io.action[2] = std::clamp(angle, -1.f, 1.f);
                    return;
                }
                return go(angle, 0.6f);
            }
            if(nearest(O::row_prey, -1.f, angle, distance)) {
                go(angle, 1.f);
                io.action[2] = std::clamp(angle, -1.f, 1.f);
                if(distance < 2.5f) io.discrete[0] = (int) AnimalMode::Attack;
                return;
            }
            return go(0.3f, 0.5f);
        }
    }

    // Fed: an adult looks for a mate
    if(adult && !pregnant && o[O::energy] > life.mate_energy + 0.1f && nearest(O::row_own, male ? 0.f : 1.f, angle, distance)) {
        if(distance < 0.8f * life.mate_reach * 1.5f) {
            io.discrete[0] = (int) AnimalMode::Mate;
            io.action[0] = -1.f;
            return;
        }
        return go(angle, 0.5f);
    }
    io.discrete[0] = (int) AnimalMode::Rest;
    io.action[0] = -1.f;

}



// The contacts between two animals, with what each blow weighs

struct SandboxSys::Contacts : public JPH::ContactListener {
    struct Event {
        uint32_t group1, group2;
        int segment1, segment2;
        float impulse;
    };

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        record(a, b, manifold, settings);
    }
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        record(a, b, manifold, settings);
    }

    void record(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, const JPH::ContactSettings& settings) {
        const uint32_t group1 = (uint32_t) (a.GetUserData() >> 32), group2 = (uint32_t) (b.GetUserData() >> 32);
        if(group1 == 0 || group2 == 0 || group1 == group2) return;
        JPH::CollisionEstimationResult result;
        JPH::EstimateCollisionResponse(a, b, manifold, result, settings.mCombinedFriction, settings.mCombinedRestitution);
        float impulse = 0.f;
        for(float i : result.mContactImpulse) impulse += i;
        if(impulse <= 0.f) return;
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back({ group1, group2, (int) (a.GetUserData() & 0xffffffffu), (int) (b.GetUserData() & 0xffffffffu), impulse });
    }

    std::vector<Event> take() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Event> out;
        out.swap(events);
        return out;
    }

    std::mutex mutex;
    std::vector<Event> events;
    std::vector<JPH::BodyID> walls;
};



// Sandbox

SandboxSys::SandboxSys(entt::registry& reg, const nlohmann::json& params) : System(reg), params(params), contacts(std::make_unique<Contacts>()) {}

SandboxSys::~SandboxSys() = default;

void SandboxSys::preinit() {

    auto& sb = reg.ctx().emplace<Sandbox>();
    if(!reg.ctx().find<AgentsReset>()) reg.ctx().emplace<AgentsReset>();
    sb.puppet = params.value("puppet", false);
    sb.heuristic = params.value("heuristic", false);
    sb.draw = params.value("draw", false);

    // The life params, with the scene's overrides
    const std::string path = libraryResourceFile(params.value("life", std::string("agents/params/life.params.json"))).string();
    std::ifstream in(path, std::ios::binary);
    if(!in) throw std::runtime_error("Sandbox: cannot open " + path);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, true, true);
    if(params.contains("arena")) j["arena"] = params["arena"];
    if(auto it = params.find("species"); it != params.end() && it->is_object()) {
        for(auto& [id, block] : it->items()) {
            if(!j["species"].contains(id)) throw std::runtime_error("Sandbox: the life params have no species " + id);
            j["species"][id].update(block);
        }
    }
    if(auto it = params.find("only"); it != params.end() && it->is_array() && !it->empty()) {
        nlohmann::json kept = nlohmann::json::object();
        for(auto& id : *it) {
            const std::string name = id.get<std::string>();
            if(!j["species"].contains(name)) throw std::runtime_error("Sandbox: the life params have no species " + name);
            kept[name] = j["species"][name];
        }
        j["species"] = kept;
    }
    sb.base = LifeParams::parse(j);
    sb.base.file_hash = fileSha256(path);
    sb.params = sb.base;
    applyEnv(reg, sb);

    sb.rewards_dir = params.value("rewards", std::string("agents/rewards"));
    auto walker_params = loadAnimalParams(libraryResourceFile("agents/params/animal.params.json").string());
    for(const SpeciesLife& life : sb.base.species) {
        SandboxSpecies s;
        s.life = life;
        auto adult = armAnimalBody(loadAnimalBody(animalBodyPath(life.id)), life, s.weapon_segment);
        if(!life.weapon_segment.empty() && s.weapon_segment < 0) throw std::runtime_error("Sandbox: " + life.id + " has no body named " + life.weapon_segment + " to hold its weapon");
        // Born small, half way to full size at half the age of an adult
        if(life.young_scale < 0.999f) {
            const float scales[2] = { life.young_scale, 0.5f * (life.young_scale + 1.f) };
            for(int i = 0; i < 2; i++) {
                s.bodies.push_back(scaleAnimalBody(*adult, scales[i]));
                s.stage_scale.push_back(scales[i]);
                s.stage_age.push_back(i == 0 ? 0.f : 0.5f * life.adult);
            }
            s.stage_age.push_back(life.adult);
        } else {
            s.stage_age.push_back(0.f);
        }
        s.bodies.push_back(adult);
        s.stage_scale.push_back(1.f);
        s.walker_params = walker_params;
        s.ranges = GaitRanges::defaults();
        s.walker_behavior = adult->behavior;
        s.action_behavior = "Action_" + life.id;
        s.rewards = RewardProgram::load(libraryResourceFile(sb.rewards_dir + "/" + life.id + ".rewards.json").string());
        sb.species.push_back(std::move(s));
    }

}

void SandboxSys::init() {

    auto& sb = reg.ctx().get<Sandbox>();
    auto& world = physicsWorld(reg);
    const uint64_t seed = reg.ctx().find<WorldInfo>() ? reg.ctx().get<WorldInfo>().seed : 0;
    sb.grass = GrassField(sb.params.grass, sb.params.arena, seed);
    world.system().SetContactListener(contacts.get());

    // Walls around the arena: what is outside is not the animals' to walk on
    const float a = sb.params.arena, thick = 1.f, high = 3.f;
    const glm::vec3 centres[4] = { { 0.f, high, -a - thick }, { 0.f, high, a + thick }, { -a - thick, high, 0.f }, { a + thick, high, 0.f } };
    for(int i = 0; i < 4; i++) {
        const glm::vec3 half = i < 2 ? glm::vec3(a + 2.f * thick, high, thick) : glm::vec3(thick, high, a + 2.f * thick);
        JPH::BodyCreationSettings settings(new JPH::BoxShape(toJolt(half)), JPH::RVec3(toJolt(centres[i])), JPH::Quat::sIdentity(), JPH::EMotionType::Static, PhysicsLayers::static_layer);
        settings.mFriction = 0.3f;
        contacts->walls.push_back(world.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate));
    }

    // For the viewer: the grass as it starts, a flat tile for every 5 m square that is mostly grass, darker where it is taller
    if(sb.draw) {
        const float tile = 5.f;
        for(float z = -sb.params.arena; z < sb.params.arena; z += tile) {
            for(float x = -sb.params.arena; x < sb.params.arena; x += tile) {
                float height = 0.f;
                int covered = 0;
                for(int k = 0; k < 25; k++) {
                    const float h = sb.grass.full(x + 0.5f + (float) (k % 5), z + 0.5f + (float) (k / 5));
                    height += h;
                    covered += h > 0.f ? 1 : 0;
                }
                if(covered < 13) continue;
                height /= (float) covered;
                auto e = reg.create();
                reg.emplace<Transform>(e).position = glm::vec3(x + 0.5f * tile, 0.01f, z + 0.5f * tile);
                auto& shape = reg.emplace<WorldShape>(e);
                shape.type = WorldShape::Type::Box;
                shape.half_extent = glm::vec3(0.5f * tile, 0.01f, 0.5f * tile);
                reg.emplace<ModelRef>(e, ModelRef { "", glm::vec4(0.42f - 0.2f * height, 0.62f - 0.18f * height, 0.25f - 0.1f * height, 1.f) });
            }
        }
    }
    populate(reg, sb);
    for(const SandboxSpecies& s : sb.species) {
        // Where its muzzle is when it stands: what the grass has to reach, or the head to come down to
        float muzzle = 0.f;
        for(auto [e, a, bot] : reg.view<SandboxAnimal, AnimalBot>().each()) if(&sb.species[a.species] == &s && a.stage + 1 == (int) s.bodies.size()) muzzle = bot.muzzlePoint().y;
        dy::log(dy::debug) << "Sandbox: " << s.alive << " " << s.life.id << " (" << s.bodies.back()->def.segments.size() << " bodies each, the muzzle "
            << muzzle << " m up standing, " << s.rewards.terms.size() << " reward terms of round " << s.rewards.round << ")\n";
    }

}

void SandboxSys::finish() {
    auto* sb = reg.ctx().find<Sandbox>();
    if(!sb) return;
    for(auto e : animals(reg)) removeAnimal(reg, *sb, e);
    auto& world = physicsWorld(reg);
    if(world.system().GetContactListener() == contacts.get()) world.system().SetContactListener(nullptr);
    for(auto id : contacts->walls) {
        world.bodies().RemoveBody(id);
        world.bodies().DestroyBody(id);
    }
    contacts->walls.clear();
}

void SandboxSys::tick(double h) {

    auto& sb = reg.ctx().get<Sandbox>();
    const auto& clock = reg.ctx().get<WorldClock>();
    auto& rng = reg.ctx().get<WorldRng>();
    const float dt = (float) h;

    if(clock.tick % clock.decision_period == 0) {

        applyEnv(reg, sb);
        const float scale = sb.params.life_scale;

        for(auto e : animals(reg)) {
            auto& a = reg.get<SandboxAnimal>(e);
            auto& bot = reg.get<AnimalBot>(e);
            if(!a.dead || !a.reported) continue;
            // Its agents' last steps went out: a carcass from here on
            if(reg.valid(a.mind)) {
                reg.destroy(a.mind);
                a.mind = entt::null;
                reg.remove<AgentIO>(e);
                if(!sb.puppet) {
                    bot.limp = true;
                    std::vector<float> zero((size_t) bot.actionSize(), 0.f);
                    driveAnimalBot(bot, zero.data());
                }
            }
            if(a.meat <= 0.f || a.rot >= sb.species[a.species].life.rot * scale) removeAnimal(reg, sb, e);
        }

        for(auto e : animals(reg)) {
            auto& a = reg.get<SandboxAnimal>(e);
            auto& bot = reg.get<AnimalBot>(e);
            if(a.dead || !a.restand) continue;
            // Put back up where it is, facing where it faced; at its new size when it grew
            const glm::vec3 at(bot.position[0].x, 0.f, bot.position[0].z);
            const float yaw = yawOf(bot.rotation[0]);
            SandboxSpecies& species = sb.species[a.species];
            if(a.grow && a.stage + 1 < (int) species.bodies.size()) {
                a.stage++;
                destroyAnimalRagdoll(reg, bot);
                bot.body = species.bodies[a.stage];
                buildAnimalRagdoll(reg, bot, AnimalBuild { a.group, true });
                makeShapes(reg, sb, e);
            } else if(!a.grow) {
                a.stun = species.life.stun;
            }
            a.grow = false;
            a.restand = false;
            bot.origin = at;
            standAnimalBot(reg, bot, at, yaw, false);
            bot.command.yaw = yaw;
            bot.command.speed = bot.command.turn = 0.f;
            std::fill(bot.previous_frame.begin(), bot.previous_frame.end(), 0.f);
            a.walker_fresh = true;
            a.walker_ticks = 0;
            a.mechanical_valid = false;
            if(auto* io = reg.try_get<AgentIO>(e)) {
                io->done = io->truncated = false;
                std::fill(io->action.begin(), io->action.end(), 0.f);
            }
        }

        // Births
        for(auto e : animals(reg)) {
            auto& a = reg.get<SandboxAnimal>(e);
            SandboxSpecies& species = sb.species[a.species];
            if(a.dead || !a.pregnant || a.gestation < species.life.gestation * scale || species.alive >= species.life.max) continue;
            const auto& bot = reg.get<AnimalBot>(e);
            HeadingFrame frame = headingFrame(bot.rotation[0]);
            const float side = rng.uniform() < 0.5 ? 1.f : -1.f;
            glm::vec3 at = bot.position[0] + side * 1.2f * bot.body->length * frame.z;
            at.x = std::clamp(at.x, -0.95f * sb.params.arena, 0.95f * sb.params.arena);
            at.z = std::clamp(at.z, -0.95f * sb.params.arena, 0.95f * sb.params.arena);
            const int species_index = a.species;
            const entt::entity father = a.father;
            const float yaw = yawOf(bot.rotation[0]);
            a.pregnant = false;
            a.gestation = 0.f;
            a.father = entt::null;
            a.energy = std::max(0.02f, a.energy - species.life.birth_cost);
            a.objective += 1.f;
            a.window.births++;
            a.offspring++;
            species.born++;
            if(SandboxAnimal* f = living(reg, father)) {
                f->objective += 1.f;
                f->window.births++;
                f->offspring++;
            }
            // (the references above are not used after this: a new animal may move the components)
            spawnSandboxAnimal(reg, species_index, glm::vec2(at.x, at.z), yaw, 0.f);
        }

        // A species that is dying out gets adults from outside
        for(size_t s = 0; s < sb.species.size(); s++) {
            while(sb.species[s].alive < sb.species[s].life.min) {
                glm::vec2 at = freePlace(reg, sb, rng, true);
                const float yaw = std::atan2(at.x, at.y);     // toward the middle
                spawnSandboxAnimal(reg, (int) s, at, yaw, -1.f);
                sb.species[s].spawned++;
            }
        }

        // The scripted minds, for whoever asked and was not answered
        if(sb.heuristic) {
            for(auto [m, mind, io] : reg.view<SandboxMind, AgentIO>().each()) {
                if(!io.requested || io.done) continue;
                const auto* a = reg.try_get<SandboxAnimal>(mind.animal);
                if(!a) continue;
                sandboxHeuristic(sb.species[a->species], io);
                io.requested = false;
                io.stats.clear();
            }
        }

        // The minds' last decisions as commands and orders; the walkers' as drives
        for(auto e : animals(reg)) {
            auto& a = reg.get<SandboxAnimal>(e);
            if(a.dead) continue;
            auto& bot = reg.get<AnimalBot>(e);
            mindOrders(sb, a, bot, reg.valid(a.mind) ? reg.try_get<AgentIO>(a.mind) : nullptr);
            if(sb.puppet) {
                bot.decisions++;
                bot.window_ticks = 0;
            } else if(auto* io = reg.try_get<AgentIO>(e)) {
                driveAnimalBot(bot, io->action.data(), sb.params.weak + (1.f - sb.params.weak) * std::clamp(a.health, 0.f, 1.f));
            }
        }

    }

    // Every tick: the commands ease toward what was asked and turn
    for(auto [e, a, bot] : reg.view<SandboxAnimal, AnimalBot>().each()) {
        if(a.dead) continue;
        WalkCommand& c = bot.command;
        c.time += dt;
        c.speed += (c.wanted_speed - c.speed) * std::min(2.f * dt, 1.f);
        c.turn += (c.wanted_turn - c.turn) * std::min(2.f * dt, 1.f);
        c.yaw = wrapAngle(c.yaw + c.turn * c.speed * dt);
        if(sb.puppet) {
            const float limit = 0.97f * sb.params.arena;
            bot.origin += c.direction() * (c.speed * dt);
            bot.origin.x = std::clamp(bot.origin.x, -limit, limit);
            bot.origin.z = std::clamp(bot.origin.z, -limit, limit);
            placePuppet(reg, bot);
        }
    }

}



// SandboxLife

void SandboxLifeSys::tick(double h) {

    auto& sb = reg.ctx().get<Sandbox>();
    const float dt = (float) h;
    const float scale = sb.params.life_scale;
    sb.seconds += h;
    const std::vector<entt::entity> all = animals(reg);

    for(auto e : all) {
        auto& a = reg.get<SandboxAnimal>(e);
        auto& bot = reg.get<AnimalBot>(e);
        syncAnimalBot(reg, bot, h);
        if(a.dead) {
            a.rot += dt;
            continue;
        }
        const SandboxSpecies& species = sb.species[a.species];
        const SpeciesLife& life = species.life;
        const AnimalBody& body = *bot.body;
        a.walker_ticks++;
        a.window.ticks++;
        a.window.modes[std::clamp(bot.mode, 0, animal_modes - 1)]++;
        a.age += dt;
        a.lived += dt;
        a.attacked += dt;
        a.since_fed += dt;
        a.stun = std::max(0.f, a.stun - dt);

        // A fall ends the walker's episode, not the life: it costs health and the animal is stood up again
        if(!sb.puppet && !a.fell && !a.restand) {
            int segment = -1;
            if(int cause = bot.fallen(&segment)) {
                a.fell = true;
                bot.killed = true;
                bot.fall_cause = cause;
                bot.fall_segment = segment;
                a.health -= life.fall_damage;
                a.window.falls++;
                a.window.damage_taken += life.fall_damage;
                sb.species[a.species].falls++;
            }
        }

        // What moving costs: the positive changes of the bodies' mechanical energy (a puppet: the work a walk of
        // its speed is taken to cost)
        if(sb.puppet) {
            a.power = 0.1f * body.mass * 9.81f * glm::length(horizontal(bot.origin_velocity[0]));
        } else {
            const double energy = mechanicalEnergy(bot);
            const float power = a.mechanical_valid ? (float) std::max(energy - a.mechanical, 0.) / dt : 0.f;
            a.mechanical = energy;
            a.mechanical_valid = true;
            // Over a quarter of a second: a single step's jolt is not work
            a.power += (power - a.power) * std::min(4.f * dt, 1.f);
        }
        {
            SandboxSpecies& counted = sb.species[a.species];
            counted.work += a.power / (0.1f * body.mass * 9.81f * std::max(body.walk_speed, 0.05f));
            counted.moved += glm::length(horizontal(bot.origin_velocity[0])) * dt;
            counted.ticks++;
        }
        const float spent = energyDrain(life, scale, a.power, body.mass, body.walk_speed, bot.mode == (int) AnimalMode::Rest) * dt;
        a.energy -= spent;
        a.window.energy_spent += spent;

        // Feeding: the muzzle in the grass, or on a carcass
        a.feeding = false;
        const bool slow = glm::length(horizontal(bot.origin_velocity[0])) <= life.feed_speed * body.walk_speed;
        if(bot.mode == (int) AnimalMode::Feed && slow && a.stun <= 0.f && a.energy < 1.f) {
            const glm::vec3 muzzle = muzzlePoint(sb, bot);
            const float full_rate = dt / (life.feed * scale);
            float gain = 0.f;
            if(!life.predator) {
                const float grass = sb.grass.height(muzzle.x, muzzle.z);
                if(grass > sb.params.grass.stubble && muzzle.y <= grass + 0.05f) {
                    const float want = life.graze * dt;
                    gain = full_rate * sb.grass.eat(muzzle.x, muzzle.z, want) / want;
                }
            } else {
                for(auto other : all) {
                    if(other == e) continue;
                    auto& carcass = reg.get<SandboxAnimal>(other);
                    if(!carcass.dead || carcass.meat <= 0.f) continue;
                    const auto& carcass_bot = reg.get<AnimalBot>(other);
                    const float reach = life.carcass_reach * (body.height + carcass_bot.body->height);
                    bool touching = false;
                    for(const glm::vec3& p : carcass_bot.position) touching = touching || glm::distance(p, muzzle) <= reach;
                    if(!touching) continue;
                    gain = std::min(full_rate, carcass.meat);
                    carcass.meat -= gain;
                    break;
                }
            }
            if(gain > 0.f) {
                a.feeding = true;
                a.energy = std::min(1.f, a.energy + gain);
                a.window.energy_gain += gain;
                a.window.feeding++;
                a.since_fed = 0.f;
            }
        }

        a.health = std::min(1.f, a.health + dt / (life.heal * scale) * (bot.mode == (int) AnimalMode::Rest ? life.rest_heal : 1.f));
        if(a.pregnant) a.gestation += dt;

        // The next size
        if(!a.grow && a.stage + 1 < (int) species.stage_age.size() && a.age >= species.stage_age[a.stage + 1] * scale) a.grow = true;
    }

    // Wounds: the head of an animal in Attack mode on a body of another
    if(sb.puppet) {
        // No blows between puppets: a muzzle held on the other's trunk in Attack mode wounds, a full health in two seconds
        for(auto e : all) {
            auto& a = reg.get<SandboxAnimal>(e);
            const auto& bot = reg.get<AnimalBot>(e);
            if(a.dead || a.stun > 0.f || bot.mode != (int) AnimalMode::Attack) continue;
            const glm::vec3 muzzle = muzzlePoint(sb, bot);
            for(auto other : all) {
                if(other == e) continue;
                auto& victim = reg.get<SandboxAnimal>(other);
                if(victim.dead) continue;
                const auto& victim_bot = reg.get<AnimalBot>(other);
                if(glm::length(horizontal(victim_bot.position[0] - muzzle)) > 0.5f * victim_bot.body->length) continue;
                const float damage = sb.species[a.species].life.bite * dt / 2.f;
                victim.health -= damage;
                victim.window.damage_taken += damage;
                victim.attacker = e;
                victim.attacked = 0.f;
                a.window.damage_dealt += damage;
            }
        }
    } else if(auto* system = static_cast<SandboxSys::Contacts*>(physicsWorld(reg).system().GetContactListener())) {
        for(const auto& event : system->take()) {
            sb.contacts++;
            auto first = sb.by_group.find(event.group1), second = sb.by_group.find(event.group2);
            if(first == sb.by_group.end() || second == sb.by_group.end()) continue;
            for(int side = 0; side < 2; side++) {
                const entt::entity attacker_entity = side == 0 ? first->second : second->second;
                const entt::entity victim_entity = side == 0 ? second->second : first->second;
                const int segment = side == 0 ? event.segment1 : event.segment2;
                auto& attacker = reg.get<SandboxAnimal>(attacker_entity);
                auto& victim = reg.get<SandboxAnimal>(victim_entity);
                const auto& attacker_bot = reg.get<AnimalBot>(attacker_entity);
                if(attacker.dead || victim.dead || attacker.stun > 0.f) continue;
                if(attacker_bot.mode != (int) AnimalMode::Attack) continue;
                if(segment != attacker_bot.head_segment && segment != sb.species[attacker.species].weapon_segment) continue;
                const float damage = woundOf(sb.params, sb.species[attacker.species].life, event.impulse, reg.get<AnimalBot>(victim_entity).body->mass);
                if(damage <= 0.f) continue;
                sb.blows++;
                victim.health -= damage;
                victim.window.damage_taken += damage;
                victim.attacker = attacker_entity;
                victim.attacked = 0.f;
                attacker.window.damage_dealt += damage;
            }
        }
    }

    // Mating: a female and a male of the species, adults, both in Mate mode with enough of a store, close
    for(auto e : all) {
        auto& female = reg.get<SandboxAnimal>(e);
        if(female.dead || female.male) continue;
        const auto& bot = reg.get<AnimalBot>(e);
        const SpeciesLife& life = sb.species[female.species].life;
        bool together = false;
        if(!female.pregnant && female.stun <= 0.f && bot.mode == (int) AnimalMode::Mate && female.energy >= life.mate_energy && isAdult(sb, female)) {
            for(auto other : all) {
                auto& male = reg.get<SandboxAnimal>(other);
                if(male.dead || !male.male || male.species != female.species || male.stun > 0.f) continue;
                const auto& male_bot = reg.get<AnimalBot>(other);
                if(male_bot.mode != (int) AnimalMode::Mate || male.energy < life.mate_energy || !isAdult(sb, male)) continue;
                if(glm::length(horizontal(male_bot.position[0] - bot.position[0])) > life.mate_reach * bot.body->length) continue;
                together = true;
                female.mating += dt;
                male.mating = female.mating;
                if(female.mating >= life.mate_time * scale) {
                    female.pregnant = true;
                    female.gestation = 0.f;
                    female.father = other;
                    female.mating = male.mating = 0.f;
                    female.window.conceptions++;
                    male.window.conceptions++;
                }
                break;
            }
        }
        if(!together) female.mating = std::max(0.f, female.mating - dt);
    }

    // Deaths
    for(auto e : all) {
        auto& a = reg.get<SandboxAnimal>(e);
        if(a.dead) continue;
        const SpeciesLife& life = sb.species[a.species].life;
        // Wounds and falls: killed, whoever or whatever did it
        if(a.health <= 0.f) die(reg, sb, e, SandboxAnimal::Killed);
        else if(a.energy <= 0.f) die(reg, sb, e, SandboxAnimal::Starved);
        else if(a.age >= life.old * scale) die(reg, sb, e, SandboxAnimal::Old);
    }

    if(sb.draw) moveShapes(reg);

    // The grass grows back, a second at a time
    const auto& clock = reg.ctx().get<WorldClock>();
    const uint64_t every = (uint64_t) std::max(1., std::round(1. / clock.dt));
    if(clock.tick % every == every - 1) sb.grass.grow((float) (every * clock.dt) / scale);

}



// SandboxObserve

void SandboxObserveSys::tick(double h) {

    auto& sb = reg.ctx().get<Sandbox>();
    const auto& clock = reg.ctx().get<WorldClock>();

    if(auto* reset = reg.ctx().find<AgentsReset>(); reset && reset->requested) {
        reset->requested = false;
        const uint64_t seed = reg.ctx().find<WorldInfo>() ? reg.ctx().get<WorldInfo>().seed : 0;
        sb.grass = GrassField(sb.params.grass, sb.params.arena, seed);
        populate(reg, sb);
        sb.seconds = 0.;
    }
    sb.windows++;

    const float window_seconds = (float) (clock.decision_period * clock.dt);
    const int episode_ticks = (int) std::lround(sb.params.walker_episode / clock.dt);
    bool world_stats = false;
    std::vector<float> signals(SandboxSignal::count, 0.f);

    for(auto e : animals(reg)) {

        auto& a = reg.get<SandboxAnimal>(e);
        auto& bot = reg.get<AnimalBot>(e);
        if(a.dead && a.reported) continue;
        SandboxSpecies& species = sb.species[a.species];
        const bool died = a.dead;

        // The mind, when it is due or the animal died
        if(reg.valid(a.mind)) {
            auto& mind = reg.get<SandboxMind>(a.mind);
            auto& io = reg.get<AgentIO>(a.mind);
            const bool due = mind.fresh || (sb.windows + (uint64_t) mind.phase) % (uint64_t) sb.params.mind_every == 0;
            if(due || died) {
                if(mind.fresh) {
                    mind.fresh = false;
                    io.reward = 0.f;
                    io.done = io.truncated = false;
                } else {
                    const float seconds = (float) (a.window.ticks * clock.dt);
                    sandboxSignals(reg, e, seconds, signals.data());
                    const float scale = sb.params.reward_scale * species.rewards.scale;
                    float shaped = 0.f;
                    for(const auto& term : species.rewards.terms) {
                        const float value = scale * term.expression.eval(signals.data());
                        shaped += value;
                        io.stats.emplace_back(statKey("Rewards", species, term.name), value);
                    }
                    const float reward = a.objective + shaped;
                    io.stats.emplace_back(statKey("Rewards", species, "Objective"), a.objective);
                    io.stats.emplace_back(statKey("Rewards", species, "Shaped"), shaped);
                    io.stats.emplace_back(statKey("Life", species, "Energy"), a.energy);
                    io.stats.emplace_back(statKey("Life", species, "Health"), a.health);
                    io.stats.emplace_back(statKey("Life", species, "Feeding"), signals[SandboxSignal::feeding]);
                    io.stats.emplace_back(statKey("Life", species, "Speed"), signals[SandboxSignal::speed]);
                    io.stats.emplace_back(statKey("Life", species, "Population"), (float) species.alive);
                    for(int m = 0; m < animal_modes; m++) {
                        io.stats.emplace_back(statKey("Mode", species, animalModeName(m)), signals[SandboxSignal::mode_move + m]);
                    }
                    // Events: summed by the trainer over a summary period
                    const int sum = 2;
                    if(a.window.births > 0) io.stats.emplace_back(statKey("Life", species, "Births"), (float) a.window.births, sum);
                    if(a.window.conceptions > 0) io.stats.emplace_back(statKey("Life", species, "Conceptions"), (float) a.window.conceptions, sum);
                    if(a.window.kills > 0) io.stats.emplace_back(statKey("Life", species, "Kills"), (float) a.window.kills, sum);
                    if(a.window.falls > 0) io.stats.emplace_back(statKey("Life", species, "Falls"), (float) a.window.falls, sum);
                    if(a.window.damage_dealt > 0.f) io.stats.emplace_back(statKey("Life", species, "Damage dealt"), a.window.damage_dealt, sum);
                    a.lifetime_reward += reward;
                    if(died) {
                        io.stats.emplace_back(statKey("Life", species, "Lifetime"), a.lived);
                        io.stats.emplace_back(statKey("Life", species, "Offspring"), (float) a.offspring);
                        io.stats.emplace_back(statKey("Life", species, "Return"), a.lifetime_reward);
                        io.stats.emplace_back(statKey("Life", species, "Died starved"), a.cause == SandboxAnimal::Starved ? 1.f : 0.f);
                        io.stats.emplace_back(statKey("Life", species, "Died killed"), a.cause == SandboxAnimal::Killed ? 1.f : 0.f);
                        io.stats.emplace_back(statKey("Life", species, "Died old"), a.cause == SandboxAnimal::Old ? 1.f : 0.f);
                        io.stats.emplace_back(statKey("Life", species, "Deaths"), 1.f, sum);
                    }
                    if(!world_stats) {
                        world_stats = true;
                        io.stats.emplace_back("Sandbox/Grass standing", sb.grass.standing());
                        io.stats.emplace_back("Sandbox/Life scale", sb.params.life_scale);
                    }
                    io.reward = reward;
                    io.done = died;
                    io.truncated = died && a.cause == SandboxAnimal::Old;
                    a.share = sb.params.walker_share * reward;
                    a.objective = 0.f;
                }
                a.window = SandboxAnimal::Window();
                a.window.from = bot.position[0];
                observeSandboxMind(reg, e, io);
                io.requested = true;
            }
        }

        // The walker, every window
        if(auto* io = sb.puppet ? nullptr : reg.try_get<AgentIO>(e)) {
            const AnimalParams& p = bot.params;
            bool new_episode = false;
            if(a.walker_fresh) {
                a.walker_fresh = false;
                io->reward = 0.f;
                io->done = io->truncated = false;
            } else {
                const AnimalRating rating = rateAnimal(bot, std::max(bot.window_ticks, 1) * clock.dt);
                const bool time = a.walker_ticks >= episode_ticks;
                const bool terminal = a.fell || (died && a.cause != SandboxAnimal::Old);
                float reward = p.life_reward + rating.root + rating.head + rating.mode;
                // A fall takes the reward of its step, as for the animal alone
                if(a.fell) reward = p.death_penalty;
                reward = reward * p.reward_factor + a.share;
                a.share = 0.f;
                io->reward = reward;
                io->done = terminal || died || a.grow || time;
                io->truncated = io->done && !terminal;
                const int histogram = 3;
                io->stats.emplace_back(statKey("Walker", species, "Root Reward"), rating.root, histogram);
                io->stats.emplace_back(statKey("Walker", species, "Order Reward"), rating.head + rating.mode, histogram);
                io->stats.emplace_back(statKey("Walker", species, "Head Error"), glm::degrees(rating.head_error), histogram);
                io->stats.emplace_back(statKey("Walker", species, "Commanded Speed"), bot.snapshot_speed / std::max(bot.body->walk_speed, 1e-3f), histogram);
                if(bot.snapshot_speed > 0.2f * bot.body->walk_speed) {
                    io->stats.emplace_back(statKey("Walker", species, "Speed Ratio"), rating.along / bot.snapshot_speed, histogram);
                }
                if(io->done) io->stats.emplace_back(statKey("Walker", species, "Fell"), a.fell ? 1.f : 0.f);
                if(io->done && !died) {
                    if(a.fell || a.grow) a.restand = true;
                    else {
                        // By time: the animal goes on, the walker's episode starts again from here
                        a.walker_ticks = 0;
                        a.walker_fresh = true;
                        new_episode = true;
                    }
                }
            }
            a.fell = false;
            bot.killed = false;

            bot.snapshot_trunk = bot.position[0];
            bot.snapshot_direction = bot.command.direction();
            bot.snapshot_speed = bot.command.speed;
            bot.snapshot_mode = bot.mode;
            bot.snapshot_head = bot.head;
            std::vector<float> current((size_t) bot.frame_size);
            observeAnimal(bot, current.data());
            for(float& f : current) if(!std::isfinite(f)) f = 0.f;
            std::copy(bot.previous_frame.begin(), bot.previous_frame.end(), io->observation.begin());
            std::copy(current.begin(), current.end(), io->observation.begin() + bot.frame_size);
            bot.previous_frame = current;
            if(new_episode) std::fill(bot.previous_frame.begin(), bot.previous_frame.end(), 0.f);
            observeOrder(bot, io->sensors[0].data.data());
            io->requested = true;
        } else if(sb.puppet) {
            bot.snapshot_trunk = bot.position[0];
            // A puppet grows where it stands
            if(a.grow && !died) a.restand = true;
        }

        if(died) a.reported = true;

    }

}



// SandboxHeuristic

void SandboxHeuristicSys::tick(double h) {
    auto& sb = reg.ctx().get<Sandbox>();
    for(auto [e, mind, io] : reg.view<SandboxMind, AgentIO>().each()) {
        if(!io.requested || io.done) continue;
        const auto* a = reg.try_get<SandboxAnimal>(mind.animal);
        if(!a) continue;
        sandboxHeuristic(sb.species[a->species], io);
        io.requested = false;
        io.stats.clear();
    }
}



void dy::registerSandbox() {
    registerSystem("Sandbox", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<SandboxSys>(reg, params); },
        SystemScope::World });
    registerSystem("SandboxLife", systemInfo<SandboxLifeSys>(SystemScope::World));
    registerSystem("SandboxObserve", systemInfo<SandboxObserveSys>(SystemScope::World));
    registerSystem("SandboxHeuristic", systemInfo<SandboxHeuristicSys>(SystemScope::World));
}
