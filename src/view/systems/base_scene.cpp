#include "system_list.h"

#include <cmath>
#include <filesystem>
#include <memory>

#include <imgui/imgui.h>

#include "view/spawn.h"
#include "logic/components/camera.h"
#include "view/components/input.h"
#include "view/components/model_renderable.h"
#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "logic/terrain/terrain_generator.h"

#include "renderer/model/model.h"
#include "renderer/model/model_manager.h"

#include "util/log.h"

using namespace dy;

// base: master's SceneSys split in three. Box heights come from the TerrainGenerator, which
// ChunkGeneratorSys creates in preinit

void BoxRingSys::preinit() {

}

void BoxRingSys::init() {

    auto& generator = *reg.ctx().get<std::shared_ptr<TerrainGenerator>>();

    // A ring of boxes dropped around the spawn point
    for(int i = 0; i < 12; i++) {
        float angle = i * 2.f * 3.14159f / 12.f;
        float size = 1.f + (i % 4);
        glm::vec3 position(std::cos(angle) * 25.f, 0.f, std::sin(angle) * 25.f);
        position.y = generator.surfaceHeight(position.x, position.z) + size + 4.f;

        auto entity = spawnBox(reg, position, size, glm::vec4(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle), 0.6f, 1.f));
        if(entity != entt::null) {
            reg.get<Transform>(entity).rotation = glm::angleAxis(angle, glm::normalize(glm::vec3(1.f, 1.f, 0.f)));
        }
    }

}

void BoxRingSys::tick(double dt) {

}

void BoxRingSys::finish() {

}

void ModelLineupSys::preinit() {

}

void ModelLineupSys::init() {

    auto& models = reg.ctx().get<ModelManager>();
    auto& generator = *reg.ctx().get<std::shared_ptr<TerrainGenerator>>();

    // Every other glTF dropped in resources/models (not its subfolders), lined up behind the spawn point,
    // about 4 units tall, with a box collider around its bounds
    std::error_code error;
    float x = -20.f;
    for(auto& file : std::filesystem::directory_iterator("./resources/models", error)) {
        auto extension = file.path().extension().string();
        if(extension != ".gltf" && extension != ".glb") continue;
        if(file.path().filename() == "box.gltf") continue;

        entt::entity model = models.load(file.path().string());
        if(model == entt::null) continue;

        auto& mesh = reg.get<ModelC>(model);
        float height = std::max(mesh.max.y - mesh.min.y, 0.001f);
        float scale = 4.f / height;

        glm::vec3 position(x, 0.f, 15.f);
        position.y = generator.surfaceHeight(position.x, position.z) - mesh.min.y * scale + 2.f;

        auto entity = reg.create();
        reg.emplace<Transform>(entity, position, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(scale));
        reg.emplace<ModelRenderable>(entity, model);
        auto& body = reg.emplace<RigidBody>(entity);
        body.half_extent = (mesh.max - mesh.min) * 0.5f * scale;
        body.offset = (mesh.max + mesh.min) * 0.5f * scale;
        x += 8.f;
    }

}

void ModelLineupSys::tick(double dt) {

}

void ModelLineupSys::finish() {

}

void BoxThrowerSys::preinit() {

}

void BoxThrowerSys::init() {

}

void BoxThrowerSys::tick(double dt) {

    // Left click throws a box from the desktop camera
    auto& input = reg.ctx().get<Input>();
    if(input.leftClick && !ImGui::GetIO().WantCaptureMouse) {
        auto& camera = reg.ctx().get<Camera>();
        glm::vec3 forward = camera.forward();
        static int count = 0;
        count++;
        glm::vec4 color(0.4f + 0.6f * ((count * 37) % 100) / 100.f, 0.4f + 0.6f * ((count * 61) % 100) / 100.f, 0.4f + 0.6f * ((count * 83) % 100) / 100.f, 1.f);
        spawnBox(reg, camera.position + forward * 3.f, 1.f, color, forward * 25.f);
    }

}

void BoxThrowerSys::finish() {

}
