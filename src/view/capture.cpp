#include "capture.h"

#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <SDL.h>
#include <imgui/imgui.h>

#include "logic/components/camera.h"
#include "logic/terrain/chunk.h"
#include "scene/run_args.h"
#include "util/log.h"

using namespace dy;

namespace {

glm::vec3 vec3(const nlohmann::json& j) {
    return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
}

}

Capture::Capture(entt::registry& reg, const RunArgs& args) : reg(reg), directory(args.capture_dir) {

    std::ifstream file(args.capture);
    if(!file) {
        error = "cannot open capture script " + args.capture;
        return;
    }

    try {
        nlohmann::json script = nlohmann::json::parse(file);
        hide_ui = script.value("hide_ui", false);

        for(auto& j : script.value("shots", nlohmann::json::array())) {
            Shot shot;
            shot.frame = j.at("frame").get<uint64_t>();
            shot.name = j.at("name").get<std::string>();
            if(j.contains("camera")) {
                auto& c = j["camera"];
                if(c.contains("pos")) shot.pos = vec3(c["pos"]);
                if(c.contains("look_at")) shot.look_at = vec3(c["look_at"]);
                if(c.contains("offset")) shot.offset = vec3(c["offset"]);
                if(c.contains("yaw")) shot.yaw = c["yaw"].get<float>();
                if(c.contains("pitch")) shot.pitch = c["pitch"].get<float>();
            }
            if(!shots.empty() && shot.frame <= shots.back().frame) {
                error = "capture script " + args.capture + ": shot frames must increase (" + shot.name + ")";
                return;
            }
            shots.push_back(shot);
        }
        // A frame sequence for videos: <name>_00000.png, ... every `every` frames from `start`, the camera untouched
        if(script.contains("sequence")) {
            auto& q = script["sequence"];
            uint64_t start = q.at("start").get<uint64_t>(), count = q.at("count").get<uint64_t>(), every = q.value("every", (uint64_t) 1);
            std::string name = q.value("name", std::string("frame"));
            if(every == 0 || (!shots.empty() && start <= shots.back().frame)) {
                error = "capture script " + args.capture + ": the sequence must start after the shots, every >= 1";
                return;
            }
            for(uint64_t i = 0; i < count; i++) {
                char index[16];
                std::snprintf(index, sizeof(index), "_%05llu", (unsigned long long) i);
                Shot shot;
                shot.frame = start + i * every;
                shot.name = name + index;
                shots.push_back(shot);
            }
        }
        if(shots.empty()) {
            error = "capture script " + args.capture + ": no shots and no sequence";
            return;
        }
        if(script.contains("input")) {
            for(auto& j : script["input"]) {
                InputEvent event;
                event.frame = j.at("frame").get<uint64_t>();
                event.key = j.value("key", "");
                event.button = j.value("button", "");
                event.down = j.value("down", true);
                if(j.contains("mouse")) event.mouse = glm::vec2(j["mouse"].at(0).get<float>(), j["mouse"].at(1).get<float>());
                if(j.contains("motion")) event.motion = glm::ivec2(j["motion"].at(0).get<int>(), j["motion"].at(1).get<int>());
                if(!event.key.empty() && SDL_GetScancodeFromName(event.key.c_str()) == SDL_SCANCODE_UNKNOWN) {
                    error = "capture script " + args.capture + ": unknown key " + event.key;
                    return;
                }
                if(!event.button.empty() && event.button != "left" && event.button != "right" && event.button != "middle") {
                    error = "capture script " + args.capture + ": unknown mouse button " + event.button;
                    return;
                }
                inputs.push_back(event);
            }
        }
    } catch(const std::exception& e) {
        error = "capture script " + args.capture + ": " + e.what();
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

}

bool Capture::readScene(const std::string& path, std::string& scene, std::vector<std::pair<std::string, std::string>>& vars) {
    std::ifstream file(path);
    if(!file) return false;
    try {
        nlohmann::json script = nlohmann::json::parse(file);
        scene = script.value("scene", "base");
        if(script.contains("vars")) {
            for(auto& [name, value] : script["vars"].items()) vars.emplace_back(name, value.is_string() ? value.get<std::string>() : value.dump());
        }
        return true;
    } catch(const std::exception&) {
        return false;
    }
}

void Capture::sendInput(uint64_t frame) {

    for(auto& in : inputs) {
        if(in.frame != frame) continue;
        if(in.mouse) ImGui::GetIO().MousePos = ImVec2(in.mouse->x, in.mouse->y);
        SDL_Event e {};
        if(!in.key.empty()) {
            e.type = in.down ? SDL_KEYDOWN : SDL_KEYUP;
            e.key.state = in.down ? SDL_PRESSED : SDL_RELEASED;
            e.key.keysym.scancode = SDL_GetScancodeFromName(in.key.c_str());
            SDL_PushEvent(&e);
        }
        if(!in.button.empty()) {
            e = {};
            e.type = in.down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.state = in.down ? SDL_PRESSED : SDL_RELEASED;
            e.button.button = in.button == "left" ? SDL_BUTTON_LEFT : in.button == "right" ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE;
            if(in.mouse) {
                e.button.x = (int) in.mouse->x;
                e.button.y = (int) in.mouse->y;
            }
            SDL_PushEvent(&e);
        }
        if(in.motion) {
            e = {};
            e.type = SDL_MOUSEMOTION;
            e.motion.xrel = in.motion->x;
            e.motion.yrel = in.motion->y;
            SDL_PushEvent(&e);
        }
    }

}

void Capture::beginFrame(uint64_t frame) {

    sendInput(frame);
    if(hide_ui) ImGui::GetStyle().Alpha = 0.f;

    auto& camera = reg.ctx().get<Camera>();
    if(!have_spawn) {
        spawn_position = camera.position;
        spawn_yaw = camera.yaw;
        spawn_pitch = camera.pitch;
        have_spawn = true;
    }

    if(done()) return;
    const Shot& shot = shots[next];

    // A shot without a camera leaves the camera where it is (scripted input may move it)
    if(!shot.pos && !shot.offset && !shot.look_at && !shot.yaw && !shot.pitch) return;

    camera.position = shot.pos.value_or(spawn_position) + shot.offset.value_or(glm::vec3(0.f));
    camera.yaw = shot.yaw.value_or(spawn_yaw);
    camera.pitch = shot.pitch.value_or(spawn_pitch);
    if(shot.look_at) {
        glm::vec3 dir = glm::normalize(*shot.look_at - camera.position);
        camera.pitch = std::asin(glm::clamp(dir.y, -1.f, 1.f));
        camera.yaw = std::atan2(-dir.x, -dir.z);
    }

}

bool Capture::terrainReady() const {
    auto pending = reg.view<ChunkC>(entt::exclude<ChunkGenerated, ChunkDestroying>);
    auto meshing = reg.view<ChunkMeshPending>(entt::exclude<ChunkDestroying>);
    return pending.begin() == pending.end() && meshing.begin() == meshing.end();
}

bool Capture::due(uint64_t frame) {

    if(done() || failed() || frame < shots[next].frame) return false;

    if(terrainReady()) {
        if(frame > shots[next].frame) {
            dy::log(dy::warning) << "Capture " << shots[next].name << " : terrain ready at frame " << frame << " instead of " << shots[next].frame << "\n";
        }
        return true;
    }

    if(frame >= shots[next].frame + max_wait) {
        error = "capture " + shots[next].name + ": terrain still loading after " + std::to_string(max_wait) + " frames";
    }
    return false;

}

void Capture::save(const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height) {

    const Shot& shot = shots[next];
    std::string path = (std::filesystem::path(directory) / (shot.name + ".png")).string();
    if(!stbi_write_png(path.c_str(), (int) width, (int) height, 3, rgb.data(), (int) width * 3)) {
        error = "cannot write " + path;
        return;
    }
    dy::log(dy::info) << "Capture " << shot.name << " (" << width << "x" << height << ") -> " << path << "\n";
    next++;

}
