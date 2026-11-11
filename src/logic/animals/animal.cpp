#include "animal.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "logic/components/transform.h"
#include "util/log.h"

using namespace dy;

namespace {

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    if(!file) throw std::runtime_error("cannot open " + path.string());
    return nlohmann::json::parse(file);
}

std::string optionalString(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

glm::vec3 readVec3(const nlohmann::json& j) {
    return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
}

glm::vec3 readFloats3(const uint8_t* data) {
    glm::vec3 v;
    std::memcpy(&v, data, sizeof(v));
    return v;
}

glm::quat readQuat(const uint8_t* data) {
    int16_t q[4];
    std::memcpy(q, data, sizeof(q));
    return glm::quat((float) q[3], (float) q[0], (float) q[1], (float) q[2]);
}

}

glm::mat4 JointPose::matrix() const {
    glm::mat4 m = glm::mat4_cast(rotation);
    m[0] *= scale.x;
    m[1] *= scale.y;
    m[2] *= scale.z;
    m[3] = glm::vec4(translation, 1.f);
    return m;
}

JointPose dy::blend(const JointPose& a, const JointPose& b, float t) {
    JointPose out;
    out.translation = glm::mix(a.translation, b.translation, t);
    out.scale = glm::mix(a.scale, b.scale, t);
    // The short way round
    float sign = glm::dot(a.rotation, b.rotation) < 0.f ? -1.f : 1.f;
    out.rotation = glm::normalize(a.rotation * (1.f - t) + b.rotation * (sign * t));
    return out;
}



int AnimalSpecies::findClip(const std::string& name) const {
    for(size_t i = 0; i < clips.size(); i++) {
        if(clips[i].name == name) return (int) i;
    }
    return -1;
}

int AnimalSpecies::findJoint(const std::string& name) const {
    for(size_t i = 0; i < joint_names.size(); i++) {
        if(joint_names[i] == name) return (int) i;
    }
    return -1;
}

void AnimalSpecies::sample(int index, float time, bool in_place, std::vector<JointPose>& pose) const {

    if(index < 0 || index >= (int) clips.size()) {
        pose = rest;
        return;
    }

    const AnimalClip& clip = clips[index];
    const float duration = clip.duration();
    if(duration <= 0.f) time = 0.f;
    else if(clip.loop) time -= std::floor(time / duration) * duration;
    else time = std::clamp(time, 0.f, duration);

    const float at = time * clip.fps;
    const uint32_t f0 = std::min((uint32_t) at, clip.frames - 1);
    const uint32_t f1 = std::min(f0 + 1, clip.frames - 1);
    const float t = at - (float) f0;

    pose.resize(clip.tracks.size());
    const uint8_t* data = clip_data.data();
    for(size_t j = 0; j < clip.tracks.size(); j++) {
        const AnimalClip::Track& track = clip.tracks[j];
        JointPose& out = pose[j];

        if(track.animated & 1) out.translation = glm::mix(readFloats3(data + track.translation + 12 * f0), readFloats3(data + track.translation + 12 * f1), t);
        else out.translation = readFloats3(data + track.translation);

        if(track.animated & 2) {
            glm::quat a = readQuat(data + track.rotation + 8 * f0), b = readQuat(data + track.rotation + 8 * f1);
            out.rotation = glm::normalize(a * (1.f - t) + b * (glm::dot(a, b) < 0.f ? -t : t));
        } else {
            out.rotation = glm::normalize(readQuat(data + track.rotation));
        }

        if(track.animated & 4) out.scale = glm::mix(readFloats3(data + track.scale + 12 * f0), readFloats3(data + track.scale + 12 * f1), t);
        else out.scale = readFloats3(data + track.scale);
    }

    if(in_place && clip.motion_joint >= 0 && duration > 0.f) {
        pose[clip.motion_joint].translation -= clip.root_motion * (time / duration);
    }

}

void AnimalSpecies::worldMatrices(const std::vector<JointPose>& pose, std::vector<glm::mat4>& world) const {
    world.resize(parents.size());
    for(size_t i = 0; i < parents.size(); i++) {
        glm::mat4 local = pose[i].matrix();
        world[i] = parents[i] < 0 ? local : world[parents[i]] * local;
    }
}

void AnimalSpecies::skinMatrices(const std::vector<glm::mat4>& world, std::vector<glm::mat4>& skin) const {
    skin.resize(world.size());
    for(size_t i = 0; i < world.size(); i++) {
        skin[i] = world[i] * inverse_bind[i];
    }
}



std::filesystem::path dy::animalsDirectory() {
    std::filesystem::path local = "./resources/animals";
    std::error_code error;
    if(std::filesystem::exists(local / "library.json", error)) return local;
#ifdef DY_SOURCE_RESOURCES
    return std::filesystem::path(DY_SOURCE_RESOURCES) / "animals";
#else
    return local;
#endif
}

AnimalLibrary& dy::animalLibrary(entt::registry& reg) {
    if(auto* library = reg.ctx().find<AnimalLibrary>()) return *library;
    auto& library = reg.ctx().emplace<AnimalLibrary>();
    auto directory = animalsDirectory();
    if(library.load(directory)) {
        dy::log(dy::info) << "Animal library : " << library.entries.size() << " animals in " << directory.string() << "\n";
    } else {
        dy::log(dy::warning) << "No animal library in " << directory.string() << " (tools/animal_import.py builds it)\n";
    }
    return library;
}

bool AnimalLibrary::load(const std::filesystem::path& dir) {

    directory = dir;
    entries.clear();
    loaded.clear();
    failed.clear();

    try {
        auto j = readJson(dir / "library.json");
        for(auto& s : j.at("species")) {
            Entry entry;
            entry.id = s.at("id").get<std::string>();
            entry.name = s.value("name", entry.id);
            entry.height = s.value("height", 0.f);
            entry.length = s.value("length", 0.f);
            entry.width = s.value("width", 0.f);
            entry.joints = s.value("joints", 0u);
            entry.triangles = s.value("triangles", 0u);
            entry.lods = s.value("lods", 1u);
            entry.clips = s.value("clips", 0u);
            entry.walk = optionalString(s, "walk");
            entry.walk_speed = s.value("walk_speed", 0.f);
            entries.push_back(std::move(entry));
        }
    } catch(const std::exception&) {
        entries.clear();
        return false;
    }

    loaded.resize(entries.size());
    failed.assign(entries.size(), false);
    return true;

}

int AnimalLibrary::find(const std::string& id) const {
    for(size_t i = 0; i < entries.size(); i++) {
        if(entries[i].id == id) return (int) i;
    }
    return -1;
}

std::shared_ptr<const AnimalSpecies> AnimalLibrary::species(uint32_t index) {
    if(index >= entries.size()) return nullptr;
    if(loaded.size() != entries.size()) {
        // Entries filled by hand (tests) : nothing on disk is promised
        loaded.resize(entries.size());
        failed.resize(entries.size(), false);
    }
    if(loaded[index] || failed[index]) return loaded[index];
    try {
        loaded[index] = loadSpecies(directory, entries[index].id);
    } catch(const std::exception& e) {
        failed[index] = true;
        dy::log(dy::error) << "Animals : cannot load " << entries[index].id << " : " << e.what() << "\n";
    }
    return loaded[index];
}

std::shared_ptr<AnimalSpecies> AnimalLibrary::loadSpecies(const std::filesystem::path& directory, const std::string& id) {

    static_assert(sizeof(AnimalVertex) == 36, "species/<id>.bin stores 36 byte vertices");

    auto j = readJson(directory / "species" / (id + ".json"));

    auto species = std::make_shared<AnimalSpecies>();
    AnimalSpecies& s = *species;
    s.id = id;
    s.min = readVec3(j.at("bounds").at("min"));
    s.max = readVec3(j.at("bounds").at("max"));
    s.vertex_count = j.at("vertex_count").get<uint32_t>();
    s.index_count = j.at("index_count").get<uint32_t>();

    for(auto& joint : j.at("joints")) {
        int32_t parent = joint.at("parent").get<int32_t>();
        if(parent >= (int32_t) s.parents.size()) throw std::runtime_error("joint before its parent in " + id);
        s.joint_names.push_back(joint.value("name", std::string()));
        s.joint_skin.push_back(joint.value("skin", 1.f));
        s.parents.push_back(parent);
    }
    const size_t joints = s.parents.size();
    if(joints == 0) throw std::runtime_error("no joint in " + id);
    if(joints > 65535) throw std::runtime_error("too many joints in " + id);

    for(auto& m : j.at("materials")) {
        AnimalMaterial material;
        material.name = m.value("name", std::string());
        material.color = optionalString(m, "color");
        material.normal = optionalString(m, "normal");
        if(auto it = m.find("tint"); it != m.end() && it->size() == 4) {
            material.tint = glm::vec4(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>(), it->at(3).get<float>());
        }
        material.alpha_cutoff = m.value("alpha_cutoff", 0.f);
        material.two_sided = m.value("two_sided", false);
        s.materials.push_back(std::move(material));
    }

    for(auto& l : j.at("lods")) {
        AnimalLod lod;
        lod.screen_height = l.value("screen_height", 0.f);
        lod.triangles = l.value("triangles", 0u);
        for(auto& d : l.at("draws")) {
            AnimalLod::Draw draw;
            draw.material = d.at("material").get<uint32_t>();
            draw.first_index = d.at("first_index").get<uint32_t>();
            draw.index_count = d.at("index_count").get<uint32_t>();
            if(draw.material >= s.materials.size() || (uint64_t) draw.first_index + draw.index_count > s.index_count) {
                throw std::runtime_error("draw call out of range in " + id);
            }
            lod.draws.push_back(draw);
        }
        s.lods.push_back(std::move(lod));
    }
    if(s.lods.empty()) throw std::runtime_error("no level of detail in " + id);

    // The skeleton, after the vertices and the indices
    {
        auto path = directory / "species" / (id + ".bin");
        std::ifstream file(path, std::ios::binary);
        if(!file) throw std::runtime_error("cannot open " + path.string());
        file.seekg((std::streamoff) ((uint64_t) s.vertex_count * sizeof(AnimalVertex) + (uint64_t) s.index_count * sizeof(uint32_t)));
        std::vector<float> values(joints * 22);
        file.read(reinterpret_cast<char*>(values.data()), (std::streamsize) (values.size() * sizeof(float)));
        if(!file) throw std::runtime_error(path.string() + " is shorter than its description");

        s.inverse_bind.resize(joints);
        s.rest.resize(joints);
        for(size_t i = 0; i < joints; i++) {
            const float* m = &values[i * 12];   // 3 rows of 4
            s.inverse_bind[i] = glm::mat4(m[0], m[4], m[8], 0.f, m[1], m[5], m[9], 0.f, m[2], m[6], m[10], 0.f, m[3], m[7], m[11], 1.f);
            const float* r = &values[joints * 12 + i * 10];
            s.rest[i].translation = glm::vec3(r[0], r[1], r[2]);
            s.rest[i].rotation = glm::normalize(glm::quat(r[6], r[3], r[4], r[5]));
            s.rest[i].scale = glm::vec3(r[7], r[8], r[9]);
        }
    }

    // Clips
    {
        auto path = directory / "species" / (id + ".anim");
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if(file) {
            s.clip_data.resize((size_t) file.tellg());
            file.seekg(0);
            file.read(reinterpret_cast<char*>(s.clip_data.data()), (std::streamsize) s.clip_data.size());
        }
    }
    const size_t flag_bytes = (joints + 3) / 4 * 4;
    for(auto& c : j.at("clips")) {
        AnimalClip clip;
        clip.name = c.at("name").get<std::string>();
        clip.kind = c.value("kind", std::string("other"));
        clip.frames = c.at("frames").get<uint32_t>();
        clip.fps = c.value("fps", 30.f);
        clip.loop = c.value("loop", false);
        // A gait is a cycle whatever its file says (seven of the library's are not marked): played as one it would
        // hold its last pose, and the procedural walk would bend a frozen body
        if(clip.kind == "walk" || clip.kind == "trot" || clip.kind == "run") clip.loop = true;
        clip.speed = c.value("speed", 0.f);
        clip.motion_joint = c.value("motion_joint", -1);
        if(auto it = c.find("root_motion"); it != c.end()) clip.root_motion = readVec3(*it);
        if(auto it = c.find("travel"); it != c.end()) clip.travel = readVec3(*it);
        if(clip.frames == 0 || clip.fps <= 0.f || clip.motion_joint >= (int) joints) throw std::runtime_error("malformed clip " + clip.name + " in " + id);

        const uint64_t start = c.at("offset").get<uint64_t>(), size = c.at("size").get<uint64_t>();
        if(start + size > s.clip_data.size() || size < flag_bytes) throw std::runtime_error("clip " + clip.name + " out of " + id + ".anim");
        uint64_t at = start + flag_bytes;
        clip.tracks.resize(joints);
        for(size_t i = 0; i < joints; i++) {
            AnimalClip::Track& track = clip.tracks[i];
            track.animated = s.clip_data[start + i];
            track.translation = (uint32_t) at;
            at += 12ull * ((track.animated & 1) ? clip.frames : 1);
            track.rotation = (uint32_t) at;
            at += 8ull * ((track.animated & 2) ? clip.frames : 1);
            track.scale = (uint32_t) at;
            at += 12ull * ((track.animated & 4) ? clip.frames : 1);
        }
        if(at != start + size) throw std::runtime_error("clip " + clip.name + " of " + id + " does not have the size of its keys");
        s.clips.push_back(std::move(clip));
    }

    if(auto roles = j.find("roles"); roles != j.end()) {
        auto role = [&](const char* kind) { return s.findClip(optionalString(*roles, kind)); };
        s.idle = role("idle");
        s.walk = role("walk");
        s.trot = role("trot");
        s.run = role("run");
        s.eat = role("eat");
        s.rest_clip = role("rest");
    }

    return species;

}

std::vector<AnimalVertex> AnimalLibrary::loadGeometry(const std::filesystem::path& directory, const AnimalSpecies& species, std::vector<uint32_t>& indices) {

    auto path = directory / "species" / (species.id + ".bin");
    std::ifstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("cannot open " + path.string());

    std::vector<AnimalVertex> vertices(species.vertex_count);
    indices.resize(species.index_count);
    file.read(reinterpret_cast<char*>(vertices.data()), (std::streamsize) (vertices.size() * sizeof(AnimalVertex)));
    file.read(reinterpret_cast<char*>(indices.data()), (std::streamsize) (indices.size() * sizeof(uint32_t)));
    if(!file) throw std::runtime_error(path.string() + " is shorter than its description");

    for(uint32_t index : indices) {
        if(index >= species.vertex_count) throw std::runtime_error("index out of range in " + path.string());
    }
    const size_t joints = species.parents.size();
    for(auto& vertex : vertices) {
        for(int k = 0; k < 4; k++) {
            if(vertex.joints[k] >= joints) throw std::runtime_error("joint out of range in " + path.string());
        }
    }

    return vertices;

}



void Animal::play(int next, float seconds) {
    if(next == clip) return;
    previous = clip;
    previous_time = time;
    previous_rate = rate;
    fade = 0.f;
    fade_duration = seconds;
    clip = next;
    time = 0.f;
}

void dy::evaluate(const AnimalSpecies& species, const Animal& animal, AnimalPose& pose, std::vector<JointPose>& scratch) {

    species.sample(animal.clip, animal.time, animal.in_place, pose.local);

    if(animal.fade < animal.fade_duration) {
        species.sample(animal.previous, animal.previous_time, animal.in_place, scratch);
        float t = animal.fade / animal.fade_duration;
        t = t * t * (3.f - 2.f * t);
        for(size_t i = 0; i < pose.local.size(); i++) {
            pose.local[i] = blend(scratch[i], pose.local[i], t);
        }
    }

    species.worldMatrices(pose.local, pose.world);
    species.skinMatrices(pose.world, pose.skin);

}

void AnimalAnimationSys::preinit() {
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    animalLibrary(reg);
}

void AnimalAnimationSys::tick(double dt) {

    OPTICK_EVENT();

    auto& library = animalLibrary(reg);

    struct Item {
        Animal* animal;
        AnimalPose* pose;
        const AnimalSpecies* species;
    };
    std::vector<Item> items;

    for(auto [entity, animal] : reg.view<Animal>().each()) {
        auto species = library.species(animal.species);
        if(!species) continue;
        if(animal.clip >= (int) species->clips.size()) animal.clip = -1;

        animal.time += (float) dt * animal.rate;
        if(animal.fade < animal.fade_duration) {
            animal.fade += (float) dt;
            animal.previous_time += (float) dt * animal.previous_rate;
        }

        auto* pose = reg.try_get<AnimalPose>(entity);
        if(!pose) pose = &reg.emplace<AnimalPose>(entity);
        items.push_back({ &animal, pose, species.get() });
    }

    auto work = [&](size_t first, size_t last) {
        std::vector<JointPose> scratch;
        for(size_t i = first; i < last; i++) {
            evaluate(*items[i].species, *items[i].animal, *items[i].pose, scratch);
        }
    };

    auto* executor = reg.ctx().find<tf::Executor>();
    const size_t batch = 16;
    if(executor && items.size() > 2 * batch) {
        tf::Taskflow taskflow;
        for(size_t first = 0; first < items.size(); first += batch) {
            taskflow.emplace([&, first]() { work(first, std::min(first + batch, items.size())); });
        }
        executor->run(taskflow).wait();
    } else {
        work(0, items.size());
    }

}
