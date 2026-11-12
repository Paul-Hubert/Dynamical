#ifndef DY_SANDBOX_H
#define DY_SANDBOX_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "expr.h"
#include "life.h"
#include "logic/animals/animal_agent.h"
#include "logic/systems/system.h"

namespace dy {

struct AgentIO;

// The sandbox (docs/action-agents.md): animals of several species living in one world, each a ragdoll with two
// agents. Its walker (behaviour Animal_<species>, a decision every window) drives the joints toward a walk command
// and an order; its mind (behaviour Action_<species>, a decision every `mind_every` windows) gives that command and
// order from what the animal senses. The rules of life (life.h) decide who eats, who is wounded, who is born and who
// dies. Both agents are AgentIO components: the animal's entity holds the walker's, a second entity the mind's.

// What the reward file of a species can read, over the action decision that just ended
namespace SandboxSignal {
    enum : int {
        energy, health, age, adult, male, pregnant,     // now: 0..1, 0..1, of its old age, flags
        energy_gain, energy_spent,                      // over the decision, in full stores
        damage_taken, damage_dealt,                     // in full healths
        kills, births, falls,                           // counts
        speed,                                          // of the trunk, in walk speeds
        mode_move, mode_feed, mode_attack, mode_mate, mode_rest,    // the mode it was in
        feeding,                                        // part of the decision it fed
        grass_here,                                     // m, under the muzzle
        muzzle_height,                                  // heights of the trunk standing
        predator_distance, prey_distance, mate_distance, carcass_distance, herd_distance,   // m to the nearest, 1000 without
        herd_count,                                     // of its species within its sight
        since_fed,                                      // of the time a full store lasts
        dt,                                             // seconds of the decision
        grass_near,                                     // m: the tallest grass 3 m around
        conceptions,                                    // count: it mated and a young is on its way (both parents)
        lived,                                          // seconds since it was born or brought in
        // m the nearest one came closer over the decision (negative: further), at most 3 either way; 0 when there
        // was none at its start or at its end
        predator_closing, prey_closing, mate_closing, carcass_closing,
        mating,                                         // how far the pair is to a conception: 0 to 1 of the time it takes together
        grass_distance,                                 // m to the nearest grass worth grazing within 40 m (0 in it), 1000 without
        grass_closing,                                  // as the other closings, on that grass
        count
    };
}
const std::vector<std::string>& sandboxSignalNames();

// resources/agents/rewards/<species>.rewards.json: the intermediate rewards, written between rounds
struct RewardProgram {
    struct Term {
        std::string name;
        Expression expression;
    };
    int round = 0;
    float scale = 1.f;
    std::string notes, file_hash;
    std::vector<Term> terms;

    // Throws std::runtime_error (an expression that does not parse, a term without a name)
    static RewardProgram parse(const nlohmann::json& j);
    // No file: no terms, the objective alone
    static RewardProgram load(const std::string& path);
};

struct SandboxSpecies {
    SpeciesLife life;
    // Bodies by growth stage, the adult's last; ages (seconds at a life scale of 1) at which each begins
    std::vector<std::shared_ptr<const AnimalBody>> bodies;
    std::vector<float> stage_age;
    std::vector<float> stage_scale;
    std::shared_ptr<const AnimalParams> walker_params;
    GaitRanges ranges;
    RewardProgram rewards;
    std::string walker_behavior, action_behavior;
    int weapon_segment = -1;            // the body that holds the species' weapon: strikes as the head does

    // Counters since the world began
    uint64_t born = 0, spawned = 0, starved = 0, killed = 0, aged = 0, kills = 0, falls = 0;
    uint64_t lives = 0;                 // that ended
    double lifetime = 0.;               // ... and the seconds they lasted, summed
    double work = 0.;                   // positive mechanical power over the reference (0.1 m g v), summed over animal ticks
    double moved = 0.;                  // m the trunks covered
    uint64_t ticks = 0;
    int alive = 0;
};

// On the entity of an animal, next to its AnimalBot (the ragdoll and the walker's state) and, while it lives, the
// walker's AgentIO
struct SandboxAnimal {
    int species = 0;
    uint32_t group = 0;                 // of its ragdoll: unique in the world
    entt::entity mind = entt::null;     // the entity of its action agent
    bool male = false;
    float age = 0.f;                    // seconds
    float energy = 1.f, health = 1.f;
    int stage = 0;                      // of growth
    bool pregnant = false;
    float gestation = 0.f;
    entt::entity father = entt::null;
    float mating = 0.f;                 // seconds together with a partner
    float stun = 0.f;                   // seconds left standing where it was put back up
    float since_fed = 0.f;
    float lived = 0.f;                  // seconds in this world
    int offspring = 0;
    bool feeding = false;               // this tick

    enum Cause : int { Alive, Starved, Killed, Old };
    bool dead = false;
    int cause = Alive;
    bool reported = false;              // its agents' terminal steps went out
    float meat = 0.f;                   // a carcass: what is left of it, in full stores of an adult of its species
    float rot = 0.f;
    entt::entity attacker = entt::null; // who wounded it last
    float attacked = 1e9f;              // seconds since

    // The walker's episode
    int walker_ticks = 0;
    bool walker_fresh = true;
    bool fell = false;                  // this window: the walker's episode ends, the animal is stood up again
    bool restand = false;               // ... at the next window (a fall, a new size)
    bool grow = false;                  // a new size is due
    float share = 0.f;                  // of the mind's last reward, for the walker's next step

    double mechanical = 0.;             // energy of the bodies, J
    bool mechanical_valid = false;
    float power = 0.f;                  // positive mechanical power, W, smoothed

    // Since the mind's last decision
    struct Window {
        float energy_gain = 0.f, energy_spent = 0.f, damage_taken = 0.f, damage_dealt = 0.f;
        int kills = 0, births = 0, falls = 0, feeding = 0, ticks = 0, conceptions = 0;
        int modes[animal_modes] = {};
        glm::vec3 from = glm::vec3(0.f);
    } window;
    float nearest[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };    // predator, prey, mate, carcass, grass at the mind's last decision; < 0: none
    float objective = 0.f;              // births and its death, to pay with the mind's next decision
    float lifetime_reward = 0.f;
};

// On the entity of a drawn body (Sandbox::draw), next to its Transform and WorldShape
struct SandboxShape {
    entt::entity animal = entt::null;
};

// On the entity of a mind, next to its AgentIO
struct SandboxMind {
    entt::entity animal = entt::null;
    int phase = 0;                      // decides on windows where (window + phase) % mind_every == 0
    bool fresh = true;
};

// The mind's layout
namespace MindObs {
    // The vector: itself, then the grass around
    constexpr int energy = 0, health = 1, age = 2, adult = 3, male = 4, pregnant = 5, gestation = 6, size = 7, since_fed = 8;
    constexpr int forward_speed = 9, side_speed = 10;      // of the trunk in its heading frame, over its top speed
    constexpr int head_yaw = 11, head_pitch = 12;          // where the head points now, as offsets over their limits
    constexpr int muzzle_height = 13;                      // over the standing height
    constexpr int grass_here = 14;                         // m under the muzzle
    constexpr int stunned = 15;
    constexpr int centre = 16;                             // 2: the middle of the arena in its heading frame, over the arena's half side
    constexpr int mode = 18;                               // 5: the mode it is in
    constexpr int grass = 23;                              // 16: grass heights (m) at 8 directions (ahead first, turning left), 3 m then 10 m away
    constexpr int size_total = 39;

    // A row of the buffer "Interests": the nearest first
    constexpr int row_position = 0;         // forward, left, over its sight
    constexpr int row_velocity = 2;         // relative, forward and left, over 5 m/s
    constexpr int row_own = 4, row_prey = 5, row_predator = 6, row_carcass = 7;
    constexpr int row_male = 8;
    constexpr int row_size = 9;             // its mass over three times the observer's, at most 1
    constexpr int row_mating = 10;          // a possible mate (adult, its species, the other sex; the observer an adult too): 0.5, and 1 in Mate mode
    constexpr int row_seen = 11;            // 1 seen, 0.5 only heard (then: no kind, no sex, no size, a rough distance)
    constexpr int row = 12;
}
constexpr int mind_actions = 4;             // speed, turn, head yaw, head pitch; one discrete branch: the mode

// World context variable
struct Sandbox {
    LifeParams params;
    LifeParams base;                    // as the file has them: environment parameters override a copy
    std::vector<SandboxSpecies> species;
    GrassField grass;
    std::unordered_map<uint32_t, entt::entity> by_group;
    uint32_t next_group = 1;
    uint64_t windows = 0;
    double seconds = 0.;
    // A check of the rules and of the minds without walkers: the animals are kinematic bodies in their standing pose,
    // slid along their command; no walker agents, the head is where its order asks, a muzzle held on a trunk in
    // Attack mode wounds. Not the sandbox: nothing a body does is learned there
    bool puppet = false;
    bool heuristic = false;             // the scripted minds answer every mind that asks and that nothing answered
    bool draw = false;                  // an entity with Transform and WorldShape per body, for WorldView
    std::string rewards_dir;
    uint64_t contacts = 0, blows = 0;   // contacts between two animals so far, and those that wounded

    int speciesIndex(const std::string& id) const;
};

// A new animal of a species at a place: its ragdoll standing there, its two agents. age < 0: an adult of a random
// age, else that age (0: a newborn)
entt::entity spawnSandboxAnimal(entt::registry& reg, int species, glm::vec2 at, float yaw, float age, int male = -1);

// The mind's observation of an animal (vector and interest rows) and the signals of its reward
void observeSandboxMind(entt::registry& reg, entt::entity animal, AgentIO& io);
// (the distances of the decision before are kept on the animal for the closing signals: once per decision)
void sandboxSignals(entt::registry& reg, entt::entity animal, float seconds, float* out);

// A scripted mind (tests, the rules without a policy): from the observation alone. Deer flee wolves, graze where
// there is grass, go to it otherwise, mate when fed; wolves go to carcasses and feed, chase and strike deer
void sandboxHeuristic(const SandboxSpecies& species, AgentIO& io);

// Every tick, before PhysicsStep. On the first tick of a window: the dead leave their agents, carcasses that are
// eaten or rotten leave the world, the fallen are stood up, the young are born and grow, species under their
// minimum get adults at the edge; then the minds' actions become commands and orders and the walkers' actions drives
//
// params: "life" (resources path of the life params), "rewards" (folder of <species>.rewards.json), "puppet",
//         "heuristic" (the scripted minds, for the minds no policy answers), "draw" (shapes for WorldView), and to
//         override the life params: "arena", "species" ({ "<id>": { "start": n, ... } }), "only" ([ids])
class SandboxSys : public System {
public:
    SandboxSys(entt::registry& reg, const nlohmann::json& params);
    ~SandboxSys() override;
    const char* name() override { return "Sandbox"; }
    void preinit() override;
    void init() override;
    void tick(double h) override;
    void finish() override;

private:
    nlohmann::json params;
    struct Contacts;
    std::unique_ptr<Contacts> contacts;
    friend class SandboxLifeSys;
};

// Every tick, after PhysicsStep: the bodies' states, falls, energy, feeding, wounds from the contacts, mating,
// ages, deaths, the grass
class SandboxLifeSys : public System {
public:
    SandboxLifeSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SandboxLife"; }
    void tick(double h) override;
};

// Observe phase: every walker's reward, end and observation; the same for the minds that are due
class SandboxObserveSys : public System {
public:
    SandboxObserveSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SandboxObserve"; }
    void tick(double h) override;
};

// Act phase: sandboxHeuristic for every mind that asked (scenes without a policy for the minds)
class SandboxHeuristicSys : public System {
public:
    SandboxHeuristicSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "SandboxHeuristic"; }
    void tick(double h) override;
};

void registerSandbox();

}

#endif
