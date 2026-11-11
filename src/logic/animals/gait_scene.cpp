#include "gait_scene.h"

#include <algorithm>
#include <cmath>

#include "logic/agents/ground_field.h"
#include "logic/animals/animal_lineup.h"
#include "logic/components/camera.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

namespace {

const float pi = 3.14159265f;

AnimalWalker::Path pathOf(const std::string& name) {
    if(name == "straight") return AnimalWalker::Path::Straight;
    if(name == "eight") return AnimalWalker::Path::Eight;
    if(name == "weave") return AnimalWalker::Path::Weave;
    return AnimalWalker::Path::Circle;
}

float heightAt(const GroundField* ground, glm::vec2 p, float near_y, bool known) {
    if(!ground) return 0.f;
    if(known) {
        if(auto hit = ground->below(glm::vec3(p.x, near_y + 2.5f, p.y), 12.f)) return hit->point.y;
    }
    if(auto hit = ground->top(p.x, p.y)) return hit->point.y;
    return near_y;
}

}

float dy::walkerCurvature(const AnimalWalker& walker) {
    const float r = std::max(walker.radius, 0.1f);
    switch(walker.path) {
        case AnimalWalker::Path::Straight: return 0.f;
        case AnimalWalker::Path::Circle: return 1.f / r;
        // One loop to the left, one to the right
        case AnimalWalker::Path::Eight: return std::fmod(walker.travelled, 4.f * pi * r) < 2.f * pi * r ? 1.f / r : -1.f / r;
        case AnimalWalker::Path::Weave: return std::sin(walker.travelled / (1.5f * r)) / r;
        case AnimalWalker::Path::Free: return walker.turn;
    }
    return 0.f;
}

void dy::stepWalker(AnimalWalker& walker, Transform& transform, float length, float sole, const GroundField* ground, float dt) {

    if(!walker.placed) {
        const glm::vec3 facing = transform.rotation * glm::vec3(0.f, 0.f, -1.f);
        walker.yaw = std::atan2(-facing.x, -facing.z);
    }
    walker.yaw += walkerCurvature(walker) * walker.speed * dt;
    const glm::vec2 forward(-std::sin(walker.yaw), -std::cos(walker.yaw));
    glm::vec2 position = glm::vec2(transform.position.x, transform.position.z) + forward * walker.speed * dt;
    walker.travelled += walker.speed * dt;

    // On the ground, leaning with it
    glm::vec2 ahead = forward * 0.3f * length;
    float here = heightAt(ground, position, walker.height, walker.placed);
    float front = heightAt(ground, position + ahead, here, true), back = heightAt(ground, position - ahead, here, true);
    float slope = std::clamp(std::atan2(front - back, 0.6f * length), -0.6f, 0.6f);
    walker.pitch = walker.placed ? walker.pitch + (slope - walker.pitch) * std::min(8.f * dt, 1.f) : slope;
    walker.height = (here + front + back) / 3.f;
    walker.placed = true;

    transform.position = glm::vec3(position.x, walker.height - sole, position.y);
    transform.rotation = glm::angleAxis(walker.yaw, glm::vec3(0.f, 1.f, 0.f)) * glm::angleAxis(walker.pitch, glm::vec3(1.f, 0.f, 0.f));

}



AnimalWalkersSys::AnimalWalkersSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    if(auto it = params.find("species"); it != params.end() && it->is_array()) {
        for(auto& id : *it) ids.push_back(id.get<std::string>());
    } else if(it != params.end() && it->is_string() && !it->get<std::string>().empty()) {
        // "a,b,c" : a scene variable
        std::string list = it->get<std::string>();
        for(size_t start = 0; start <= list.size();) {
            size_t end = std::min(list.find(',', start), list.size());
            if(end > start) ids.push_back(list.substr(start, end - start));
            start = end + 1;
        }
    }
    path = pathOf(params.value("path", std::string("circle")));
    radius = params.value("radius", radius);
    speed = params.value("speed", speed);
    gait = params.value("gait", gait);
    procedural = params.value("procedural", procedural);
    gap = params.value("gap", gap);
    move_camera = params.value("camera", move_camera);
    follow = params.value("follow", follow);
    orbit = params.value("orbit", orbit);
    if(auto it = params.find("style"); it != params.end()) style = GaitStyle::parse(*it);
    if(auto it = params.find("origin"); it != params.end() && it->is_array() && it->size() == 2) {
        origin = glm::vec2(it->at(0).get<float>(), it->at(1).get<float>());
        has_origin = true;
    }
}

void AnimalWalkersSys::preinit() {
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<AnimalGait>();
    reg.storage<AnimalWalker>();
    reg.storage<Transform>();
}

void AnimalWalkersSys::init() {

    auto& library = animalLibrary(reg);
    if(ids.empty()) ids = { "horse_farm", "deer_red", "wolf_2", "rhinoceros", "bearbrown", "lioness", "cat", "crow", "homo_heidelbergensis_walking" };

    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(generator) ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());
    else ground = std::make_shared<PlaneField>();

    struct Entry {
        uint32_t index;
        std::shared_ptr<const AnimalSpecies> species;
        int clip;
        float radius;
    };
    std::vector<Entry> entries;
    float width = 0.f, widest = 0.f;
    for(auto& id : ids) {
        int index = library.find(id);
        auto species = index >= 0 ? library.species((uint32_t) index) : nullptr;
        if(!species) {
            dy::log(dy::warning) << "AnimalWalkers : no animal " << id << " in the library\n";
            continue;
        }
        int clip = gait == "run" ? species->run : gait == "trot" ? species->trot : species->walk;
        if(clip < 0) clip = species->walk;
        if(clip < 0) {
            dy::log(dy::warning) << "AnimalWalkers : " << id << " has no walk\n";
            continue;
        }
        float r = radius * std::max(library.entries[index].length, 0.3f);
        entries.push_back({ (uint32_t) index, species, clip, r });
        width += 2.f * r + gap;
        widest = std::max(widest, r);
    }
    if(entries.empty()) return;

    if(!has_origin && generator) origin = levelGround(**generator, glm::vec2(0.5f * width, 2.f * widest));
    if(move_camera) {
        if(auto* camera = reg.ctx().find<Camera>()) {
            glm::vec2 at = origin + glm::vec2(0.f, widest + 9.f);
            camera->position = glm::vec3(at.x, heightAt(ground.get(), at, 0.f, false) + 3.5f, at.y);
            camera->yaw = 0.f;
            camera->pitch = -0.3f;
        }
    }

    float x = origin.x - 0.5f * width;
    for(auto& entry : entries) {
        x += entry.radius + 0.5f * gap;
        const AnimalClip& clip = entry.species->clips[entry.clip];
        const auto& info = library.entries[entry.index];

        auto entity = reg.create();
        // It starts on the right of its circle, heading away from the camera
        reg.emplace<Transform>(entity, glm::vec3(x + (path == AnimalWalker::Path::Straight ? 0.f : entry.radius), 0.f, origin.y));
        auto& animal = reg.emplace<Animal>(entity);
        animal.species = entry.index;
        animal.clip = entry.clip;
        auto& walker = reg.emplace<AnimalWalker>(entity);
        walker.path = path;
        walker.radius = entry.radius;
        walker.speed = speed * (clip.speed > 0.02f ? clip.speed : 0.6f * info.length);
        if(procedural) reg.emplace<AnimalGait>(entity).style = style;
        else animal.rate = speed;
        walkers.push_back(entity);

        dy::log(dy::info) << "AnimalWalkers : " << info.id << " plays " << clip.name << " at " << walker.speed << " m/s, radius " << entry.radius
            << " m, around " << x << " " << origin.y << "\n";
        x += entry.radius + 0.5f * gap;
    }

}

void AnimalWalkersSys::tick(double dt_seconds) {
    auto& library = animalLibrary(reg);
    const float dt = (float) std::min(dt_seconds, 0.1);
    for(auto [entity, transform, animal, walker] : reg.view<Transform, const Animal, AnimalWalker>().each()) {
        auto species = library.species(animal.species);
        if(!species) continue;
        const auto& info = library.entries[animal.species];
        bool grounded = std::abs(species->min.y) < 0.2f * (species->max.y - species->min.y);
        stepWalker(walker, transform, std::max(info.length, 0.1f) * transform.scale.y, grounded ? species->min.y * transform.scale.y : 0.f, ground.get(), dt);
    }

    // The camera round one of them, at the height of its back
    auto* camera = reg.ctx().find<Camera>();
    if(camera && follow >= 0 && follow < (int) walkers.size() && reg.valid(walkers[follow])) {
        const auto& transform = reg.get<Transform>(walkers[follow]);
        const auto& info = library.entries[reg.get<Animal>(walkers[follow]).species];
        const float size = std::max(info.length, info.height) * transform.scale.y;
        angle += glm::radians(orbit) * dt;
        glm::vec3 aim = transform.position + glm::vec3(0.f, 0.45f * info.height * transform.scale.y, 0.f);
        glm::vec3 eye = aim + glm::vec3(std::sin(angle), 0.f, std::cos(angle)) * (1.9f * size + 0.8f) + glm::vec3(0.f, 0.35f * size + 0.2f, 0.f);
        camera->position = eye;
        glm::vec3 to = aim - eye;
        camera->yaw = std::atan2(-to.x, -to.z);
        camera->pitch = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z));
    }
}
