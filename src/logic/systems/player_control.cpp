#include "system_list.h"

#define _USE_MATH_DEFINES
#include <cmath>

#include "util/entt_util.h"

#include "logic/components/inputc.h"
#include "logic/components/camerac.h"
#include "logic/components/playerc.h"
#include "logic/components/positionc.h"

void PlayerControlSys::init() {
    
    auto player = reg.ctx<Util::Entity<"player"_hs>>();
    reg.assign<CameraC>(player);
    
}

void PlayerControlSys::tick() {
    
    auto player = reg.ctx<Util::Entity<"player"_hs>>();
    CameraC& camera = reg.get<CameraC>(player);
    auto& pos = reg.get<PositionC>(player).pos;
    
    const InputC& input = reg.ctx<InputC>();
    
    if(input.mouseFree) return;
    
    constexpr float base_speed = 60.f/60;
    
    if(input.on[Action::SPRINT]) {
        camera.sprinting = true;
    }
    
    float speed = camera.sprinting ? base_speed * 10. : base_speed;
    
    bool moving = false;
    if(input.on[Action::FORWARD]) {
        pos.x -= speed * std::sin(camera.yAxis);//*dt;
        pos.z -= speed * std::cos(camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::BACKWARD]) {
        pos.x += speed * std::sin(camera.yAxis);//*dt;
        pos.z += speed * std::cos(camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::LEFT]) {
        pos.x -= speed * std::sin(M_PI/2.0 + camera.yAxis);//*dt;
        pos.z -= speed * std::cos(M_PI/2.0 + camera.yAxis);//*dt;
        moving = true;
    } if(input.on[Action::RIGHT]) {
        pos.x += speed * std::sin(M_PI/2.0 + camera.yAxis);//*dt;
        pos.z += speed * std::cos(M_PI/2.0 + camera.yAxis);//*dt;
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
    
    camera.yAxis -= (input.mouseDiff.x) * (M_PI*0.1/180.);
    camera.xAxis = std::max(std::min(camera.xAxis - (input.mouseDiff.y) * (M_PI*0.1/180.), M_PI/2.), -M_PI/2.);
    
}
