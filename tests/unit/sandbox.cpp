#include <doctest/doctest.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "logic/agents/agent_io.h"
#include "logic/animals/animal_agent.h"
#include "logic/animals/animal_body.h"
#include "logic/sandbox/expr.h"
#include "logic/sandbox/life.h"
#include "logic/sandbox/sandbox.h"
#include "physics/physics_world.h"
#include "scene/scenario_runner.h"
#include "scene/world.h"
#include "util/paths.h"

using namespace dy;

// The sandbox (docs/action-agents.md): expressions of the reward files, rules of life, the systems. Tests on the
// generated bodies of the deer and the wolf are skipped without them

namespace {

using Vars = std::vector<std::pair<std::string, std::string>>;

bool haveBodies() {
    return std::filesystem::exists(libraryResourceFile("animals/bodies/deer_red.body.json"))
        && std::filesystem::exists(libraryResourceFile("animals/bodies/wolf_2.body.json"));
}

float eval(const std::string& text, const std::vector<std::string>& names = {}, const std::vector<float>& values = {}) {
    return Expression::parse(text, names).eval(values.data());
}

std::vector<entt::entity> animalsOf(World& w, const std::string& species, bool alive = true) {
    auto& sb = w.reg.ctx().get<Sandbox>();
    const int index = sb.speciesIndex(species);
    std::vector<entt::entity> list;
    for(auto e : w.reg.view<SandboxAnimal, AnimalBot>()) {
        const auto& a = w.reg.get<SandboxAnimal>(e);
        if(a.species == index && a.dead != alive) list.push_back(e);
    }
    std::sort(list.begin(), list.end());
    return list;
}

// A puppet where a test wants it, heading `yaw`
void put(World& w, entt::entity e, glm::vec2 at, float yaw) {
    auto& bot = w.reg.get<AnimalBot>(e);
    bot.origin = glm::vec3(at.x, 0.f, at.y);
    bot.command.yaw = yaw;
    bot.command.speed = bot.command.wanted_speed = 0.f;
    standAnimalBot(w.reg, bot, bot.origin, yaw, false);
}

// What a mind decided, held until it is changed: speed, turn, head yaw, head pitch, mode
void decide(World& w, entt::entity animal, std::vector<float> action, AnimalMode mode) {
    auto& io = w.reg.get<AgentIO>(w.reg.get<SandboxAnimal>(animal).mind);
    io.action = std::move(action);
    io.discrete = { (int) mode };
}

void seconds(World& w, double s) {
    const int ticks = (int) std::lround(s / w.reg.ctx().get<WorldClock>().dt);
    for(int t = 0; t < ticks; t++) w.tick();
}

}

TEST_SUITE("unit") {

TEST_CASE("EXPR.1 expressions: arithmetic, precedence, comparisons, functions and signals") {
    CHECK(eval("1 + 2 * 3") == doctest::Approx(7.f));
    CHECK(eval("(1 + 2) * 3") == doctest::Approx(9.f));
    CHECK(eval("-2 * -3") == doctest::Approx(6.f));
    CHECK(eval("10 / 4 - 1") == doctest::Approx(1.5f));
    CHECK(eval("2 - 3 - 4") == doctest::Approx(-5.f));
    CHECK(eval("1.5e1 + .5") == doctest::Approx(15.5f));
    CHECK(eval("3 > 2") == 1.f);
    CHECK(eval("3 <= 2") == 0.f);
    CHECK(eval("1 + 1 == 2") == 1.f);
    CHECK(eval("2 != 2") == 0.f);
    CHECK(eval("max(0, 1 - 30 / 15)") == 0.f);
    CHECK(eval("min(4, 2) + abs(-3) + sqrt(16) + pow(2, 3)") == doctest::Approx(17.f));
    CHECK(eval("clamp(5, 0, 1) + clamp(-5, 0, 1) + clamp(0.25, 0, 1)") == doctest::Approx(1.25f));
    CHECK(eval("step(0.1) + step(0) + step(-1)") == 1.f);
    CHECK(eval("sign(-4) + sign(0) + sign(9)") == 0.f);
    CHECK(eval("if(2 > 1, 10, 20) + if(0, 10, 20)") == doctest::Approx(30.f));
    CHECK(eval("exp(0) + log(1)") == doctest::Approx(1.f));
    // What is not a number is 0, not a reward of infinity
    CHECK(eval("1 / 0") == 0.f);
    CHECK(eval("log(-1) + sqrt(-4)") == 0.f);
    CHECK(eval("exp(1000)") == 0.f);

    const std::vector<std::string> names = { "energy", "energy_gain", "predator_distance" };
    Expression e = Expression::parse("0.2 * energy_gain - 0.02 * max(0, 1 - predator_distance / 15)", names);
    const float near[] = { 0.5f, 0.1f, 3.f }, far[] = { 0.5f, 0.1f, 400.f };
    CHECK(e.eval(near) == doctest::Approx(0.02f - 0.02f * 0.8f));
    CHECK(e.eval(far) == doctest::Approx(0.02f));
    CHECK(e.uses() == std::vector<int> { 1, 2 });
}

TEST_CASE("EXPR.2 a fault is an error that says where") {
    const std::vector<std::string> names = { "energy" };
    CHECK_THROWS_AS(Expression::parse("1 +", names), ExprError);
    CHECK_THROWS_AS(Expression::parse("(1 + 2", names), ExprError);
    CHECK_THROWS_AS(Expression::parse("1 2", names), ExprError);
    CHECK_THROWS_AS(Expression::parse("max(1)", names), ExprError);
    CHECK_THROWS_AS(Expression::parse("max(1, 2, 3)", names), ExprError);
    CHECK_THROWS_AS(Expression::parse("energi * 2", names), ExprError);
    CHECK_THROWS_AS(Expression::parse("2 $ 3", names), ExprError);
    try {
        Expression::parse("0.1 * energi", names);
        FAIL("no error");
    } catch(const ExprError& e) {
        CHECK(std::string(e.what()).find("energi") != std::string::npos);
        CHECK(std::string(e.what()).find("character 7") != std::string::npos);
    }
    // An empty expression is worth nothing
    CHECK_THROWS_AS(Expression::parse("", names), ExprError);
    CHECK(Expression().eval(nullptr) == 0.f);
}

TEST_CASE("EXPR.3 the reward files of the repository parse against the sandbox's signals") {
    for(const char* species : { "deer_red", "wolf_2" }) {
        RewardProgram p = RewardProgram::load(libraryResourceFile(std::string("agents/rewards/") + species + ".rewards.json").string());
        CHECK(p.round >= 1);
        CHECK(!p.terms.empty());
        CHECK(!p.file_hash.empty());
        std::vector<float> zero(SandboxSignal::count, 0.f);
        for(auto& term : p.terms) CHECK(std::isfinite(term.expression.eval(zero.data())));
    }
    CHECK(sandboxSignalNames().size() == (size_t) SandboxSignal::count);
    CHECK(sandboxSignalNames()[SandboxSignal::dt] == "dt");
    CHECK(sandboxSignalNames()[SandboxSignal::mode_rest] == "mode_rest");
    // A file that is not there: the objective alone
    CHECK(RewardProgram::load("no/such/file.json").terms.empty());
    CHECK_THROWS(RewardProgram::parse(nlohmann::json::parse(R"({ "terms": [ { "name": "x", "expr": "nothing * 2" } ] })")));
    CHECK_THROWS(RewardProgram::parse(nlohmann::json::parse(R"({ "terms": [ { "expr": "1" } ] })")));
}

TEST_CASE("LIFE.1 energy: a store lasts `starve` seconds standing, less working, longer resting, and the life scale stretches it") {
    SpeciesLife s;
    s.starve = 180.f;
    s.work = 1.f;
    s.rest_drain = 0.6f;
    const float mass = 300.f, walk = 1.f;
    CHECK(energyDrain(s, 1.f, 0.f, mass, walk, false) * 180.f == doctest::Approx(1.f));
    // At 0.1 m g v watts: as much again
    CHECK(energyDrain(s, 1.f, 0.1f * mass * 9.81f * walk, mass, walk, false) * 180.f == doctest::Approx(2.f));
    CHECK(energyDrain(s, 1.f, 0.f, mass, walk, true) * 180.f == doctest::Approx(0.6f));
    CHECK(energyDrain(s, 3.f, 0.f, mass, walk, false) * 540.f == doctest::Approx(1.f));
    // Negative work gives nothing back
    CHECK(energyDrain(s, 1.f, -500.f, mass, walk, false) * 180.f == doctest::Approx(1.f));
}

TEST_CASE("LIFE.2 a wound is the blow's impulse over the victim's mass, above a threshold, times the attacker's bite") {
    LifeParams p;
    p.wound_threshold = 0.02f;
    p.kill_impulse = 1.f;
    SpeciesLife wolf, deer;
    wolf.bite = 1.f;
    deer.bite = 0.3f;
    CHECK(woundOf(p, wolf, 0.02f * 300.f, 300.f) == 0.f);
    CHECK(woundOf(p, wolf, 60.f, 300.f) == doctest::Approx(0.18f));
    CHECK(woundOf(p, deer, 60.f, 300.f) == doctest::Approx(0.054f));
    // The same blow on a smaller animal wounds more
    CHECK(woundOf(p, wolf, 60.f, 100.f) == doctest::Approx(0.58f));
}

TEST_CASE("LIFE.3 the grass: patches over about its cover, eaten down to the stubble where a muzzle feeds, growing back") {
    GrassParams p;
    GrassField grass(p, 100.f, 7);
    CHECK(grass.size() == 200);
    CHECK(grass.coverPart() == doctest::Approx(p.cover).epsilon(0.03));
    CHECK(grass.standing() == doctest::Approx(1.f));
    CHECK(grass.height(1000.f, 0.f) == 0.f);

    // A cell with grass and a bare one
    glm::vec2 with(0.f), bare(0.f);
    bool found_with = false, found_bare = false;
    for(float x = -95.f; x < 95.f && !(found_with && found_bare); x += 1.f) {
        for(float z = -95.f; z < 95.f; z += 1.f) {
            if(!found_with && grass.full(x, z) > 0.f) { with = glm::vec2(x, z); found_with = true; }
            if(!found_bare && grass.full(x, z) == 0.f) { bare = glm::vec2(x, z); found_bare = true; }
        }
    }
    REQUIRE(found_with);
    REQUIRE(found_bare);
    const float full = grass.height(with.x, with.y);
    CHECK(full >= p.shortest);
    CHECK(full <= p.tallest);
    CHECK(grass.eat(bare.x, bare.y, 1.f) == 0.f);
    CHECK(grass.eat(with.x, with.y, 0.1f) == doctest::Approx(0.1f));
    CHECK(grass.height(with.x, with.y) == doctest::Approx(full - 0.1f));
    // Never under the stubble
    CHECK(grass.eat(with.x, with.y, 5.f) == doctest::Approx(full - 0.1f - p.stubble));
    CHECK(grass.height(with.x, with.y) == doctest::Approx(p.stubble));
    CHECK(grass.eat(with.x, with.y, 5.f) < 1e-6f);
    CHECK(grass.standing() < 1.f);
    // Half its regrow time later it is half way back, then full and no more
    grass.grow(0.5f * p.regrow);
    CHECK(grass.height(with.x, with.y) == doctest::Approx(p.stubble + 0.5f * full));
    grass.grow(p.regrow);
    CHECK(grass.height(with.x, with.y) == doctest::Approx(full));
    // Another world has other patches
    GrassField other(p, 100.f, 8);
    int differ = 0;
    for(float x = -90.f; x < 90.f; x += 10.f) differ += (other.full(x, 3.f) > 0.f) != (grass.full(x, 3.f) > 0.f) ? 1 : 0;
    CHECK(differ > 0);
}

TEST_CASE("LIFE.4 the life params of the repository: deer graze and wolves hunt, and a bad file is refused") {
    LifeParams p = LifeParams::load(libraryResourceFile("agents/params/life.params.json").string());
    REQUIRE(p.speciesIndex("deer_red") >= 0);
    REQUIRE(p.speciesIndex("wolf_2") >= 0);
    const SpeciesLife& deer = p.species[p.speciesIndex("deer_red")];
    const SpeciesLife& wolf = p.species[p.speciesIndex("wolf_2")];
    CHECK(!deer.predator);
    CHECK(wolf.predator);
    CHECK(deer.start == 12);
    CHECK(wolf.start == 4);
    CHECK(deer.max == 24);
    CHECK(wolf.max == 8);
    // The defaults block reaches both
    CHECK(deer.gestation == 60.f);
    CHECK(wolf.gestation == 60.f);
    CHECK(wolf.starve > deer.starve);
    CHECK(wolf.bite > deer.bite);
    CHECK(deer.fov > wolf.fov);
    CHECK(p.speciesIndex("unicorn") == -1);

    auto j = nlohmann::json::parse(R"({ "format_version": 1, "kind": "life_params", "species": { "x": { "starv": 10 } } })");
    CHECK_THROWS(LifeParams::parse(j));
    j = nlohmann::json::parse(R"({ "format_version": 1, "kind": "life_params", "species": { "x": { "min": 5, "max": 2 } } })");
    CHECK_THROWS(LifeParams::parse(j));
    j = nlohmann::json::parse(R"({ "format_version": 1, "kind": "life_params" })");
    CHECK_THROWS(LifeParams::parse(j));
}

TEST_CASE("LIFE.5 a body at another size: lengths, masses and drives scale and it keeps its layout") {
    if(!haveBodies()) return;
    auto adult = loadAnimalBody(animalBodyPath("deer_red"));
    auto young = scaleAnimalBody(*adult, 0.6f);
    CHECK(young->mass == doctest::Approx(adult->mass * 0.216f));
    CHECK(young->stand_height == doctest::Approx(adult->stand_height * 0.6f));
    CHECK(young->walk_speed == doctest::Approx(adult->walk_speed * std::sqrt(0.6f)));
    REQUIRE(young->def.segments.size() == adult->def.segments.size());
    REQUIRE(young->def.joints.size() == adult->def.joints.size());
    CHECK(young->behavior == adult->behavior);
    for(size_t i = 0; i < adult->def.segments.size(); i++) {
        CHECK(young->def.segments[i].percentage == doctest::Approx(adult->def.segments[i].percentage));
        CHECK(glm::length(young->def.segments[i].spawn_position - 0.6f * adult->def.segments[i].spawn_position) < 1e-5f);
    }
    CHECK(young->drives[0].stiffness == doctest::Approx(adult->drives[0].stiffness * 0.1296f));
    CHECK(young->drives[0].stand == adult->drives[0].stand);
}

}

TEST_SUITE("scenario") {

TEST_CASE("SANDBOX.1 a world of the sandbox: twelve deer and four wolves, each a ragdoll with a walker and a mind") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    REQUIRE(sb.species.size() == 2);
    CHECK(animalsOf(w, "deer_red").size() == 12);
    CHECK(animalsOf(w, "wolf_2").size() == 4);
    CHECK(sb.species[sb.speciesIndex("deer_red")].alive == 12);
    CHECK(w.reg.ctx().find<AgentsReset>() != nullptr);

    std::map<std::string, int> behaviours;
    for(auto e : w.reg.view<AgentIO>()) behaviours[w.reg.get<AgentIO>(e).behavior]++;
    CHECK(behaviours["Animal_deer_red"] == 12);
    CHECK(behaviours["Action_deer_red"] == 12);
    CHECK(behaviours["Animal_wolf_2"] == 4);
    CHECK(behaviours["Action_wolf_2"] == 4);

    std::map<uint32_t, int> groups;
    int males = 0;
    for(auto e : animalsOf(w, "deer_red")) {
        const auto& a = w.reg.get<SandboxAnimal>(e);
        const auto& bot = w.reg.get<AnimalBot>(e);
        groups[a.group]++;
        males += a.male ? 1 : 0;
        CHECK(a.group > 0);
        CHECK((sb.by_group.at(a.group) == e));
        CHECK(a.energy >= 0.6f);
        CHECK(a.health == 1.f);
        // Adults at the start
        CHECK(a.stage == 2);
        CHECK(bot.orders);
        CHECK(std::abs(bot.position[0].x) < sb.params.arena);
        CHECK(std::abs(bot.position[0].z) < sb.params.arena);
        // The head: its segment, a muzzle ahead of and under its origin, pointing forward when it stands
        CHECK(bot.body->def.segments[bot.head_segment].name == "head");
        CHECK(bot.head_stand.x > 0.5f);
        const auto& walker = w.reg.get<AgentIO>(e);
        CHECK(walker.observation_size == 2 * (animal_header + 9 * 22));
        CHECK(walker.action_size == 8 * 21);
        REQUIRE(walker.sensors.size() == 1);
        CHECK(walker.sensors[0].name == "nogail_order");
        CHECK(walker.sensors[0].size == animal_order_size);
        const auto& mind = w.reg.get<AgentIO>(a.mind);
        CHECK(mind.observation_size == MindObs::size_total);
        CHECK(mind.action_size == mind_actions);
        REQUIRE(mind.branches.size() == 1);
        CHECK(mind.branches[0] == animal_modes);
        REQUIRE(mind.sensors.size() == 1);
        CHECK(mind.sensors[0].rows == 16);
        CHECK(mind.sensors[0].size == MindObs::row);
    }
    CHECK(groups.size() == 12);
    CHECK(males == 6);
}

TEST_CASE("SANDBOX.2 with zero actions the animals stand: no fall in ten seconds, a deer's store goes down at its basal rate") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    std::map<entt::entity, float> before;
    for(auto e : animalsOf(w, "deer_red")) before[e] = w.reg.get<SandboxAnimal>(e).energy;
    seconds(w, 10.);
    const auto& deer = sb.species[sb.speciesIndex("deer_red")];
    const auto& wolves = sb.species[sb.speciesIndex("wolf_2")];
    MESSAGE("falls in 10 s standing: deer " << deer.falls << ", wolves " << wolves.falls);
    CHECK(deer.falls == 0);
    CHECK(wolves.falls == 0);
    for(auto e : animalsOf(w, "deer_red")) {
        const auto& a = w.reg.get<SandboxAnimal>(e);
        const auto& bot = w.reg.get<AnimalBot>(e);
        // Standing costs the basal rate and little more
        const float spent = before[e] - a.energy;
        CHECK(spent >= 10.f / 180.f * 0.99f);
        CHECK(spent < 10.f / 180.f * 1.5f);
        CHECK(bot.trunkHeight() > 0.9f * bot.body->stand_height);
        // The first mode is Move, the walkers saw their order
        const auto& walker = w.reg.get<AgentIO>(e);
        CHECK(walker.sensors[0].data[2] == 1.f);
        CHECK(walker.requested);
    }
}

TEST_CASE("SANDBOX.3 a wolf's head thrown on a deer wounds it in Attack mode only") {
    if(!haveBodies()) return;
    for(int attack = 0; attack < 2; attack++) {
        ScenarioRunner runner("sandbox_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
        auto& w = runner.world(0);
        auto& bodies = w.reg.ctx().get<PhysicsWorld>().bodies();
        entt::entity wolf = animalsOf(w, "wolf_2")[0], deer = animalsOf(w, "deer_red")[0];
        // The deer ahead of the wolf, facing the same way, a hind leg in the line of the wolf's head (the wolf looks down -z)
        {
            auto& bot = w.reg.get<AnimalBot>(wolf);
            standAnimalBot(w.reg, bot, glm::vec3(50.f, 0.f, 50.f), 0.f, false);
            auto& victim = w.reg.get<AnimalBot>(deer);
            standAnimalBot(w.reg, victim, glm::vec3(50.2f, 0.f, 47.9f), 0.f, false);
        }
        decide(w, wolf, { -1.f, 0.f, 0.f, 0.f }, attack ? AnimalMode::Attack : AnimalMode::Move);
        seconds(w, 0.1);
        // The whole wolf thrown forward at 5 m/s: its head is what arrives first
        const auto& bot = w.reg.get<AnimalBot>(wolf);
        for(auto id : bot.parts.bodies) bodies.SetLinearVelocity(id, JPH::Vec3(0.f, 0.f, -5.f));
        seconds(w, 0.6);
        const auto& victim = w.reg.get<SandboxAnimal>(deer);
        const auto& attacker = w.reg.get<SandboxAnimal>(wolf);
        MESSAGE(std::string(attack ? "attack" : "move") << ": the deer health " << victim.health << ", contacts " << w.reg.ctx().get<Sandbox>().contacts << ", blows " << w.reg.ctx().get<Sandbox>().blows << ", wolf head z " << bot.position[bot.head_segment].z << " mode " << bot.mode << " stun " << attacker.stun << " fell " << w.reg.ctx().get<Sandbox>().species[1].falls);
        if(attack) {
            CHECK(victim.health < 1.f);
            CHECK((victim.attacker == wolf));
            CHECK(victim.attacked < 1.f);
        } else {
            // A bump: the fall it may cause costs health, the contact does not
            CHECK((victim.attacker == entt::null));
            CHECK(attacker.window.damage_dealt == 0.f);
        }
    }
}

TEST_CASE("SANDBOX.4 a fall ends the walker's episode and not the life: health lost, stood up again, stunned") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    auto& bodies = w.reg.ctx().get<PhysicsWorld>().bodies();
    entt::entity deer = animalsOf(w, "deer_red")[0];
    seconds(w, 0.2);
    // Knocked over sideways
    {
        const auto& bot = w.reg.get<AnimalBot>(deer);
        for(auto id : bot.parts.bodies) bodies.SetLinearVelocity(id, JPH::Vec3(6.f, 0.f, 0.f));
        bodies.SetAngularVelocity(bot.parts.bodies[0], JPH::Vec3(0.f, 0.f, -6.f));
    }
    bool terminal = false, stood = false;
    for(int t = 0; t < 240 && !stood; t++) {
        w.tick();
        const auto& a = w.reg.get<SandboxAnimal>(deer);
        const auto& io = w.reg.get<AgentIO>(deer);
        if(io.done && !terminal) {
            terminal = true;
            // A fall is a terminal state of the walker, not a limit
            CHECK(!io.truncated);
            CHECK(a.restand);
            CHECK(!a.dead);
            CHECK(a.health == doctest::Approx(1.f - sb.species[a.species].life.fall_damage).epsilon(0.02));
        } else if(terminal && !io.done) {
            stood = true;
            CHECK(a.stun > 0.f);
            CHECK(io.reward == 0.f);
            const auto& bot = w.reg.get<AnimalBot>(deer);
            CHECK(bot.trunkHeight() > 0.9f * bot.body->stand_height);
            CHECK(bot.mode == (int) AnimalMode::Rest);
        }
    }
    CHECK(terminal);
    CHECK(stood);
    CHECK(sb.species[sb.speciesIndex("deer_red")].falls >= 1);
    // Its mind lives on: the same agent, not ended
    const auto& a = w.reg.get<SandboxAnimal>(deer);
    REQUIRE(w.reg.valid(a.mind));
    CHECK(!w.reg.get<AgentIO>(a.mind).done);
}

TEST_CASE("SANDBOX.5 a death: both agents end, the carcass stays with its meat, the agents leave at the next window") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_train", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    entt::entity deer = animalsOf(w, "deer_red")[0];
    seconds(w, 0.5);
    const entt::entity mind = w.reg.get<SandboxAnimal>(deer).mind;
    w.reg.get<SandboxAnimal>(deer).energy = 1e-6f;
    // The window it starves in
    bool ended = false;
    for(int t = 0; t < 8 && !ended; t++) {
        w.tick();
        ended = w.reg.get<SandboxAnimal>(deer).reported;
    }
    REQUIRE(ended);
    {
        const auto& a = w.reg.get<SandboxAnimal>(deer);
        CHECK(a.dead);
        CHECK(a.cause == SandboxAnimal::Starved);
        CHECK(a.meat == doctest::Approx(3.f));
        const auto& walker = w.reg.get<AgentIO>(deer);
        const auto& io = w.reg.get<AgentIO>(mind);
        CHECK(walker.done);
        CHECK(!walker.truncated);
        CHECK(io.done);
        CHECK(!io.truncated);
        // The objective: -1 for a death before old age, and whatever the reward file adds
        CHECK(io.reward < -0.9f);
        CHECK(io.reward > -1.1f);
        CHECK(sb.species[a.species].alive == 11);
        CHECK(sb.species[a.species].starved == 1);
    }
    w.tick();
    CHECK(!w.reg.valid(mind));
    REQUIRE(w.reg.valid(deer));
    CHECK(!w.reg.all_of<AgentIO>(deer));
    CHECK(w.reg.get<AnimalBot>(deer).limp);
    CHECK(animalsOf(w, "deer_red", false).size() == 1);
    // Limp: a second later it lies on the ground
    seconds(w, 1.5);
    const auto& bot = w.reg.get<AnimalBot>(deer);
    CHECK(bot.trunkHeight() < 0.6f * bot.body->stand_height);
}

TEST_CASE("SANDBOX.6 puppets: a deer with its muzzle in the grass feeds and the grass gets shorter; a wolf feeds on a carcass") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_puppet", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    REQUIRE(sb.puppet);
    // No walkers in a puppet world: the minds only
    std::map<std::string, int> behaviours;
    for(auto e : w.reg.view<AgentIO>()) behaviours[w.reg.get<AgentIO>(e).behavior]++;
    CHECK(behaviours.size() == 2);
    CHECK(behaviours["Action_deer_red"] == 12);

    entt::entity deer = animalsOf(w, "deer_red")[0];
    // Where its muzzle is over grass
    glm::vec2 place(0.f);
    bool found = false;
    for(float x = -80.f; x < 80.f && !found; x += 1.f) {
        put(w, deer, glm::vec2(x, 20.f), 0.f);
        const glm::vec3 muzzle = w.reg.get<AnimalBot>(deer).muzzlePoint();
        if(sb.grass.height(muzzle.x, muzzle.z) > 0.5f && sb.grass.height(muzzle.x, muzzle.z - 1.f) > 0.5f) {
            place = glm::vec2(x, 20.f);
            found = true;
        }
    }
    REQUIRE(found);
    const glm::vec3 muzzle = w.reg.get<AnimalBot>(deer).muzzlePoint();
    const float grass_before = sb.grass.height(muzzle.x, muzzle.z);
    w.reg.get<SandboxAnimal>(deer).energy = 0.3f;

    // Head up: in Feed mode but the muzzle over the grass, nothing eaten
    decide(w, deer, { -1.f, 0.f, 0.f, 0.5f }, AnimalMode::Feed);
    seconds(w, 1.);
    CHECK(w.reg.get<SandboxAnimal>(deer).energy < 0.3f);
    CHECK(sb.grass.height(muzzle.x, muzzle.z) == doctest::Approx(grass_before));
    // Head down, another mode: nothing either
    decide(w, deer, { -1.f, 0.f, 0.f, -1.f }, AnimalMode::Move);
    seconds(w, 1.);
    CHECK(sb.grass.height(muzzle.x, muzzle.z) == doctest::Approx(grass_before));
    // Head down in Feed mode: the store fills at 1 / feed a second while the cell lasts
    const float energy = w.reg.get<SandboxAnimal>(deer).energy;
    decide(w, deer, { -1.f, 0.f, 0.f, -1.f }, AnimalMode::Feed);
    seconds(w, 1.);
    const auto& a = w.reg.get<SandboxAnimal>(deer);
    CHECK(a.energy > energy + 0.8f / 30.f - 1.5f / 180.f);
    CHECK(sb.grass.height(muzzle.x, muzzle.z) == doctest::Approx(grass_before - 0.3f).epsilon(0.05));
    CHECK(a.since_fed < 0.1f);
    // The cell is eaten down in a few seconds: then nothing more comes from it
    seconds(w, 4.);
    // (a second of growing back since)
    CHECK(sb.grass.height(muzzle.x, muzzle.z) < sb.params.grass.stubble + 0.01f);
    CHECK(!w.reg.get<SandboxAnimal>(deer).feeding);

    // A carcass, and a hungry wolf with its muzzle on it
    entt::entity prey = animalsOf(w, "deer_red")[1], wolf = animalsOf(w, "wolf_2")[0];
    put(w, prey, glm::vec2(-50.f, -50.f), 0.f);
    w.reg.get<SandboxAnimal>(prey).health = -1.f;
    seconds(w, 0.2);
    REQUIRE(w.reg.get<SandboxAnimal>(prey).dead);
    CHECK(w.reg.get<SandboxAnimal>(prey).cause == SandboxAnimal::Killed);
    put(w, wolf, glm::vec2(-50.f, -48.5f), 0.f);
    w.reg.get<SandboxAnimal>(wolf).energy = 0.2f;
    decide(w, wolf, { -1.f, 0.f, 0.f, -0.5f }, AnimalMode::Feed);
    const float meat = w.reg.get<SandboxAnimal>(prey).meat;
    seconds(w, 3.);
    const auto& fed = w.reg.get<SandboxAnimal>(wolf);
    CHECK(fed.energy > 0.2f + 2.5f / 30.f - 3.5f / 360.f);
    CHECK(w.reg.get<SandboxAnimal>(prey).meat == doctest::Approx(meat - (fed.energy - 0.2f)).epsilon(0.15));
    // A deer does not eat meat: with the wolf gone, one that holds its muzzle there in Feed mode takes nothing
    put(w, wolf, glm::vec2(80.f, -80.f), 0.f);
    entt::entity other = animalsOf(w, "deer_red")[1];
    put(w, other, glm::vec2(-50.f, -48.f), 0.f);
    decide(w, other, { -1.f, 0.f, 0.f, 0.5f }, AnimalMode::Feed);
    seconds(w, 0.2);
    const float still = w.reg.get<SandboxAnimal>(prey).meat;
    seconds(w, 0.5);
    CHECK(w.reg.get<SandboxAnimal>(prey).meat == still);
}

TEST_CASE("SANDBOX.7 puppets: two adults in Mate mode side by side, a gestation, a young born small that grows, +1 for both") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_puppet", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    auto& species = sb.species[sb.speciesIndex("deer_red")];
    // Nobody starves while this lasts
    for(auto& all : sb.species) all.life.starve = 1e6f;
    entt::entity female = entt::null, male = entt::null;
    for(auto e : animalsOf(w, "deer_red")) {
        const auto& a = w.reg.get<SandboxAnimal>(e);
        if(a.male && male == entt::null) male = e;
        if(!a.male && female == entt::null) female = e;
    }
    REQUIRE((female != entt::null));
    REQUIRE((male != entt::null));
    put(w, female, glm::vec2(60.f, 60.f), 0.f);
    put(w, male, glm::vec2(61.5f, 60.f), 0.f);
    w.reg.get<SandboxAnimal>(female).energy = 1.f;
    w.reg.get<SandboxAnimal>(male).energy = 1.f;

    // One of them is not willing: nothing
    decide(w, female, { -1.f, 0.f, 0.f, 0.f }, AnimalMode::Mate);
    decide(w, male, { -1.f, 0.f, 0.f, 0.f }, AnimalMode::Rest);
    seconds(w, 4.);
    CHECK(!w.reg.get<SandboxAnimal>(female).pregnant);
    decide(w, male, { -1.f, 0.f, 0.f, 0.f }, AnimalMode::Mate);
    seconds(w, 1.2);
    CHECK(!w.reg.get<SandboxAnimal>(female).pregnant);
    CHECK(w.reg.get<SandboxAnimal>(female).mating > 0.8f);
    {
        // The signal a reward file reads: how far the pair is to a conception
        std::vector<float> signals(SandboxSignal::count);
        sandboxSignals(w.reg, female, 0.2f, signals.data());
        CHECK(signals[SandboxSignal::mating] > 0.4f);
        CHECK(signals[SandboxSignal::mating] < 0.7f);
    }
    seconds(w, 1.5);
    REQUIRE(w.reg.get<SandboxAnimal>(female).pregnant);
    CHECK((w.reg.get<SandboxAnimal>(female).father == male));
    decide(w, female, { -1.f, 0.f, 0.f, 0.f }, AnimalMode::Rest);
    decide(w, male, { -1.f, 0.f, 0.f, 0.f }, AnimalMode::Rest);

    const int alive = species.alive;
    seconds(w, 58.);
    CHECK(species.alive == alive);
    CHECK(species.born == 0);
    seconds(w, 3.);
    CHECK(species.alive == alive + 1);
    CHECK(species.born == 1);
    {
        const auto& mother = w.reg.get<SandboxAnimal>(female);
        CHECK(!mother.pregnant);
        CHECK(mother.energy == doctest::Approx(1.f - species.life.birth_cost).epsilon(0.01));
    }
    // The young: the newest deer, small, not an adult, close to its mother
    entt::entity young = entt::null;
    for(auto e : animalsOf(w, "deer_red")) if(w.reg.get<SandboxAnimal>(e).age < 5.f) young = e;
    REQUIRE((young != entt::null));
    {
        const auto& y = w.reg.get<SandboxAnimal>(young);
        const auto& bot = w.reg.get<AnimalBot>(young);
        CHECK(y.stage == 0);
        CHECK(bot.body->mass == doctest::Approx(w.reg.get<AnimalBot>(female).body->mass * 0.216f));
        CHECK(glm::distance(bot.position[0], w.reg.get<AnimalBot>(female).position[0]) < 4.f);
        CHECK(w.reg.valid(y.mind));
    }
    // Both parents were paid at their next decision
    CHECK(w.reg.get<SandboxAnimal>(female).lifetime_reward > 0.9f);
    CHECK(w.reg.get<SandboxAnimal>(male).lifetime_reward > 0.9f);
    // It grows: half way at a minute, an adult's body at two
    seconds(w, 61.);
    CHECK(w.reg.get<SandboxAnimal>(young).stage == 1);
    seconds(w, 61.);
    CHECK(w.reg.get<SandboxAnimal>(young).stage == 2);
    CHECK(w.reg.get<AnimalBot>(young).body->mass == doctest::Approx(w.reg.get<AnimalBot>(female).body->mass));
}

TEST_CASE("SANDBOX.8 the mind's observation: itself, the grass around, and the others it sees or hears") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_puppet", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    auto deer_list = animalsOf(w, "deer_red");
    auto wolves = animalsOf(w, "wolf_2");
    // Everyone far away, then a wolf, a deer ahead of it, one behind it close, one behind it far
    int k = 0;
    for(auto e : deer_list) put(w, e, glm::vec2(-90.f + 3.f * (float) k++, 90.f), 0.f);
    for(auto e : wolves) put(w, e, glm::vec2(-90.f + 3.f * (float) k++, 80.f), 0.f);
    entt::entity wolf = wolves[0], ahead = deer_list[0], behind = deer_list[1], far_behind = deer_list[2];
    put(w, wolf, glm::vec2(0.5f, 0.5f), 0.f);
    put(w, ahead, glm::vec2(-5.5f, -19.5f), 0.f);
    put(w, behind, glm::vec2(0.5f, 8.5f), 0.f);
    put(w, far_behind, glm::vec2(0.5f, 30.5f), 0.f);
    w.reg.get<SandboxAnimal>(wolf).energy = 0.4f;
    // The one behind walks: it can be heard
    decide(w, behind, { 0.5f, 0.f, 0.f, 0.f }, AnimalMode::Move);
    seconds(w, 0.5);

    auto& io = w.reg.get<AgentIO>(w.reg.get<SandboxAnimal>(wolf).mind);
    observeSandboxMind(w.reg, wolf, io);
    namespace O = MindObs;
    CHECK(io.observation[O::energy] == doctest::Approx(0.4f).epsilon(0.02));
    CHECK(io.observation[O::health] == 1.f);
    CHECK(io.observation[O::adult] == 1.f);
    CHECK(io.observation[O::size] == 1.f);
    CHECK(io.observation[O::mode + (int) AnimalMode::Move] == 1.f);
    CHECK(std::abs(io.observation[O::centre]) < 0.01f);
    // The grass ring reads the field: ahead is -z
    const glm::vec3 at = w.reg.get<AnimalBot>(wolf).position[0];
    CHECK(io.observation[O::grass] == doctest::Approx(sb.grass.height(at.x, at.z - 3.f)));
    CHECK(io.observation[O::grass + 2] == doctest::Approx(sb.grass.height(at.x - 3.f, at.z)));

    const auto& buffer = io.sensors[0];
    // Seen: the deer ahead (and the animals parked far ahead-left are out of sight at 80 m or in view: count the near ones)
    const float* seen_row = nullptr;
    const float* heard_row = nullptr;
    for(int r = 0; r < buffer.count; r++) {
        const float* row = buffer.data.data() + (size_t) r * buffer.size;
        const float f = row[O::row_position] * 80.f, l = row[O::row_position + 1] * 80.f;
        if(row[O::row_seen] == 1.f && std::abs(f - 20.f) < 1.5f && std::abs(l - 6.f) < 1.5f) seen_row = row;
        if(row[O::row_seen] == 0.5f) heard_row = row;
    }
    REQUIRE(seen_row);
    CHECK(seen_row[O::row_prey] == 1.f);
    CHECK(seen_row[O::row_own] == 0.f);
    CHECK(seen_row[O::row_predator] == 0.f);
    CHECK(seen_row[O::row_size] > 0.5f);
    // Heard: the one walking close behind, a direction and a rough distance, nothing about what it is
    REQUIRE(heard_row);
    CHECK(heard_row[O::row_position] < 0.f);
    CHECK(heard_row[O::row_prey] == 0.f);
    CHECK(heard_row[O::row_size] == 0.f);
    // The one far behind is neither seen (a wolf sees 180 degrees) nor heard
    for(int r = 0; r < buffer.count; r++) {
        const float* row = buffer.data.data() + (size_t) r * buffer.size;
        CHECK(row[O::row_position] * 80.f > -25.f);
    }
    // Turning the head to look behind: now it is seen
    decide(w, wolf, { -1.f, 0.f, 1.f, 0.f }, AnimalMode::Move);
    seconds(w, 0.2);
    // (a head turned one radian to the left of a 180 degree view covers 147 degrees to the left: not straight behind)
    // The deer sees nearly all around: the wolf behind it is in its buffer
    // ... but for straight behind: the wolf is 135 degrees round, 10 m away
    put(w, ahead, glm::vec2(7.57f, -6.57f), 0.f);
    seconds(w, 0.1);
    auto& deer_io = w.reg.get<AgentIO>(w.reg.get<SandboxAnimal>(ahead).mind);
    observeSandboxMind(w.reg, ahead, deer_io);
    bool predator_seen = false;
    for(int r = 0; r < deer_io.sensors[0].count; r++) {
        const float* row = deer_io.sensors[0].data.data() + (size_t) r * deer_io.sensors[0].size;
        if(row[O::row_predator] == 1.f && row[O::row_position] < 0.f) predator_seen = true;
    }
    CHECK(predator_seen);

    // The signals of the reward: distances whatever the direction
    std::vector<float> signals(SandboxSignal::count);
    sandboxSignals(w.reg, ahead, 0.2f, signals.data());
    CHECK(signals[SandboxSignal::predator_distance] == doctest::Approx(10.f).epsilon(0.05));
    CHECK(signals[SandboxSignal::prey_distance] == 1000.f);
    // Closing: how much nearer the nearest one came since the decision before, at most 3 m
    // (the deer was put there from far away since its mind last decided: as much nearer as the signal goes)
    CHECK(signals[SandboxSignal::predator_closing] == 3.f);
    put(w, ahead, glm::vec2(7.57f - 1.41f, -6.57f + 1.41f), 0.f);
    sandboxSignals(w.reg, ahead, 0.2f, signals.data());
    CHECK(signals[SandboxSignal::predator_closing] == doctest::Approx(2.f).epsilon(0.05));
    put(w, ahead, glm::vec2(60.f, -60.f), 0.f);
    sandboxSignals(w.reg, ahead, 0.2f, signals.data());
    CHECK(signals[SandboxSignal::predator_closing] == -3.f);
    put(w, ahead, glm::vec2(7.57f, -6.57f), 0.f);
    sandboxSignals(w.reg, wolf, 0.2f, signals.data());
    CHECK(signals[SandboxSignal::prey_distance] < 10.5f);
    CHECK(signals[SandboxSignal::energy] == doctest::Approx(0.4f).epsilon(0.03));
}

TEST_CASE("SANDBOX.9 puppets with the scripted minds: deer graze and breed, wolves kill and feed, the dead leave") {
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_heuristic", { { "num_worlds", "1" } }, SceneLoadOptions {});
    auto& w = runner.world(0);
    auto& sb = w.reg.ctx().get<Sandbox>();
    const auto& deer = sb.species[sb.speciesIndex("deer_red")];
    const auto& wolves = sb.species[sb.speciesIndex("wolf_2")];
    seconds(w, 480.);
    double deer_energy = 0., wolf_energy = 0.;
    for(auto e : animalsOf(w, "deer_red")) deer_energy += w.reg.get<SandboxAnimal>(e).energy;
    for(auto e : animalsOf(w, "wolf_2")) wolf_energy += w.reg.get<SandboxAnimal>(e).energy;
    MESSAGE("8 minutes: deer " << deer.alive << " alive (born " << deer.born << ", starved " << deer.starved << ", killed " << deer.killed << ", old " << deer.aged
        << ", brought in " << deer.spawned << "), mean store " << deer_energy / std::max(deer.alive, 1) << "; wolves " << wolves.alive << " alive (born " << wolves.born
        << ", starved " << wolves.starved << ", killed " << wolves.killed << ", kills " << wolves.kills << ", brought in " << wolves.spawned << "), mean store "
        << wolf_energy / std::max(wolves.alive, 1) << "; grass standing " << sb.grass.standing() << ", carcasses " << animalsOf(w, "deer_red", false).size());
    CHECK(deer.alive >= deer.life.min);
    CHECK(deer.alive <= deer.life.max);
    CHECK(wolves.alive >= wolves.life.min);
    // Deer that graze do not all starve in eight minutes (a store lasts three)
    CHECK(deer.starved < 12);
    CHECK(sb.grass.standing() < 0.9999f);
    CHECK(deer_energy / std::max(deer.alive, 1) > 0.3);
    // Wolves that chase and strike kill, and feed on what they killed
    CHECK(wolves.kills >= 1);
    CHECK(deer.killed >= 1);
    CHECK(deer.born >= 1);
    // The world holds its animals and nothing else: every group is an animal
    CHECK(sb.by_group.size() == w.reg.view<SandboxAnimal>().size());
}

}

TEST_SUITE("scenario") {

TEST_CASE("SANDBOX.10 a weapon: the hunter's spear is a second shape of the forearm, ahead of it, and strikes as a head does") {
    if(!std::filesystem::exists(libraryResourceFile("animals/bodies/homo_heidelbergensis_walking.body.json"))) return;
    auto body = loadAnimalBody(animalBodyPath("homo_heidelbergensis_walking"));
    SpeciesLife life;
    int segment = -1;
    // No weapon: the same body
    CHECK(armAnimalBody(body, life, segment) == body);
    CHECK(segment == -1);
    life.weapon_segment = "mixamorig:RightForeArm";
    life.weapon_length = 2.f;
    auto armed = armAnimalBody(body, life, segment);
    REQUIRE(segment >= 0);
    CHECK(armed != body);
    const SegmentDef& arm = armed->def.segments[(size_t) segment];
    REQUIRE(arm.shapes.size() == 2);
    CHECK(body->def.segments[(size_t) segment].shapes.size() == 1);
    // Masses are the animal's: a shape only
    CHECK(armed->mass == body->mass);
    CHECK(arm.percentage == body->def.segments[(size_t) segment].percentage);
    // Where the man stands the spear is level and points ahead (-z), its tip 1.5 m in front of the hand
    const ShapeDef& spear = arm.shapes[1];
    const glm::vec3 axis = arm.spawn_rotation * (spear.rotation * glm::vec3(0.f, 1.f, 0.f));
    CHECK(axis.z == doctest::Approx(-1.f).epsilon(0.001));
    const glm::vec3 centre = arm.spawn_position + arm.spawn_rotation * spear.center;
    const glm::vec3 hand = arm.spawn_position + arm.spawn_rotation * arm.shapes[0].center;
    CHECK(centre.z == doctest::Approx(hand.z - 0.5f));
    CHECK(centre.y == doctest::Approx(hand.y));
    life.weapon_segment = "no such body";
    armAnimalBody(body, life, segment);
    CHECK(segment == -1);

    // The sandbox with hunters: three species, the men armed
    if(!haveBodies()) return;
    ScenarioRunner runner("sandbox_puppet", { { "num_worlds", "1" }, { "life", "agents/params/life_hunters.params.json" } }, SceneLoadOptions {});
    auto& sb = runner.world(0).reg.ctx().get<Sandbox>();
    REQUIRE(sb.species.size() == 3);
    const int men = sb.speciesIndex("homo_heidelbergensis_walking");
    REQUIRE(men >= 0);
    CHECK(sb.species[men].weapon_segment >= 0);
    CHECK(sb.species[men].life.predator);
    CHECK(sb.species[men].alive == 3);
    CHECK(sb.species[men].rewards.terms.size() > 0);
}

}
