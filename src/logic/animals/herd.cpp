#include "herd.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "logic/agents/ground_field.h"
#include "logic/components/camera.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "logic/vegetation/forest.h"
#include "logic/vegetation/vegetation.h"
#include "util/log.h"

using namespace dy;

namespace {

const float pi = 3.14159265f;
const float trunk_cell = 8.f;

uint32_t mix(uint32_t h, uint32_t value) {
    h ^= value + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return h;
}

// PCG, 24 bits a draw
float unit(uint32_t& state) {
    state = state * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word ^= word >> 22u;
    return (float) (word >> 8) / 16777216.f;
}

float range(uint32_t& state, float low, float high) {
    return low + (high - low) * unit(state);
}

float wrap(float angle) {
    while(angle > pi) angle -= 2.f * pi;
    while(angle < -pi) angle += 2.f * pi;
    return angle;
}

// As Camera : yaw 0 looks toward -z
glm::vec2 forward(float yaw) {
    return glm::vec2(-std::sin(yaw), -std::cos(yaw));
}

uint64_t key(glm::ivec2 cell) {
    return ((uint64_t) (uint32_t) cell.x << 32) | (uint32_t) cell.y;
}

HerdKind::Where whereOf(const std::string& name) {
    if(name == "any") return HerdKind::Where::Any;
    if(name == "open") return HerdKind::Where::Open;
    if(name == "edge") return HerdKind::Where::Edge;
    if(name == "forest") return HerdKind::Where::Forest;
    throw std::runtime_error("herd : where is any, open, edge or forest, not " + name);
}

}



HerdDefinition HerdDefinition::parse(const nlohmann::json& j, AnimalLibrary& library) {

    HerdDefinition definition;
    auto& p = definition.params;
    p.seed = j.value("seed", p.seed);
    p.cell = j.value("cell", p.cell);
    p.density = j.value("density", p.density);
    p.radius = j.value("radius", p.radius);
    p.range = j.value("range", p.range);
    p.max_slope = j.value("max_slope", p.max_slope);
    if(auto it = j.find("graze"); it != j.end()) {
        p.graze_min = it->at(0).get<float>();
        p.graze_max = it->at(1).get<float>();
    }
    if(p.cell < 10.f) throw std::runtime_error("herds : cells under 10 m");

    try {
        if(auto herds = j.find("herds"); herds != j.end()) {
            for(auto& h : *herds) {
                HerdKind kind;
                kind.name = h.value("name", std::string("herd ") + std::to_string(definition.kinds.size()));
                kind.weight = h.value("weight", kind.weight);
                kind.spacing = h.value("spacing", kind.spacing);
                kind.where = whereOf(h.value("where", std::string("any")));
                if(auto it = h.find("count"); it != h.end()) {
                    kind.count_min = it->at(0).get<int>();
                    kind.count_max = std::max(it->at(1).get<int>(), kind.count_min);
                }
                if(auto it = h.find("scale"); it != h.end()) {
                    kind.scale_min = it->at(0).get<float>();
                    kind.scale_max = it->at(1).get<float>();
                }

                float total = 0.f;
                for(auto& [id, member] : h.at("members").items()) {
                    float weight = member.is_object() ? member.value("weight", 1.f) : member.get<float>();
                    float scale = member.is_object() ? member.value("scale", 1.f) : 1.f;
                    int index = library.find(id);
                    if(index < 0) {
                        dy::log(dy::warning) << "Herds : no animal " << id << " in the library\n";
                        continue;
                    }
                    auto species = library.species((uint32_t) index);
                    if(!species || species->walk < 0) {
                        dy::log(dy::warning) << "Herds : " << id << " has no walk, left out of " << kind.name << "\n";
                        continue;
                    }
                    if(weight <= 0.f) continue;
                    total += weight;
                    kind.species.push_back((uint32_t) index);
                    kind.weights.push_back(total);
                    kind.scales.push_back(scale);
                }
                if(kind.species.empty() || kind.weight <= 0.f || kind.count_min <= 0) continue;
                definition.kinds.push_back(std::move(kind));
            }
        }
    } catch(const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("herd definition : ") + e.what());
    }

    return definition;

}



HerdSys::HerdSys(entt::registry& reg, const nlohmann::json& params) : System(reg), params(params) {
    if(auto it = params.find("centre"); it != params.end() && it->is_array() && it->size() == 2) {
        centre = glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
        has_centre = true;
    }
    procedural = params.value("procedural", procedural);
    if(auto it = params.find("style"); it != params.end()) style = GaitStyle::parse(*it);
}

HerdSys::~HerdSys() {}

void HerdSys::preinit() {
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<HerdAnimal>();
    reg.storage<AnimalGait>();
    reg.storage<Transform>();
    reg.storage<Plant>();
    reg.ctx().emplace<HerdStats>();
    reg.ctx().insert_or_assign<HerdSys*>(this);
}

void HerdSys::init() {

    auto& library = animalLibrary(reg);

    try {
        nlohmann::json j = nlohmann::json::object();
        if(auto it = params.find("definition"); it != params.end() && it->is_object()) {
            j = *it;
        } else {
            std::string path = "./resources/" + params.value("definition", std::string("herds/tautavel.herds.json"));
            std::ifstream file(path);
            if(!file) throw std::runtime_error("cannot open " + path);
            j = nlohmann::json::parse(file);
        }
        for(const char* name : { "seed", "cell", "density", "radius", "range", "max_slope", "graze" }) {
            if(auto it = params.find(name); it != params.end()) j[name] = *it;
        }
        definition = HerdDefinition::parse(j, library);
    } catch(const std::exception& e) {
        dy::log(dy::error) << "Herds : " << e.what() << "\n";
        definition = HerdDefinition();
        return;
    }

    // The ground : the world's field when a system made one, else the terrain itself, else a level plane
    if(auto* existing = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) generator = *existing;
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(generator) ground = std::make_shared<TerrainField>(generator, std::make_shared<MarchingCubes>());
    else ground = std::make_shared<PlaneField>();
    if(auto* field = reg.ctx().find<std::shared_ptr<ForestField>>()) forest = *field;

    dy::log(dy::info) << "Herds : " << definition.kinds.size() << " kinds, one cell of " << definition.params.cell << " m in "
        << 1.f / std::max(definition.params.density, 1e-3f) << " has one, kept within " << definition.params.radius << " m\n";

}

float HerdSys::landSlope(glm::vec2 p) const {
    if(!generator) return 0.f;
    const float reach = 6.f;
    float dx = (generator->surfaceHeight(p.x + reach, p.y) - generator->surfaceHeight(p.x - reach, p.y)) / (2.f * reach);
    float dz = (generator->surfaceHeight(p.x, p.y + reach) - generator->surfaceHeight(p.x, p.y - reach)) / (2.f * reach);
    return glm::degrees(std::atan(std::sqrt(dx * dx + dz * dz)));
}

bool HerdSys::suits(const HerdKind& kind, glm::vec2 p) const {
    if(landSlope(p) > definition.params.max_slope) return false;
    if(!forest || kind.where == HerdKind::Where::Any) return true;
    float cover = forest->cover(p.x, p.y);
    switch(kind.where) {
        case HerdKind::Where::Open: return cover < 0.25f;
        case HerdKind::Where::Edge: return cover > 0.1f && cover < 0.8f;
        case HerdKind::Where::Forest: return cover > 0.6f;
        default: return true;
    }
}

float HerdSys::groundHeight(glm::vec2 p, float near_y, bool known) const {
    if(known) {
        if(auto hit = ground->below(glm::vec3(p.x, near_y + 2.5f, p.y), 12.f)) return hit->point.y;
    }
    if(auto hit = ground->top(p.x, p.y)) return hit->point.y;
    return generator ? generator->surfaceHeight(p.x, p.y) : 0.f;
}

bool HerdSys::describe(glm::ivec2 cell, Herd& herd) {

    const HerdParams& p = definition.params;
    if(definition.kinds.empty()) return false;

    uint32_t rng = mix(mix(p.seed * 0x9E3779B9u, (uint32_t) cell.x), (uint32_t) cell.y);
    // Every draw is made whether the herd is kept or not : a parameter only changes what it is about
    float roll = unit(rng);
    float total = 0.f;
    for(auto& kind : definition.kinds) total += kind.weight;
    float pick = unit(rng) * total;
    if(roll >= p.density) return false;

    herd = Herd();
    herd.cell = cell;
    for(size_t k = 0; k < definition.kinds.size(); k++) {
        herd.kind = (uint32_t) k;
        pick -= definition.kinds[k].weight;
        if(pick < 0.f) break;
    }
    const HerdKind& kind = definition.kinds[herd.kind];

    bool found = false;
    for(int attempt = 0; attempt < 10 && !found; attempt++) {
        glm::vec2 candidate = (glm::vec2(cell) + glm::vec2(range(rng, 0.15f, 0.85f), range(rng, 0.15f, 0.85f))) * p.cell;
        if(suits(kind, candidate)) {
            herd.home = candidate;
            found = true;
        }
    }
    if(!found) return false;

    auto& library = animalLibrary(reg);
    int count = kind.count_min + (int) (unit(rng) * (float) (kind.count_max - kind.count_min + 1));
    count = std::clamp(count, kind.count_min, kind.count_max);
    herd.extent = kind.spacing * std::sqrt((float) count) * 0.55f;
    herd.centre = herd.destination = herd.home;
    herd.speed = 1e9f;

    for(int i = 0; i < count; i++) {
        Member member;
        float choice = unit(rng) * kind.weights.back();
        size_t s = std::lower_bound(kind.weights.begin(), kind.weights.end(), choice) - kind.weights.begin();
        s = std::min(s, kind.species.size() - 1);
        uint32_t index = kind.species[s];
        member.species = library.species(index);
        member.scale = range(rng, kind.scale_min, kind.scale_max) * kind.scales[s];

        // Apart from the ones already placed, when there is room
        for(int attempt = 0; attempt < 12; attempt++) {
            float r = herd.extent * std::sqrt(unit(rng)), a = unit(rng) * 2.f * pi;
            member.slot = r * glm::vec2(std::cos(a), std::sin(a));
            bool apart = true;
            for(auto& other : herd.members) apart = apart && glm::distance(other.slot, member.slot) > 0.7f * kind.spacing;
            if(apart) break;
        }
        member.spot = member.slot;
        member.position = herd.home + member.slot;
        member.yaw = unit(rng) * 2.f * pi;
        member.timer = range(rng, 0.f, 8.f);
        if(!member.species) continue;

        const auto& entry = library.entries[index];
        member.length = std::max(entry.length, 0.1f) * member.scale;
        member.width = std::max(entry.width, 0.05f) * member.scale;
        float clip_speed = member.species->walk >= 0 ? member.species->clips[member.species->walk].speed : 0.f;
        member.walk_speed = (clip_speed > 0.02f ? clip_speed : 0.6f * std::max(entry.length, 0.1f)) * member.scale;
        herd.speed = std::min(herd.speed, 0.85f * member.walk_speed);
        herd.members.push_back(std::move(member));
    }
    if(herd.members.empty()) return false;

    herd.timer = range(rng, 4.f, 0.6f * p.graze_max);
    herd.rng = rng;
    return true;

}

bool HerdSys::nearest(const std::string& kind, glm::vec2 from, float reach, glm::vec2& home) {
    const float cell_size = definition.params.cell;
    glm::ivec2 low = glm::ivec2(glm::floor((from - reach) / cell_size)), high = glm::ivec2(glm::floor((from + reach) / cell_size));
    float best = reach;
    bool found = false;
    for(int cx = low.x; cx <= high.x; cx++) {
        for(int cz = low.y; cz <= high.y; cz++) {
            Herd herd;
            if(!describe(glm::ivec2(cx, cz), herd) || definition.kinds[herd.kind].name != kind) continue;
            float d = glm::distance(herd.home, from);
            if(d < best) {
                best = d;
                home = herd.home;
                found = true;
            }
        }
    }
    return found;
}

void HerdSys::spawn(Herd& herd) {
    auto& library = animalLibrary(reg);
    for(auto& member : herd.members) {
        member.entity = reg.create();
        reg.emplace<Transform>(member.entity, glm::vec3(member.position.x, 0.f, member.position.y), glm::angleAxis(member.yaw, glm::vec3(0.f, 1.f, 0.f)), glm::vec3(member.scale));
        auto& animal = reg.emplace<Animal>(member.entity);
        animal.species = (uint32_t) library.find(member.species->id);
        animal.clip = member.species->idle >= 0 ? member.species->idle : member.species->walk;
        animal.time = member.timer;
        reg.emplace<HerdAnimal>(member.entity, herd.kind, herd.cell);
        if(procedural) reg.emplace<AnimalGait>(member.entity).style = style;
    }
}

void HerdSys::despawn(Herd& herd) {
    for(auto& member : herd.members) {
        if(reg.valid(member.entity)) reg.destroy(member.entity);
    }
}

void HerdSys::rebuildTrunks() {
    trunks.clear();
    auto* library = reg.ctx().find<VegetationLibrary>();
    if(!library) return;
    for(auto [entity, transform, plant] : reg.view<const Transform, const Plant>().each()) {
        if(plant.species >= library->species.size()) continue;
        float height = library->species[plant.species].height * transform.scale.y;
        if(height < 2.5f) continue;
        Trunk trunk { glm::vec2(transform.position.x, transform.position.z), std::min(0.15f + 0.015f * height, 0.6f) };
        trunks[key(glm::ivec2(glm::floor(trunk.position / trunk_cell)))].push_back(trunk);
    }
}

void HerdSys::updateHerd(Herd& herd, float dt) {

    const HerdParams& p = definition.params;
    const HerdKind& kind = definition.kinds[herd.kind];

    if(herd.travelling) {
        // The herd waits for those behind
        float behind = 0.f;
        for(auto& member : herd.members) behind += glm::distance(member.position, herd.centre + member.spot);
        behind /= (float) herd.members.size();
        float speed = herd.speed * (behind > 1.5f * herd.extent + 3.f ? 0.25f : 1.f);

        glm::vec2 to = herd.destination - herd.centre;
        float distance = glm::length(to);
        if(distance < std::max(speed * dt, 0.5f)) {
            herd.centre = herd.destination;
            herd.travelling = false;
            herd.timer = range(herd.rng, p.graze_min, p.graze_max);
        } else {
            herd.centre += to / distance * speed * dt;
        }
    } else {
        herd.timer -= dt;
        if(herd.timer <= 0.f) {
            herd.timer = range(herd.rng, 5.f, 15.f);
            // Somewhere else around its home, on land that suits it
            for(int attempt = 0; attempt < 12; attempt++) {
                float r = p.range * std::sqrt(unit(herd.rng)), a = unit(herd.rng) * 2.f * pi;
                glm::vec2 candidate = herd.home + r * glm::vec2(std::cos(a), std::sin(a));
                if(glm::distance(candidate, herd.centre) < 3.f * herd.extent + 10.f) continue;
                // And the way there is not across a slope
                bool clear = suits(kind, candidate);
                for(int step = 1; step < 4 && clear; step++) clear = landSlope(glm::mix(herd.centre, candidate, step / 4.f)) <= p.max_slope + 6.f;
                if(clear) {
                    herd.destination = candidate;
                    herd.travelling = true;
                    break;
                }
            }
        }
    }

    for(auto& member : herd.members) {
        updateMember(herd, member, dt);
    }

}

void HerdSys::updateMember(Herd& herd, Member& m, float dt) {

    if(!reg.valid(m.entity)) return;
    const HerdKind& kind = definition.kinds[herd.kind];
    const AnimalSpecies& species = *m.species;
    auto& animal = reg.get<Animal>(m.entity);
    const float reach = 0.4f * m.length + 0.3f;

    // Standing : graze, look around, or go and stand a little further
    if(!m.moving) {
        m.timer -= dt;
        if(m.timer <= 0.f) {
            if(!herd.travelling && unit(herd.rng) < 0.3f) {
                float r = 0.5f * kind.spacing * std::sqrt(unit(herd.rng)) + 1.2f * reach, a = unit(herd.rng) * 2.f * pi;
                m.spot = m.slot + r * glm::vec2(std::cos(a), std::sin(a));
            }
            std::vector<int> options;
            for(size_t c = 0; c < species.clips.size(); c++) {
                const AnimalClip& clip = species.clips[c];
                if(clip.motion_joint >= 0 || clip.frames < 8) continue;
                // Grazing twice as often as looking around
                if(clip.kind == "idle") options.push_back((int) c);
                if(clip.kind == "eat" && !herd.travelling) options.insert(options.end(), 2, (int) c);
            }
            int next = options.empty() ? species.idle : options[std::min((size_t) (unit(herd.rng) * (float) options.size()), options.size() - 1)];
            if(next >= 0) {
                const AnimalClip& clip = species.clips[next];
                animal.play(next, 0.5f);
                animal.rate = 1.f;
                m.timer = clip.loop ? range(herd.rng, 5.f, 14.f) : std::max(clip.duration(), 1.f);
            } else {
                m.timer = 5.f;
            }
        }
    }

    glm::vec2 target = herd.centre + m.spot;
    glm::vec2 to = target - m.position;
    float distance = glm::length(to);
    if(!m.moving && distance > (herd.travelling ? 1.5f : 1.f) * reach) m.moving = true;
    if(m.moving && distance < 0.3f * reach) {
        m.moving = false;
        m.timer = 0.f;
    }

    glm::vec2 steer = distance > 1e-4f ? to / distance : forward(m.yaw);

    // Away from the others and from the trunks
    glm::vec2 push(0.f);
    for(auto& other : herd.members) {
        if(&other == &m) continue;
        glm::vec2 away = m.position - other.position;
        float d = glm::length(away), keep = 0.45f * (m.length + other.length);
        if(d < keep && d > 1e-4f) push += away / d * (1.f - d / keep);
    }
    const float margin = 0.5f * m.width + 0.9f;
    glm::ivec2 cell = glm::ivec2(glm::floor(m.position / trunk_cell));
    for(int cx = -1; cx <= 1; cx++) {
        for(int cz = -1; cz <= 1; cz++) {
            auto it = trunks.find(key(cell + glm::ivec2(cx, cz)));
            if(it == trunks.end()) continue;
            for(const Trunk& trunk : it->second) {
                glm::vec2 away = m.position - trunk.position;
                float d = glm::length(away);
                if(d >= trunk.radius + margin || d < 1e-4f) continue;
                // Around it rather than back from it
                glm::vec2 side(-steer.y, steer.x);
                if(glm::dot(side, away) < 0.f) side = -side;
                float near = 1.f - (d - trunk.radius) / margin;
                push += (away / d + side) * 1.5f * std::clamp(near, 0.f, 1.f);
                // Never inside a trunk
                float inside = trunk.radius + 0.35f * m.width - d;
                if(inside > 0.f) m.position += away / d * inside;
            }
        }
    }
    if(m.moving) {
        glm::vec2 combined = steer + push;
        if(glm::length(combined) > 1e-3f) steer = glm::normalize(combined);
    } else if(glm::length(push) > 0.6f) {
        // Too close to stand there : a step aside
        m.spot += glm::normalize(push) * reach;
    }

    float target_speed = 0.f;
    if(m.moving) {
        float error = wrap(std::atan2(-steer.x, -steer.y) - m.yaw);
        float turn = std::clamp(2.2f / std::max(m.length, 0.3f), 0.9f, 4.f) * dt;
        m.yaw = wrap(m.yaw + std::clamp(error, -turn, turn));
        // Slower through a turn and on arriving, faster to catch up with the herd
        float aligned = std::clamp(0.25f + 0.75f * std::cos(error), 0.2f, 1.f);
        float arriving = std::clamp(distance / (2.f * reach), 0.35f, 1.f);
        float behind = herd.travelling && distance > herd.extent ? 1.3f : 1.f;
        target_speed = m.walk_speed * aligned * arriving * behind;
    }
    float change = 2.5f * m.walk_speed * dt;
    m.speed += std::clamp(target_speed - m.speed, -change, change);
    m.position += forward(m.yaw) * m.speed * dt;

    // On the ground, leaning with it
    glm::vec2 ahead = forward(m.yaw) * 0.3f * m.length;
    float here = groundHeight(m.position, m.height, m.placed);
    float front = groundHeight(m.position + ahead, here, true), back = groundHeight(m.position - ahead, here, true);
    float slope = std::clamp(std::atan2(front - back, 0.6f * m.length), -0.6f, 0.6f);
    m.pitch = m.placed ? m.pitch + (slope - m.pitch) * std::min(8.f * dt, 1.f) : slope;
    m.height = (here + front + back) / 3.f;
    m.placed = true;

    if(m.speed > 0.1f * m.walk_speed && species.walk >= 0) {
        float clip_speed = species.clips[species.walk].speed * m.scale;
        animal.play(species.walk, 0.35f);
        // The procedural walk sets the pace of its steps itself
        if(!reg.all_of<AnimalGait>(m.entity)) animal.rate = clip_speed > 1e-3f ? std::clamp(m.speed / clip_speed, 0.5f, 1.8f) : 1.f;
    } else if(animal.clip == species.walk && !m.moving) {
        m.timer = 0.f;
    }

    auto& transform = reg.get<Transform>(m.entity);
    transform.position = glm::vec3(m.position.x, m.height - species.min.y * m.scale * (std::abs(species.min.y) < 0.2f * (species.max.y - species.min.y) ? 1.f : 0.f), m.position.y);
    transform.rotation = glm::angleAxis(m.yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(m.pitch, glm::vec3(1.f, 0.f, 0.f));

    auto& tag = reg.get<HerdAnimal>(m.entity);
    tag.moving = m.moving;
    tag.speed = m.speed;

}

void HerdSys::tick(double dt_seconds) {

    OPTICK_EVENT();

    if(definition.kinds.empty() || !ground) return;
    const HerdParams& p = definition.params;
    const float dt = (float) std::min(dt_seconds, 0.1);

    glm::vec2 viewer = centre;
    if(!has_centre) {
        if(auto* camera = reg.ctx().find<Camera>()) viewer = glm::vec2(camera->position.x, camera->position.z);
    }

    // Herds come and go with the viewer
    for(auto it = herds.begin(); it != herds.end();) {
        if(glm::distance(it->centre, viewer) > p.radius + 0.4f * p.cell) {
            despawn(*it);
            it = herds.erase(it);
        } else {
            ++it;
        }
    }
    glm::ivec2 low = glm::ivec2(glm::floor((viewer - p.radius) / p.cell)), high = glm::ivec2(glm::floor((viewer + p.radius) / p.cell));
    scan_timer -= dt;
    if(scan_timer > 0.f) high = low - 1;
    else scan_timer = 0.5f;
    for(int cx = low.x; cx <= high.x; cx++) {
        for(int cz = low.y; cz <= high.y; cz++) {
            glm::ivec2 cell(cx, cz);
            if(std::any_of(herds.begin(), herds.end(), [&](const Herd& h) { return h.cell == cell; })) continue;
            Herd herd;
            if(!describe(cell, herd) || glm::distance(herd.home, viewer) > p.radius) continue;
            spawn(herd);
            dy::log(dy::debug) << "Herds : " << definition.kinds[herd.kind].name << ", " << herd.members.size() << " animals around "
                << herd.home.x << " " << groundHeight(herd.home, 0.f, false) << " " << herd.home.y << std::endl;
            herds.push_back(std::move(herd));
        }
    }

    // Trunks : again when plants came or went, not more than twice a second
    trunk_timer -= dt;
    size_t plants = reg.storage<Plant>().size();
    if(plants != trunk_plants && trunk_timer <= 0.f) {
        rebuildTrunks();
        trunk_plants = plants;
        trunk_timer = 0.5f;
    }

    auto& stats = reg.ctx().get<HerdStats>();
    stats = HerdStats();
    for(auto& herd : herds) {
        updateHerd(herd, dt);
        stats.herds++;
        stats.travelling += herd.travelling;
        HerdInfo info { herd.cell, definition.kinds[herd.kind].name };
        for(auto& member : herd.members) {
            stats.animals++;
            stats.moving += member.moving;
            info.centre += glm::vec3(member.position.x, member.height, member.position.y);
        }
        info.animals = (uint32_t) herd.members.size();
        info.centre /= (float) herd.members.size();
        for(auto& member : herd.members) {
            info.extent = std::max(info.extent, glm::distance(member.position, glm::vec2(info.centre.x, info.centre.z)) + 0.5f * member.length);
        }
        info.travelling = herd.travelling;
        stats.list.push_back(std::move(info));
    }

}

void HerdSys::finish() {
    for(auto& herd : herds) despawn(herd);
    herds.clear();
}



HerdWatchSys::HerdWatchSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    distance = params.value("distance", distance);
    height = params.value("height", height);
    orbit = params.value("orbit", orbit);
    kind = params.value("kind", kind);
    clear = params.value("clear", clear);
}

float HerdWatchSys::clearance(float at_angle) const {
    glm::vec2 centre(focus.x, focus.z);
    glm::vec2 at = centre + radius * glm::vec2(std::sin(at_angle), std::cos(at_angle));
    glm::vec2 line = centre - at;
    float best = 1e9f;
    for(const glm::vec4& tree : trees) {
        glm::vec2 p(tree.x, tree.y);
        // Leaves around the camera hide everything, a crown further on only a part of the herd
        float t = std::clamp(glm::dot(p - at, line) / std::max(glm::dot(line, line), 1e-6f), 0.f, 0.8f);
        best = std::min(best, glm::distance(p, at + t * line) - tree.w);
        best = std::min(best, glm::distance(p, at) - tree.z - 1.f);
    }
    return best;
}

void HerdWatchSys::init() {
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());
    else ground = std::make_shared<PlaneField>();
}

void HerdWatchSys::tick(double dt_seconds) {

    auto* stats = reg.ctx().find<HerdStats>();
    auto* camera = reg.ctx().find<Camera>();
    if(!stats || !camera) return;

    // The kind asked for may be far : the camera goes to the nearest herd of it, the herds follow the camera
    if(!searched && !kind.empty()) {
        searched = true;
        bool near = std::any_of(stats->list.begin(), stats->list.end(), [&](const HerdInfo& info) { return info.kind == kind; });
        glm::vec2 home;
        auto* herds = reg.ctx().find<HerdSys*>();
        if(!near && herds && (*herds)->nearest(kind, glm::vec2(camera->position.x, camera->position.z), 3000.f, home)) {
            float y = camera->position.y;
            if(auto top = ground->top(home.x, home.y)) y = top->point.y + 3.f;
            camera->position = glm::vec3(home.x, y, home.y + 15.f);
            wait = 2.f;
            return;
        }
    }
    if(stats->list.empty()) return;
    if(!has_herd && wait > 0.f) {
        wait -= (float) dt_seconds;
        if(std::none_of(stats->list.begin(), stats->list.end(), [&](const HerdInfo& info) { return info.kind == kind; })) return;
    }
    const float dt = (float) std::min(dt_seconds, 0.1);

    const HerdInfo* herd = nullptr;
    if(has_herd) {
        for(auto& info : stats->list) {
            if(info.cell == cell) herd = &info;
        }
    }
    if(!herd) {
        // The nearest, one of the kind asked for or one on its way first
        float best = 1e9f;
        for(auto& info : stats->list) {
            // And one that has company
            int around = 0;
            for(auto& other : stats->list) around += &other != &info && glm::distance(other.centre, info.centre) < 70.f;
            float score = glm::distance(glm::vec2(info.centre.x, info.centre.z), glm::vec2(camera->position.x, camera->position.z))
                - (info.travelling ? 150.f : 0.f) - (!kind.empty() && info.kind == kind ? 5000.f : 0.f) - 4.f * (float) info.animals - 60.f * (float) around;
            if(score < best) {
                best = score;
                herd = &info;
            }
        }
        dy::log(dy::info) << "HerdWatch : " << herd->kind << ", " << herd->animals << " animals around " << herd->centre.x << " " << herd->centre.y
            << " " << herd->centre.z << (kind.empty() ? "" : " (asked for " + kind + ")") << std::endl;
        cell = herd->cell;
        focus = herd->centre;
        radius = 0.7f * herd->extent + distance;
        angle = std::atan2(camera->position.x - focus.x, camera->position.z - focus.z);
        has_herd = true;
        eye = 0.f;
    }

    // The camera follows the herd without its jolts
    float follow = std::min(1.5f * dt, 1.f);
    focus += (herd->centre - focus) * follow;
    radius += (0.7f * herd->extent + distance - radius) * follow;

    // Crowns that come down to the camera, around the herd
    tree_timer -= dt;
    if(clear && tree_timer <= 0.f) {
        tree_timer = 0.5f;
        trees.clear();
        if(auto* library = reg.ctx().find<VegetationLibrary>()) {
            for(auto [entity, transform, plant] : reg.view<const Transform, const Plant>().each()) {
                if(plant.species >= library->species.size()) continue;
                glm::vec2 p(transform.position.x, transform.position.z);
                if(glm::distance(p, glm::vec2(focus.x, focus.z)) > radius + 30.f) continue;
                const auto& species = library->species[plant.species];
                float tall = species.height * transform.scale.y;
                if(tall < 1.5f) continue;
                float crown = std::min(species.radius * transform.scale.y, 0.6f * tall + 1.f);
                trees.push_back(glm::vec4(p, crown, std::max(crown * (0.2f + 0.8f * std::clamp((14.f - tall) / 9.f, 0.f, 1.f)), 0.5f)));
            }
        }
    }

    // Round the herd, as long as the view ahead is open : else towards the nearest angle that is
    float turn = glm::radians(orbit);
    if(clear && !trees.empty() && std::abs(orbit) > 1e-3f) {
        const float lead = glm::radians(orbit > 0.f ? 20.f : -20.f), open = 4.f;
        float best = -1e9f, offset = lead;
        for(int i = -18; i < 18; i++) {
            float candidate = lead + glm::radians(10.f * (float) i);
            float score = std::min(clearance(angle + candidate), open) - open * std::abs((float) i) / 9.f;
            if(score > best) {
                best = score;
                offset = candidate;
            }
        }
        turn = std::clamp(offset / lead, -4.f, 4.f) * glm::radians(orbit);
    }
    spin = has_spin ? spin + (turn - spin) * std::min(2.f * dt, 1.f) : turn;
    has_spin = true;
    angle += spin * dt;

    glm::vec2 at =glm::vec2(focus.x, focus.z) + radius * glm::vec2(std::sin(angle), std::cos(angle));
    float floor_y = focus.y;
    if(auto hit = ground->below(glm::vec3(at.x, focus.y + 30.f, at.y), 80.f)) floor_y = hit->point.y;
    else if(auto top = ground->top(at.x, at.y)) floor_y = top->point.y;
    // Over the ground between the camera and the herd
    float wanted = floor_y + height;
    glm::vec3 aim = focus + glm::vec3(0.f, 0.8f, 0.f);
    for(int i = 1; i <= 8; i++) {
        float t = (float) i / 10.f;
        glm::vec2 p = glm::mix(at, glm::vec2(focus.x, focus.z), t);
        if(auto hit = ground->below(glm::vec3(p.x, focus.y + 30.f, p.y), 80.f)) {
            wanted = std::max(wanted, (hit->point.y + 0.7f - aim.y * t) / (1.f - t));
        }
    }
    wanted = std::min(wanted, floor_y + height + 0.6f * radius);
    eye = eye == 0.f ? wanted : eye + (wanted - eye) * std::min(2.f * dt, 1.f);

    camera->position = glm::vec3(at.x, eye, at.y);
    glm::vec3 to = focus + glm::vec3(0.f, 0.8f, 0.f) - camera->position;
    camera->yaw = std::atan2(-to.x, -to.z);
    camera->pitch = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z));

}
