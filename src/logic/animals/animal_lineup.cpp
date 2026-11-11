#include "animal_lineup.h"

#include <algorithm>
#include <cmath>

#include "logic/agents/ground_field.h"
#include "logic/animals/gait.h"
#include "logic/animals/gait_scene.h"
#include "logic/animals/herd.h"
#include "logic/components/camera.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

AnimalLineupSys::AnimalLineupSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    if(auto it = params.find("species"); it != params.end() && it->is_array()) {
        for(auto& id : *it) ids.push_back(id.get<std::string>());
    }
    if(auto it = params.find("origin"); it != params.end() && it->is_array() && it->size() == 2) {
        origin = glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
        has_origin = true;
    }
    move_camera = params.value("camera", move_camera);
    per_row = std::max(params.value("per_row", per_row), 1);
    gap = params.value("gap", gap);
    clips = params.value("clips", clips);
    seconds = params.value("seconds", seconds);
    yaw = params.value("yaw", yaw);
}

glm::vec2 dy::levelGround(const TerrainGenerator& generator, glm::vec2 half_size) {
    glm::vec2 best_place(0.f);
    float best = 1e9f;
    for(int i = -20; i <= 20; i++) {
        for(int j = -20; j <= 20; j++) {
            glm::vec2 candidate = glm::vec2((float) i, (float) j) * 20.f;
            float low = 1e9f, high = -1e9f;
            for(int a = -2; a <= 2; a++) {
                for(int b = -2; b <= 2; b++) {
                    float h = generator.surfaceHeight(candidate.x + (float) a * 0.5f * half_size.x, candidate.y + (float) b * 0.5f * half_size.y);
                    low = std::min(low, h);
                    high = std::max(high, h);
                }
            }
            // Near the origin of the world when it is as level
            float score = high - low + 0.002f * glm::length(candidate);
            if(score < best) {
                best = score;
                best_place = candidate;
            }
        }
    }
    return best_place;
}

void AnimalLineupSys::preinit() {
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<AnimalShow>();
    reg.storage<Transform>();
}

void AnimalLineupSys::init() {

    auto& library = animalLibrary(reg);
    std::vector<uint32_t> selected;
    if(ids.empty()) {
        for(uint32_t i = 0; i < library.entries.size(); i++) selected.push_back(i);
    } else {
        for(auto& id : ids) {
            int index = library.find(id);
            if(index >= 0) selected.push_back((uint32_t) index);
            else dy::log(dy::warning) << "AnimalLineup : no animal " << id << " in the library\n";
        }
    }
    if(selected.empty()) return;

    std::shared_ptr<GroundField> ground;
    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(generator) ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());

    // Without an origin : the most level ground around
    if(!has_origin && generator) {
        origin = levelGround(**generator, glm::vec2(12.f, 14.f)) + glm::vec2(0.f, 7.f);
        dy::log(dy::info) << "AnimalLineup : origin " << origin.x << " " << origin.y << "\n";
    }
    if(move_camera) {
        if(auto* camera = reg.ctx().find<Camera>()) {
            float y = generator ? (*generator)->surfaceHeight(origin.x, origin.y + 8.f) : 0.f;
            if(ground) {
                if(auto hit = ground->top(origin.x, origin.y + 8.f)) y = hit->point.y;
            }
            camera->position = glm::vec3(origin.x, y + 3.f, origin.y + 8.f);
            camera->yaw = 0.f;
            camera->pitch = -0.25f;
        }
    }

    // Rows of animals sorted by size, the small ones in front
    std::sort(selected.begin(), selected.end(), [&](uint32_t a, uint32_t b) {
        float sa = std::max(library.entries[a].length, library.entries[a].height), sb = std::max(library.entries[b].length, library.entries[b].height);
        return sa != sb ? sa < sb : a < b;
    });

    float z = origin.y;
    for(size_t first = 0; first < selected.size(); first += (size_t) per_row) {
        size_t last = std::min(first + (size_t) per_row, selected.size());
        float width = 0.f, depth = 0.f;
        for(size_t i = first; i < last; i++) {
            const auto& entry = library.entries[selected[i]];
            width += std::max(entry.width, 0.3f * entry.length) + gap;
            depth = std::max(depth, std::max(entry.length, entry.width));
        }
        z -= 0.5f * depth;

        float x = origin.x - 0.5f * width;
        for(size_t i = first; i < last; i++) {
            const auto& entry = library.entries[selected[i]];
            float room = std::max(entry.width, 0.3f * entry.length) + gap;
            x += 0.5f * room;

            auto species = library.species(selected[i]);
            if(species) {
                glm::vec3 position(x, 0.f, z);
                if(ground) {
                    if(auto hit = ground->top(x, z)) position.y = hit->point.y;
                } else if(generator) {
                    position.y = (*generator)->surfaceHeight(x, z);
                }
                bool grounded = std::abs(species->min.y) < 0.2f * (species->max.y - species->min.y);
                if(grounded) position.y -= species->min.y;

                auto entity = reg.create();
                reg.emplace<Transform>(entity, position, glm::angleAxis(glm::radians(yaw), glm::vec3(0.f, 1.f, 0.f)));
                auto& animal = reg.emplace<Animal>(entity);
                animal.species = selected[i];
                auto& show = reg.emplace<AnimalShow>(entity);
                for(size_t c = 0; c < species->clips.size(); c++) {
                    const AnimalClip& clip = species->clips[c];
                    if(clips == "all" || clip.kind == clips || clip.name == clips) show.clips.push_back((int) c);
                }
                // The plain clip of the kind first
                int plain = clips == "walk" ? species->walk : clips == "idle" ? species->idle : clips == "run" ? species->run
                    : clips == "eat" ? species->eat : clips == "rest" ? species->rest_clip : -1;
                if(plain >= 0) {
                    show.clips.erase(std::remove(show.clips.begin(), show.clips.end(), plain), show.clips.end());
                    show.clips.insert(show.clips.begin(), plain);
                }
                dy::log(dy::info) << "AnimalLineup : " << entry.id << " at " << position.x << " " << position.y << " " << position.z
                    << " (" << entry.height << " m tall, " << show.clips.size() << " clips)\n";
            }
            x += 0.5f * room;
        }
        z -= 0.5f * depth + gap;
    }

}

void AnimalLineupSys::tick(double dt) {

    auto& library = animalLibrary(reg);
    for(auto [entity, animal, show] : reg.view<Animal, AnimalShow>().each()) {
        if(show.clips.empty()) continue;
        show.left -= (float) dt;
        if(show.left > 0.f && animal.clip >= 0) continue;
        auto species = library.species(animal.species);
        if(!species) continue;
        if(animal.clip >= 0) show.at = (show.at + 1) % show.clips.size();
        const AnimalClip& clip = species->clips[show.clips[show.at]];
        animal.play(show.clips[show.at], 0.25f);
        animal.rate = 1.f;
        // A clip that loops is held for a few rounds, one that does not is played once
        float duration = std::max(clip.duration(), 0.1f);
        show.left = clip.loop ? duration * std::max(std::round(seconds / duration), 1.f) : duration + 0.5f;
    }

}

void dy::registerAnimals() {
    registerSystem("AnimalAnimation", systemInfo<AnimalAnimationSys>(SystemScope::Host));
    registerSystem("AnimalGait", systemInfo<AnimalGaitSys>(SystemScope::Host));
    registerSystem("AnimalWalkers", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<AnimalWalkersSys>(reg, params); },
        SystemScope::Host });
    registerSystem("AnimalLineup", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<AnimalLineupSys>(reg, params); },
        SystemScope::Host });
    registerSystem("HerdWatch", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<HerdWatchSys>(reg, params); },
        SystemScope::Host });
    registerSystem("Herds", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<HerdSys>(reg, params); },
        SystemScope::Host });
}
