#include "system_list.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "renderer/context/context.h"

#include "logic/components/camerac.h"
#include "logic/components/inputc.h"
#include "logic/settings.h"

void EditorControlSys::preinit() {

}

void EditorControlSys::init() {

}

void EditorControlSys::tick(float dt) {

    auto& s = reg.ctx<Settings>();

    if (s.spectator_mode == 3) {

        InputC& input = reg.ctx<InputC>();
        Context& ctx = *reg.ctx<Context*>();
        CameraC& camera = reg.ctx<CameraC>();

        camera.projection = glm::perspective(90.f, (float)ctx.swap.extent.width / ctx.swap.extent.height, 0.1f, 100.f);

        camera.position = glm::vec3();

        camera.view = glm::translate(glm::mat4(1.f), camera.position);

    }

}

void EditorControlSys::finish() {


}