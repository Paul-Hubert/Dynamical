#include "logic/systems/system_list.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "renderer/context/context.h"

#include "logic/components/camerac.h"
#include "logic/components/inputc.h"
#include "logic/settings.h"

#include "logic/components/editor_contextc.h"

void EditorControlSys::preinit() {
    reg.set<EditorContextC>();
}

void EditorControlSys::init() {

}

void EditorControlSys::tick(float dt) {

    auto& s = reg.ctx<Settings>();

    if (s.spectator_mode != 3) return;

    EditorContextC& ctx = reg.ctx<EditorContextC>();

    InputC& input = reg.ctx<InputC>();
    CameraC& camera = reg.ctx<CameraC>();

    Context& context = *reg.ctx<Context*>();
    camera.projection = glm::perspective(90.f, (float)context.swap.extent.width / context.swap.extent.height, 0.1f, 100.f);

    camera.position = glm::vec3();

    camera.view = glm::translate(glm::mat4(1.f), camera.position);


}

void EditorControlSys::finish() {


}