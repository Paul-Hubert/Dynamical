#ifndef DY_SANDBOX_LIFE_H
#define DY_SANDBOX_LIFE_H

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "logic/animals/animal_body.h"

namespace dy {

// The rules of life of the sandbox (docs/action-agents.md): what an animal's energy and health do, what the grass
// does, what a blow costs. Parameters and functions with no world in them; the systems are in sandbox.h.
// resources/agents/params/life.params.json holds the parameters, with a block per species.

// A species of a sandbox. Durations are seconds at a life scale of 1 (LifeParams::life_scale stretches them all)
struct SpeciesLife {
    std::string id;                 // of the library: its body is animals/bodies/<id>.body.json
    bool predator = false;          // feeds on carcasses, not on grass
    int start = 12;                 // animals of it at the start of a world
    int min = 4;                    // under it, adults come in at the edge of the arena
    int max = 24;                   // no birth over it

    float starve = 180.f;           // a full store lasts this long, standing
    float feed = 30.f;              // feeding fills an empty store in this long
    float adult = 120.f;            // age at which it is full size and can mate
    float gestation = 60.f;
    float old = 1200.f;             // age at which the life ends, without penalty
    float heal = 300.f;             // no health to full, when nothing wounds
    float mate_time = 2.f;          // together, both in Mate mode
    float rot = 300.f;              // its carcass lasts this long if nothing eats it

    float work = 0.1f;              // what positive mechanical work costs: at 0.1 m g v watts (v its walk speed) this much of the basal rate
                                    // again. The walkers spend 4.5 to 6.7 times that reference at the speeds the minds ask (the jolts of
                                    // their drives more than the work of a gait): at 0.1 walking takes a store half as fast again
    float rest_drain = 0.6f;        // of the basal rate, in Rest mode
    float rest_heal = 3.f;          // times faster, in Rest mode
    float top_speed = 2.2f;         // walk speeds asked for a speed action of 1
    float turn_radius = 1.5f;       // lengths: the tightest turn asked
    float feed_speed = 0.6f;        // walk speeds: feeding needs the trunk slower than this
    float graze = 0.3f;             // m of grass height a second eaten at the full feeding rate
    float carcass_reach = 0.35f;    // heights: the muzzle this close to a body of a carcass feeds on it
    float mate_energy = 0.5f;       // both need this much to mate
    float mate_reach = 2.f;          // lengths between the trunks
    float birth_cost = 0.3f;        // of the mother's store
    float newborn_energy = 0.7f;
    float young_scale = 0.6f;       // size at birth; 1: born full size
    float meat = 3.f;               // a carcass of an adult holds this many full stores of whoever eats it
    float bite = 1.f;               // how hard its head wounds in Attack mode
    float fall_damage = 0.08f;      // health lost when it falls
    float stun = 1.5f;              // seconds (not scaled) it stays where it was stood up after a fall
    float fov = 300.f;              // degrees of its field of view, around where the head points
    float sight = 80.f;             // m
    float hearing = 12.f;           // m: moving animals this close are heard whatever the direction

    // A weapon in the hand (a hunter's spear): a capsule fixed to a body of the ragdoll, pointing ahead where the
    // animal stands. It strikes as the head does, in Attack mode. No weapon without a segment
    std::string weapon_segment;     // name of the body that holds it
    float weapon_length = 2.f;      // m
    float weapon_radius = 0.02f;
    float weapon_reach = 0.75f;     // of its length ahead of the hand
};

struct GrassParams {
    float cell = 1.f;               // m
    float cover = 0.55f;            // part of the arena that has grass
    float patch = 60.f;             // m: wavelength of the patches
    float shortest = 0.55f, tallest = 1.f;  // m, as the engine's grass (GrassHeightField)
    float stubble = 0.04f;          // m: what grazing leaves
    float regrow = 600.f;           // seconds from stubble to full height
    int seed = 1337;
};

struct LifeParams {
    std::string file_hash;
    float arena = 100.f;            // m: half side of the square the animals live in
    float life_scale = 1.f;         // multiplies every duration (environment parameter lifeScale)
    float walker_share = 0.f;       // of the action policy's reward that its walker gets too (walkerShare)
    float reward_scale = 1.f;       // of the reward file's terms (rewardScale): the objective is not scaled
    int mind_every = 6;             // walker decisions per action decision
    int interests = 16;             // rows of the interest buffer
    float wound_threshold = 0.01f;  // m/s: a blow's impulse over the victim's mass under this does not wound (a push, a brush)
    float kill_impulse = 0.25f;     // m/s: ... and this much over it takes a full health at a bite of 1. The impulse is the one
                                    // of the two bodies that meet (a head of 10 kg on a leg, on a trunk): 15 to 50 N s at 5 m/s
    float weak = 0.5f;              // the drives of an animal with no health left keep this part of their strength
    float walker_episode = 33.3f;   // seconds: a walker's episode is cut (a truncation, the animal goes on)
    GrassParams grass;
    std::vector<SpeciesLife> species;

    // Throws std::runtime_error on a malformed file
    static LifeParams parse(const nlohmann::json& j);
    static LifeParams load(const std::string& path);

    int speciesIndex(const std::string& id) const;
};

// Energy lost per second, as a part of a full store. power: positive mechanical power of the body (W), mass and
// walk_speed: of the animal as it is
float energyDrain(const SpeciesLife& species, float life_scale, float power, float mass, float walk_speed, bool resting);

// Health a blow takes: the impulse (N s) of the head of an animal of `attacker` in Attack mode on a body of an animal
// of `victim_mass`
float woundOf(const LifeParams& params, const SpeciesLife& attacker, float impulse, float victim_mass);

// The grass of the arena: a grid of heights, eaten where a muzzle feeds, growing back
class GrassField {
public:
    GrassField() = default;
    GrassField(const GrassParams& params, float arena, uint64_t seed);

    // Height now and full height at a point, m (0 outside the arena and on bare ground)
    float height(float x, float z) const;
    float full(float x, float z) const;
    // Eats up to `amount` m of height from the cell of the point; what was eaten
    float eat(float x, float z, float amount);
    void grow(float seconds);

    int size() const { return side; }
    // Mean height over the mean full height: 1 untouched
    float standing() const;
    float coverPart() const;        // of the cells that have grass

private:
    int index(float x, float z) const;

    GrassParams p;
    float arena = 0.f;
    int side = 0;
    std::vector<float> tall, now;
};

// A body of the same animal at another size: lengths x scale, masses x scale^3, inertias x scale^5, the drives with
// the weight and the lever (x scale^4), speeds as Froude has them (x sqrt(scale))
std::shared_ptr<const AnimalBody> scaleAnimalBody(const AnimalBody& body, float scale);

// The body with its species' weapon as a second shape of the body that holds it (a shape only: masses stay the
// animal's). `segment`: which body that is, -1 when the species has no weapon or the body no such segment
std::shared_ptr<const AnimalBody> armAnimalBody(std::shared_ptr<const AnimalBody> body, const SpeciesLife& species, int& segment);

}

#endif
