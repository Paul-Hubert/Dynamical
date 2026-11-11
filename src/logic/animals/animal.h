#ifndef DY_ANIMAL_H
#define DY_ANIMAL_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "logic/systems/system.h"

namespace dy {

// 36 bytes, the layout of species/<id>.bin
struct AnimalVertex {
    glm::vec3 position;
    int8_t normal[4];
    glm::vec2 uv;
    uint16_t joints[4];
    uint8_t weights[4];     // sum to 255
};

// A joint relative to its parent
struct JointPose {
    glm::vec3 translation = glm::vec3(0.f);
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::vec3 scale = glm::vec3(1.f);

    glm::mat4 matrix() const;
};

JointPose blend(const JointPose& a, const JointPose& b, float t);

struct AnimalClip {
    std::string name;
    std::string kind;               // idle, walk, trot, run, eat, rest, other : from the name
    uint32_t frames = 0;
    float fps = 30.f;
    bool loop = false;
    float speed = 0.f;              // m/s the animal covers : the travel of its root, or how fast its planted feet slide
    int motion_joint = -1;          // the joint that carries the animal over the clip, -1 : the clip stays in place
    glm::vec3 root_motion = glm::vec3(0.f);     // what that joint travels, in its parent
    glm::vec3 travel = glm::vec3(0.f);          // the same in model space

    // Where each joint's keys are in AnimalSpecies::clip_data. One key when the channel is constant
    struct Track {
        uint32_t translation = 0;   // 3 floats a key
        uint32_t rotation = 0;      // 4 int16 a key : x y z w, 32767 is 1
        uint32_t scale = 0;         // 3 floats a key
        uint8_t animated = 0;       // bit 0 : translation, 1 : rotation, 2 : scale
    };
    std::vector<Track> tracks;

    float duration() const { return frames > 1 ? (float) (frames - 1) / fps : 0.f; }
};

struct AnimalMaterial {
    std::string name;
    std::string color;              // paths relative to the library folder, empty without the map
    std::string normal;
    glm::vec4 tint = glm::vec4(1.f);
    float alpha_cutoff = 0.f;       // > 0 : alpha tested (feathers, whiskers, fur cards)
    bool two_sided = false;
};

struct AnimalLod {
    struct Draw {
        uint32_t material = 0;
        uint32_t first_index = 0;
        uint32_t index_count = 0;
    };
    float screen_height = 0.f;      // drawn while the animal is taller than this fraction of the screen
    uint32_t triangles = 0;
    std::vector<Draw> draws;
};

// species/<id>.json, the skeleton of species/<id>.bin and the clips of species/<id>.anim. Model space : meters,
// Y up, the animal looks down -Z with its feet around y = 0
struct AnimalSpecies {
    std::string id;
    glm::vec3 min = glm::vec3(0.f);     // as it stands in its idle
    glm::vec3 max = glm::vec3(0.f);
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;

    std::vector<std::string> joint_names;
    std::vector<int32_t> parents;       // parents come first, -1 : a root
    std::vector<float> joint_skin;      // part of the vertices each joint moves : 0 for the controls of a rig and for ends
    std::vector<glm::mat4> inverse_bind;
    std::vector<JointPose> rest;        // the bind pose

    std::vector<AnimalMaterial> materials;
    std::vector<AnimalLod> lods;
    std::vector<AnimalClip> clips;
    std::vector<uint8_t> clip_data;

    // The plain clip of each kind, -1 without one
    int idle = -1, walk = -1, trot = -1, run = -1, eat = -1, rest_clip = -1;

    int findClip(const std::string& name) const;
    int findJoint(const std::string& name) const;

    // The joints at `time` seconds of a clip (wrapped when it loops, held at its ends else).
    // in_place : the travel of the clip's motion joint is taken out, whoever plays the clip moves the animal
    void sample(int clip, float time, bool in_place, std::vector<JointPose>& pose) const;

    // Model space matrices of the joints
    void worldMatrices(const std::vector<JointPose>& pose, std::vector<glm::mat4>& world) const;
    // What takes a vertex of the bind pose to the pose : world * inverse bind
    void skinMatrices(const std::vector<glm::mat4>& world, std::vector<glm::mat4>& skin) const;
};

// Context variable : the index of resources/animals (tools/animal_import.py, docs/animals.md)
class AnimalLibrary {
public:
    struct Entry {
        std::string id;
        std::string name;
        float height = 0.f;         // as it stands
        float length = 0.f;
        float width = 0.f;
        uint32_t joints = 0;
        uint32_t triangles = 0;     // of the most detailed level
        uint32_t lods = 1;
        uint32_t clips = 0;
        std::string walk;           // name of the walk clip, empty without one
        float walk_speed = 0.f;
    };

    // False when the folder has no library.json
    bool load(const std::filesystem::path& directory);

    int find(const std::string& id) const;

    // Loaded on first use (main thread); null when its files are missing or malformed, logged once
    std::shared_ptr<const AnimalSpecies> species(uint32_t index);

    // Throw std::runtime_error when the files are missing or malformed. No shared state : worker threads can call them
    static std::shared_ptr<AnimalSpecies> loadSpecies(const std::filesystem::path& directory, const std::string& id);
    static std::vector<AnimalVertex> loadGeometry(const std::filesystem::path& directory, const AnimalSpecies& species, std::vector<uint32_t>& indices);

    std::filesystem::path directory;
    std::vector<Entry> entries;

private:
    std::vector<std::shared_ptr<const AnimalSpecies>> loaded;
    std::vector<bool> failed;
};

// ./resources/animals next to the working directory, else the source tree's (the library is generated and large)
std::filesystem::path animalsDirectory();

// The context's library, loaded from animalsDirectory() on first use (empty when there is none)
AnimalLibrary& animalLibrary(entt::registry& reg);

// An animal of the library, next to a Transform (uniform scale). AnimalAnimationSys plays its clip, AnimalPass draws it
struct Animal {
    uint32_t species = 0;           // index in AnimalLibrary::entries
    int clip = -1;                  // index in the species' clips, -1 : the bind pose
    float time = 0.f;               // seconds in the clip
    float rate = 1.f;               // playback speed
    bool in_place = true;           // the clip does not move the animal : its Transform does

    // The clip it comes from, while the two are blended
    int previous = -1;
    float previous_time = 0.f;
    float previous_rate = 1.f;
    float fade = 0.f;               // seconds since the change
    float fade_duration = 0.f;

    // Starts `next` (when it is not already playing), blending from the current pose over `seconds`
    void play(int next, float seconds = 0.3f);
};

// Written by AnimalAnimationSys : the skinning matrices of the animal's joints in model space, and the joints
// themselves for whoever attaches something to them
struct AnimalPose {
    std::vector<JointPose> local;
    std::vector<glm::mat4> world;
    std::vector<glm::mat4> skin;
};

// Context variable written by AnimalPass, read by the debug menu and by the captures
struct AnimalStats {
    uint32_t animals = 0;           // Animal entities
    uint32_t drawn = 0;             // drawn on the desktop target
    uint32_t triangles = 0;
    uint32_t joints = 0;            // skinning matrices sent this frame
    uint32_t species_loaded = 0;
    uint32_t species_loading = 0;   // asked for and not drawn yet : animals are missing
};

// Advances the clips of every Animal and writes its AnimalPose, on the taskflow executor when there is one
class AnimalAnimationSys : public System {
public:
    AnimalAnimationSys(entt::registry& reg) : System(reg) {}
    const char* name() override { return "AnimalAnimation"; }
    void preinit() override;
    void tick(double dt) override;
};

// The pose of an animal now : its clip, blended with the one it comes from
void evaluate(const AnimalSpecies& species, const Animal& animal, AnimalPose& pose, std::vector<JointPose>& scratch);

void registerAnimals();

}

#endif
