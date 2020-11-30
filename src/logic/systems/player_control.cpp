#include "system_list.h"

#define _USE_MATH_DEFINES
#include <cmath>

#include "util/entt_util.h"

#include "logic/components/inputc.h"
#include "logic/components/camerac.h"
#include "logic/components/playerc.h"

void PlayerControlSys::preinit() {

}

void PlayerControlSys::init() {
    
    auto player = reg.ctx<Util::Entity<"player"_hs>>();
    reg.emplace<CameraC>(player);
    
}

void PlayerControlSys::tick() {

    OPTICK_EVENT();
    
    /*
    auto player = reg.ctx<Util::Entity<"player"_hs>>();
    CameraC& camera = reg.get<CameraC>(player);
    auto& pos = reg.get<PositionC>(player).pos;
    
    const InputC& input = reg.ctx<InputC>();
    
    if(input.mouseFree) return;
    
    constexpr float base_speed = 60.f/60;
    
    if(input.on[Action::SPRINT]) {
        camera.sprinting = true;
    }
    
    float speed = (float) (camera.sprinting ? base_speed * 10. : base_speed);
    
    bool moving = false;
    if(input.on[Action::FORWARD]) {
        pos.x -= speed * (float) std::sin(camera.yAxis);//*dt;
        pos.z -= speed * (float) std::cos(camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::BACKWARD]) {
        pos.x += speed * (float) std::sin(camera.yAxis);//*dt;
        pos.z += speed * (float) std::cos(camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::LEFT]) {
        pos.x -= speed * (float) std::sin(M_PI/2.0 + camera.yAxis);//*dt;
        pos.z -= speed * (float) std::cos(M_PI/2.0 + camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::RIGHT]) {
        pos.x += speed * (float) std::sin(M_PI/2.0 + camera.yAxis);//*dt;
        pos.z += speed * (float) std::cos(M_PI/2.0 + camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::UP]) {
        pos.y += speed;
        moving = true;
    } if(input.on[Action::DOWN]) {
        pos.y -= speed;
        moving = true;
    }
    
    if(!moving) {
        camera.sprinting = false;
    }
    
    camera.yAxis -= (float) ((input.mouseDiff.x) * (M_PI*0.1/180.));
    camera.xAxis = (float) std::max(std::min(camera.xAxis - (input.mouseDiff.y) * (M_PI*0.1/180.), M_PI/2.), -M_PI/2.);
    
    */

}

void PlayerControlSys::finish() {

}