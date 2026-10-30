#include "system_list.h"

#include <cmath>
#include <filesystem>
#include <memory>

#include "logic/components/model_renderable.h"
#include "logic/components/transform.h"
#include "logic/terrain/terrain_generator.h"

#include "renderer/model/model.h"
#include "renderer/model/model_manager.h"

#include "util/log.h"

using namespace dy;

void SceneSys::preinit() {

}

void SceneSys::init() {

    auto& models = reg.ctx().get<ModelManager>();
    auto& generator = *reg.ctx().get<std::shared_ptr<TerrainGenerator>>();

    // A ring of boxes around the spawn point
    entt::entity box = models.load("./resources/models/box.gltf");
    if(box != entt::null) {
        for(int i = 0; i < 12; i++) {
            float angle = i * 2.f * 3.14159f / 12.f;
            float size = 1.f + (i % 4);
            glm::vec3 position(std::cos(angle) * 25.f, 0.f, std::sin(angle) * 25.f);
            position.y = generator.surfaceHeight(position.x, position.z) + size * 0.5f;

            auto entity = reg.create();
            reg.emplace<Transform>(entity, position, glm::angleAxis(angle, glm::vec3(0.f, 1.f, 0.f)), glm::vec3(size));
            reg.emplace<ModelRenderable>(entity, box, glm::vec4(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle), 0.6f, 1.f));
        }
    }

    // Every other glTF dropped in resources/models, lined up behind the spawn point, about 4 units tall
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
        position.y = generator.surfaceHeight(position.x, position.z) - mesh.min.y * scale;

        auto entity = reg.create();
        reg.emplace<Transform>(entity, position, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(scale));
        reg.emplace<ModelRenderable>(entity, model);
        x += 8.f;
    }

}

void SceneSys::tick(double dt) {

}

void SceneSys::finish() {

}
