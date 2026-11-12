#include "life.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "logic/grass/grass_height.h"
#include "util/paths.h"

using namespace dy;

namespace {

void readSpecies(const nlohmann::json& j, SpeciesLife& s) {
    auto f = [&](const char* key, float& v) { v = j.value(key, v); };
    auto i = [&](const char* key, int& v) { v = j.value(key, v); };
    s.predator = j.value("predator", s.predator);
    i("start", s.start); i("min", s.min); i("max", s.max);
    f("starve", s.starve); f("feed", s.feed); f("adult", s.adult); f("gestation", s.gestation); f("old", s.old); f("heal", s.heal);
    f("mate_time", s.mate_time); f("rot", s.rot);
    f("work", s.work); f("rest_drain", s.rest_drain); f("rest_heal", s.rest_heal); f("top_speed", s.top_speed); f("turn_radius", s.turn_radius);
    f("feed_speed", s.feed_speed); f("graze", s.graze); f("carcass_reach", s.carcass_reach); f("mate_energy", s.mate_energy); f("mate_reach", s.mate_reach);
    f("birth_cost", s.birth_cost); f("newborn_energy", s.newborn_energy); f("young_scale", s.young_scale); f("meat", s.meat); f("bite", s.bite);
    f("fall_damage", s.fall_damage); f("stun", s.stun); f("fov", s.fov); f("sight", s.sight); f("hearing", s.hearing);
    s.weapon_segment = j.value("weapon_segment", s.weapon_segment);
    f("weapon_length", s.weapon_length); f("weapon_radius", s.weapon_radius); f("weapon_reach", s.weapon_reach);
    static const char* const known[] = { "predator", "start", "min", "max", "starve", "feed", "adult", "gestation", "old", "heal", "mate_time", "rot", "work",
        "rest_drain", "rest_heal", "top_speed", "turn_radius", "feed_speed", "graze", "carcass_reach", "mate_energy", "mate_reach", "birth_cost", "newborn_energy",
        "young_scale", "meat", "bite", "fall_damage", "stun", "fov", "sight", "hearing", "description", "weapon_segment", "weapon_length", "weapon_radius",
        "weapon_reach" };
    for(auto& [key, value] : j.items()) {
        if(std::find_if(std::begin(known), std::end(known), [&](const char* k) { return key == k; }) == std::end(known)) {
            throw std::runtime_error("life params: unknown key '" + key + "' of a species");
        }
    }
    if(s.starve <= 0.f || s.feed <= 0.f || s.adult < 0.f || s.old <= 0.f || s.heal <= 0.f) throw std::runtime_error("life params: durations of " + s.id + " must be positive");
    if(s.min > s.max || s.start > s.max || s.start < 0) throw std::runtime_error("life params: start, min and max of " + s.id + " do not agree");
    if(s.young_scale <= 0.2f || s.young_scale > 1.f) throw std::runtime_error("life params: young_scale of " + s.id + " must be in 0.2..1");
}

}

LifeParams LifeParams::parse(const nlohmann::json& j) {
    if(j.value("format_version", 0) != 1 || j.value("kind", "") != "life_params") throw std::runtime_error("life params: not a life_params file of format 1");
    LifeParams p;
    p.arena = j.value("arena", p.arena);
    p.life_scale = j.value("life_scale", p.life_scale);
    p.walker_share = j.value("walker_share", p.walker_share);
    p.reward_scale = j.value("reward_scale", p.reward_scale);
    p.mind_every = j.value("mind_every", p.mind_every);
    p.interests = j.value("interests", p.interests);
    p.wound_threshold = j.value("wound_threshold", p.wound_threshold);
    p.kill_impulse = j.value("kill_impulse", p.kill_impulse);
    p.weak = j.value("weak", p.weak);
    p.walker_episode = j.value("walker_episode", p.walker_episode);
    if(auto it = j.find("grass"); it != j.end()) {
        GrassParams& g = p.grass;
        g.cell = it->value("cell", g.cell);
        g.cover = it->value("cover", g.cover);
        g.patch = it->value("patch", g.patch);
        g.shortest = it->value("shortest", g.shortest);
        g.tallest = it->value("tallest", g.tallest);
        g.stubble = it->value("stubble", g.stubble);
        g.regrow = it->value("regrow", g.regrow);
        g.seed = it->value("seed", g.seed);
    }
    if(p.arena <= 1.f || p.mind_every < 1 || p.interests < 1 || p.grass.cell <= 0.f) throw std::runtime_error("life params: arena, mind_every, interests and the grass cell must be positive");
    // Shared by every species, then its own block
    SpeciesLife defaults;
    if(auto it = j.find("defaults"); it != j.end()) readSpecies(*it, defaults);
    if(!j.contains("species") || !j["species"].is_object()) throw std::runtime_error("life params: no species");
    for(auto& [id, block] : j["species"].items()) {
        SpeciesLife s = defaults;
        s.id = id;
        readSpecies(block, s);
        p.species.push_back(s);
    }
    if(p.species.empty()) throw std::runtime_error("life params: no species");
    return p;
}

LifeParams LifeParams::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) throw std::runtime_error(path + ": cannot open");
    LifeParams p;
    try {
        p = parse(nlohmann::json::parse(in, nullptr, true, true));
    } catch(const nlohmann::json::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
    p.file_hash = fileSha256(path);
    return p;
}

int LifeParams::speciesIndex(const std::string& id) const {
    for(size_t i = 0; i < species.size(); i++) if(species[i].id == id) return (int) i;
    return -1;
}

float dy::energyDrain(const SpeciesLife& species, float life_scale, float power, float mass, float walk_speed, bool resting) {
    const float basal = 1.f / (species.starve * std::max(life_scale, 1e-3f));
    const float reference = 0.1f * mass * 9.81f * std::max(walk_speed, 0.05f);
    return basal * ((resting ? species.rest_drain : 1.f) + species.work * std::max(power, 0.f) / reference);
}

float dy::woundOf(const LifeParams& params, const SpeciesLife& attacker, float impulse, float victim_mass) {
    const float change = impulse / std::max(victim_mass, 1e-3f);
    if(change <= params.wound_threshold) return 0.f;
    return attacker.bite * (change - params.wound_threshold) / std::max(params.kill_impulse, 1e-4f);
}

GrassField::GrassField(const GrassParams& params, float arena, uint64_t seed) : p(params), arena(arena) {
    side = std::max(1, (int) std::ceil(2.f * arena / p.cell));
    tall.assign((size_t) side * side, 0.f);
    // The engine's height noise for how tall, another draw of it at the wavelength of the patches for where
    GrassHeightParams heights;
    heights.seed = p.seed + (int) (seed % 100000);
    GrassHeightParams patches = heights;
    patches.seed += 7919;
    patches.wavelength = p.patch;
    patches.octaves = 2;
    GrassHeightField height_field(heights), patch_field(patches);
    // The threshold that leaves `cover` of the cells: the noise is not uniform
    std::vector<float> values((size_t) side * side);
    for(int z = 0; z < side; z++) {
        for(int x = 0; x < side; x++) {
            values[(size_t) z * side + x] = patch_field.sample(-arena + (x + 0.5f) * p.cell, -arena + (z + 0.5f) * p.cell);
        }
    }
    std::vector<float> sorted(values);
    std::sort(sorted.begin(), sorted.end());
    const float cover = std::clamp(p.cover, 0.f, 1.f);
    const float threshold = cover >= 1.f ? -1.f : sorted[std::min(sorted.size() - 1, (size_t) ((1.f - cover) * (float) sorted.size()))];
    for(int z = 0; z < side; z++) {
        for(int x = 0; x < side; x++) {
            const size_t i = (size_t) z * side + x;
            if(cover <= 0.f || values[i] < threshold) continue;
            float t = height_field.sample(-arena + (x + 0.5f) * p.cell, -arena + (z + 0.5f) * p.cell);
            tall[i] = p.shortest + (p.tallest - p.shortest) * std::clamp(t, 0.f, 1.f);
        }
    }
    now = tall;
}

int GrassField::index(float x, float z) const {
    if(side == 0) return -1;
    int cx = (int) std::floor((x + arena) / p.cell), cz = (int) std::floor((z + arena) / p.cell);
    if(cx < 0 || cz < 0 || cx >= side || cz >= side) return -1;
    return cz * side + cx;
}

float GrassField::height(float x, float z) const {
    int i = index(x, z);
    return i < 0 ? 0.f : now[(size_t) i];
}

float GrassField::full(float x, float z) const {
    int i = index(x, z);
    return i < 0 ? 0.f : tall[(size_t) i];
}

float GrassField::nearest(float x, float z, float over, float radius) const {
    if(side == 0) return -1.f;
    const int cx = (int) std::floor((x + arena) / p.cell), cz = (int) std::floor((z + arena) / p.cell);
    const int reach = (int) std::ceil(radius / p.cell);
    float best = radius;
    bool found = false;
    // Rings of cells around the point's own; a ring further than the best so far has nothing nearer
    for(int ring = 0; ring <= reach && (float) (ring - 1) * p.cell <= best; ring++) {
        for(int dz = -ring; dz <= ring; dz++) {
            const int step = std::abs(dz) == ring ? 1 : 2 * ring;
            for(int dx = -ring; dx <= ring; dx += std::max(step, 1)) {
                const int ix = cx + dx, iz = cz + dz;
                if(ix < 0 || iz < 0 || ix >= side || iz >= side) continue;
                if(now[(size_t) (iz * side + ix)] <= over) continue;
                if(ring == 0) return 0.f;
                const float px = ((float) ix + 0.5f) * p.cell - arena, pz = ((float) iz + 0.5f) * p.cell - arena;
                const float d = std::sqrt((px - x) * (px - x) + (pz - z) * (pz - z));
                if(d <= best) {
                    best = d;
                    found = true;
                }
            }
        }
    }
    return found ? best : -1.f;
}

float GrassField::eat(float x, float z, float amount) {
    int i = index(x, z);
    if(i < 0 || amount <= 0.f) return 0.f;
    float& h = now[(size_t) i];
    const float eaten = std::clamp(h - p.stubble, 0.f, amount);
    h -= eaten;
    return eaten;
}

void GrassField::grow(float seconds) {
    if(p.regrow <= 0.f) return;
    const float part = seconds / p.regrow;
    for(size_t i = 0; i < now.size(); i++) {
        if(now[i] < tall[i]) now[i] = std::min(tall[i], now[i] + tall[i] * part);
    }
}

float GrassField::standing() const {
    double h = 0., t = 0.;
    for(size_t i = 0; i < now.size(); i++) {
        h += now[i];
        t += tall[i];
    }
    return t > 0. ? (float) (h / t) : 0.f;
}

float GrassField::coverPart() const {
    size_t n = 0;
    for(float t : tall) n += t > 0.f ? 1 : 0;
    return tall.empty() ? 0.f : (float) n / (float) tall.size();
}

std::shared_ptr<const AnimalBody> dy::scaleAnimalBody(const AnimalBody& body, float s) {
    auto out = std::make_shared<AnimalBody>(body);
    AnimalBody& b = *out;
    const float s3 = s * s * s, s4 = s3 * s, root = std::sqrt(s);
    b.mass *= s3;
    b.height *= s;
    b.length *= s;
    b.com_height *= s;
    b.stand_height *= s;
    b.walk_speed *= root;
    b.max_angular_velocity /= root;
    b.max_target_angular_velocity /= root;
    BodyDef& d = b.def;
    d.total_mass *= s3;
    d.height *= s;
    for(SegmentDef& seg : d.segments) {
        seg.rest_position *= s;
        seg.spawn_position *= s;
        seg.center_of_mass *= s;
        // s^5 by the lengths, raised by 1 / s so that the drives (s^4) stay as far from what the solver holds as
        // the adult's are (the body generator raises inertias for the same reason)
        seg.inertia *= s4;
        for(ShapeDef& shape : seg.shapes) {
            shape.center *= s;
            shape.radius *= s;
            shape.half_height *= s;
            shape.half_extents *= s;
            if(shape.convex_radius > 0.f) shape.convex_radius *= s;
        }
    }
    for(JointDef& joint : d.joints) joint.anchor *= s;
    for(glm::vec3& o : b.offset) o *= s;
    for(AnimalJointDrive& drive : b.drives) {
        drive.stiffness *= s4;
        drive.damping *= s4 * root;
        drive.max_torque *= s4;
    }
    return out;
}

std::shared_ptr<const AnimalBody> dy::armAnimalBody(std::shared_ptr<const AnimalBody> body, const SpeciesLife& species, int& segment) {
    segment = -1;
    if(species.weapon_segment.empty()) return body;
    for(size_t i = 0; i < body->def.segments.size(); i++) if(body->def.segments[i].name == species.weapon_segment) segment = (int) i;
    if(segment < 0) return body;
    auto armed = std::make_shared<AnimalBody>(*body);
    SegmentDef& seg = armed->def.segments[(size_t) segment];
    // Where the animal stands the weapon points ahead (-z), level, most of it in front of the hand
    const glm::vec3 hand = seg.spawn_position + seg.spawn_rotation * seg.shapes[0].center;
    const glm::vec3 centre = hand + glm::vec3(0.f, 0.f, -(species.weapon_reach - 0.5f) * species.weapon_length);
    ShapeDef spear;
    spear.type = ShapeDef::Type::Capsule;
    spear.source = "weapon";
    spear.radius = species.weapon_radius;
    spear.half_height = 0.5f * species.weapon_length;
    const glm::quat to_segment = glm::inverse(seg.spawn_rotation);
    spear.center = to_segment * (centre - seg.spawn_position);
    // A capsule's axis is its Y: turned onto the world's -z
    spear.rotation = glm::normalize(to_segment * glm::angleAxis(-1.57079633f, glm::vec3(1.f, 0.f, 0.f)));
    seg.shapes.push_back(spear);
    return armed;
}
