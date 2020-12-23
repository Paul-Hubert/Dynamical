#include "editor_control.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "util/log.h"

#include "renderer/context/context.h"

#include "logic/components/camerac.h"
#include "logic/components/editorcontrolc.h"
#include "logic/components/inputc.h"
#include "logic/settings.h"


void EditorControlSys::preinit() {

}

void EditorControlSys::init() {

    Settings& settings = reg.ctx<Settings>();

    if(settings.spectator_mode == 3) {
        SDL_SetRelativeMouseMode(SDL_TRUE);

        Context& context = *reg.ctx<Context*>();
        CameraC& camera = reg.ctx<CameraC>();

        camera.projection = glm::perspective(90.f, (float)context.swap.extent.width / context.swap.extent.height, 0.1f, 100.f);
        camera.position = glm::vec3(0,0,0);
    }

}

void EditorControlSys::tick(float dt) {

    Settings& settings = reg.ctx<Settings>();

    if (settings.spectator_mode != 3) {
        reg.unset<Settings>();
        return;
    }

    Context& context = *reg.ctx<Context*>();
    EditorControlC& e = reg.ctx_or_set<EditorControlC>();
    InputC& input = reg.ctx<InputC>();
    CameraC& camera = reg.ctx<CameraC>();


    camera.pitch += input.mouseRelPos.y * settings.editor_sensitivity * glm::pi<float>() / context.win.getHeight();
    //Clamp
    if(camera.pitch > 0) {
        camera.pitch = 0;
    } else if(camera.pitch < -glm::pi<float>()) {
        camera.pitch = -glm::pi<float>();
    }


    camera.yaw -= input.mouseRelPos.x * settings.editor_sensitivity * glm::pi<float>() / context.win.getWidth();
    //Wrap
    if(camera.yaw > glm::pi<float>()*2) {
        camera.yaw -= glm::pi<float>()*2;
    }

    glm::mat4 rotation = glm::eulerAngleXYZ(camera.pitch, camera.yaw, camera.roll);

    glm::vec4 diff = glm::vec4(0,0,0,1);

    bool is_y = false;

    if(input.on[Action::FORWARD]) {
        diff.z = 1;
    } if(input.on[Action::BACKWARD]) {
        diff.z = -1;
    } if(input.on[Action::LEFT]) {
        diff.x = -1;
    } if(input.on[Action::RIGHT]) {
        diff.x = 1;
    } if(input.on[Action::UP]) {
        diff.y = 1;
        is_y = true;
    } if(input.on[Action::DOWN]) {
        diff.y = -1;
       is_y = true;
    }

    const float speed = 10;

    diff *= speed * dt;

    if(!is_y) {
        diff = rotation * diff;
    }

    camera.position.x += diff.x;
    camera.position.y += diff.y;
    camera.position.z += diff.z;

    glm::mat4 translation = glm::translate(glm::mat4(1.f), camera.position);
    camera.view = translation * rotation;
}

void EditorControlSys::finish() {


}