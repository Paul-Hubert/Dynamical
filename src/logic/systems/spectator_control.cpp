#include "system_list.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/quaternion.hpp>

#include "renderer/context/context.h"

#include "logic/components/camerac.h"
#include "logic/components/vrinputc.h"
#include "logic/settings.h"

void SpectatorControlSys::preinit() {
	reg.set<CameraC>();
}

void SpectatorControlSys::init() {

}

void SpectatorControlSys::tick(float dt) {

    auto& s = reg.ctx<Settings>();

    if (s.spectator_mode == 2) {

        VRInputC& vr_input = reg.ctx<VRInputC>();
        Context& ctx = *reg.ctx<Context*>();
        CameraC& camera = reg.ctx<CameraC>();

        auto& v = vr_input.views[0];

        camera.projection = glm::perspective(90.f, (float) ctx.swap.extent.width / ctx.swap.extent.height, 0.01f, 100.f);

        glm::quat quat = glm::quat(v.pose.orientation.w * -1.0f, v.pose.orientation.x,
            v.pose.orientation.y * -1.0f, v.pose.orientation.z);
        glm::mat4 rotation = glm::mat4_cast(quat);
        glm::extractEulerAngleZXY(rotation, camera.yaw, camera.pitch, camera.roll);

        camera.position =
            glm::vec3(v.pose.position.x, -v.pose.position.y, v.pose.position.z);

        glm::mat4 translation = glm::translate(glm::mat4(1.f), camera.position);
        camera.view = translation * rotation;

    }

}

void SpectatorControlSys::finish() {


}