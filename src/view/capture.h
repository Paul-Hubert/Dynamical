#ifndef DY_CAPTURE_H
#define DY_CAPTURE_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dy {

struct RunArgs;

// Runs a capture script (capture=<script.json>): moves the desktop camera to each shot and writes the frame
// to <capture_dir>/<name>.png. A shot waits until the terrain is generated and meshed.
//
// { "scene": "base", "shots": [ { "frame": 600, "name": "base_spawn",
//                                 "camera": { "pos": [x, y, z], "look_at": [x, y, z] } } ] }
// camera (optional, else the spawn camera): pos with look_at or yaw / pitch (radians), or offset from the spawn
// camera position. The camera of a shot is held from the frame after the previous shot.
//
// "input": [ { "frame": 610, "key": "W", "down": true }, { "frame": 700, "button": "left", "down": true, "mouse": [x, y] } ]
// Scripted input goes through SDL's event queue, so InputSys and the systems after it see it as real input.
// "mouse" places ImGui's cursor (the hidden window never has the OS cursor); the OS cursor is not touched.
class Capture {
public:
    Capture(entt::registry& reg, const RunArgs& args);

    bool failed() const { return !error.empty(); }
    bool done() const { return next >= shots.size(); }

    // Before the systems tick
    void beginFrame(uint64_t frame);
    // After the systems tick: the next shot is due and the terrain is ready
    bool due(uint64_t frame);
    void save(const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height);

    std::string error;

    // Shots wait at most this many frames for the terrain
    static constexpr uint64_t max_wait = 1200;

private:
    struct InputEvent {
        uint64_t frame = 0;
        std::string key;            // SDL scancode name
        std::string button;         // left, right, middle
        bool down = true;
        std::optional<glm::vec2> mouse;
    };

    void sendInput(uint64_t frame);

    struct Shot {
        uint64_t frame = 0;
        std::string name;
        std::optional<glm::vec3> pos, look_at, offset;
        std::optional<float> yaw, pitch;
    };

    bool terrainReady() const;

    entt::registry& reg;
    std::string directory;
    std::vector<Shot> shots;
    std::vector<InputEvent> inputs;
    size_t next = 0;

    bool have_spawn = false;
    glm::vec3 spawn_position;
    float spawn_yaw = 0.f, spawn_pitch = 0.f;
};

}

#endif
