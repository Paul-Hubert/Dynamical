#include "capture.h"

#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <filesystem>
#include <fstream>

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

        std::string scene = script.value("scene", "base");
        if(scene != "base") {
            error = "capture script " + args.capture + ": scene '" + scene + "' does not exist (only base)";
            return;
        }

        for(auto& j : script.at("shots")) {
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
    } catch(const std::exception& e) {
        error = "capture script " + args.capture + ": " + e.what();
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

}

void Capture::beginFrame(uint64_t frame) {

    auto& camera = reg.ctx().get<Camera>();
    if(!have_spawn) {
        spawn_position = camera.position;
        spawn_yaw = camera.yaw;
        spawn_pitch = camera.pitch;
        have_spawn = true;
    }

    if(done()) return;
    const Shot& shot = shots[next];

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
