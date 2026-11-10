#include "gait_view.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "logic/agents/gait.h"
#include "logic/agents/ground_field.h"
#include "logic/agents/humanbot.h"
#include "logic/agents/ik.h"
#include "logic/components/camera.h"
#include "logic/components/transform.h"
#include "renderer/model/model_manager.h"
#include "scene/world.h"
#include "view/components/model_renderable.h"

using namespace dy;

namespace {

const char* const box_model = "./resources/models/primitives/box.gltf";
const char* const sphere_model = "./resources/models/primitives/sphere.gltf";
const float deg = 180.f / 3.14159265358979f;
const glm::vec3 up(0.f, 1.f, 0.f);

// Rotation taking local z to dir
glm::quat along(glm::vec3 dir) {
    float length = glm::length(dir);
    if(length < 1e-6f) return glm::quat(1.f, 0.f, 0.f, 0.f);
    dir /= length;
    return glm::quatLookAt(dir, std::abs(dir.y) > 0.99f ? glm::vec3(1.f, 0.f, 0.f) : up);
}

const char* contactName(FootContact c) {
    switch(c) {
    case FootContact::Heel: return "heel";
    case FootContact::Flat: return "flat";
    case FootContact::Toe: return "toe";
    default: return "swing";
    }
}

}

GaitViewSys::GaitViewSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    world = params.value("world", world);
    distance = params.value("distance", distance);
    height = params.value("height", height);
    azimuth = params.value("azimuth", azimuth);
    camera = params.value("camera", camera);
    markers = params.value("markers", markers);
    panel = params.value("panel", panel);
}

void GaitViewSys::place(Shape shape, glm::vec3 position, glm::quat rotation, glm::vec3 scale, glm::vec4 color) {
    auto* models = reg.ctx().find<ModelManager>();
    if(!models) return;
    auto& list = shape == Shape::Box ? boxes : spheres;
    size_t& used = shape == Shape::Box ? used_boxes : used_spheres;
    if(used >= list.size()) {
        entt::entity e = reg.create();
        reg.emplace<Transform>(e);
        reg.emplace<ModelRenderable>(e, models->load(shape == Shape::Box ? box_model : sphere_model));
        list.push_back(e);
    }
    entt::entity e = list[used++];
    auto& t = reg.get<Transform>(e);
    t.position = position;
    t.rotation = rotation;
    t.scale = scale;
    reg.get<ModelRenderable>(e).tint = color;
}

void GaitViewSys::bar(glm::vec3 from, glm::vec3 to, float thickness, glm::vec4 color) {
    place(Shape::Box, 0.5f * (from + to), along(to - from), glm::vec3(thickness, thickness, glm::length(to - from)), color);
}

void GaitViewSys::tick(double dt) {

    used_boxes = used_spheres = 0;
    auto hide = [&]() {
        for(size_t i = used_boxes; i < boxes.size(); i++) reg.get<Transform>(boxes[i]).scale = glm::vec3(0.f);
        for(size_t i = used_spheres; i < spheres.size(); i++) reg.get<Transform>(spheres[i]).scale = glm::vec3(0.f);
    };

    auto* worlds = reg.ctx().find<Worlds>();
    if(!worlds || world >= (int) worlds->size()) { hide(); return; }
    auto& w = (*worlds)[world].reg;
    auto view = w.view<GaitState, GaitCommand>();
    if(view.begin() == view.end()) { hide(); return; }
    entt::entity e = *view.begin();
    auto& state = w.get<GaitState>(e);
    auto& command = w.get<GaitCommand>(e);
    const auto* balance = w.try_get<BalanceState>(e);
    const auto* report = w.try_get<GaitReport>(e);
    auto* params = w.try_get<GaitParams>(e);
    GroundField& ground = groundField(w);
    auto* plane = dynamic_cast<PlaneField*>(&ground);
    if(!state.started) { hide(); return; }

    glm::vec3 body = balance && balance->valid ? balance->com : state.centre + up;
    if(!has_focus) {
        focus = body;
        view_heading = state.heading;
        has_focus = true;
    }
    const float k = 1.f - std::exp(-(float) dt * 5.f);
    focus = glm::mix(focus, body, k);
    float turn = state.heading - view_heading;
    while(turn > 3.14159265f) turn -= 6.2831853f;
    while(turn < -3.14159265f) turn += 6.2831853f;
    view_heading += turn * (1.f - std::exp(-(float) dt * 1.5f));

    if(camera) {
        // From behind the walker, swung to its side by the azimuth; on a slope that would hide the feet, from the
        // other side, from higher or from behind: the first place that sees both feet, kept while it does
        struct Place { float side, turn, lift; };
        const Place places[] = { { 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f }, { 1.f, 0.f, 1.5f }, { -1.f, 0.f, 1.5f }, { 1.f, 60.f, 2.f }, { -1.f, 60.f, 2.f },
            { 1.f, 0.f, 3.5f }, { -1.f, 0.f, 3.5f }, { 1.f, 100.f, 4.f } };
        const int count = (int) (sizeof(places) / sizeof(places[0]));
        auto at = [&](const Place& place) {
            float a = view_heading + place.side * glm::radians(azimuth + place.turn);
            glm::vec3 e = focus + glm::vec3(std::sin(a), 0.f, std::cos(a)) * distance + up * (height - 1.f + place.lift);
            if(auto hit = ground.below(e + up * 50.f, 100.f)) e.y = std::max(e.y, hit->point.y + 0.4f);
            return e;
        };
        auto sees = [&](glm::vec3 from, glm::vec3 to) {
            for(int i = 1; i < 16; i++) {
                glm::vec3 point = glm::mix(from, to, i / 16.f);
                if(auto hit = ground.below(point + up * 30.f, 60.f)) {
                    if(hit->point.y > point.y - 0.03f) return false;
                }
            }
            return true;
        };
        auto clear = [&](const Place& place) {
            glm::vec3 e = at(place);
            if(!state.feet_ready) return true;
            for(const FootState& f : state.foot) if(!sees(e, f.position + up * 0.12f)) return false;
            return sees(e, focus);
        };
        if(!clear(places[std::clamp(view_choice, 0, count - 1)])) {
            view_choice = count - 1;
            for(int i = 0; i < count; i++) if(clear(places[i])) { view_choice = i; break; }
        } else if(view_choice != 0 && clear(places[0])) {
            view_choice = 0;
        }
        glm::vec3 wanted = at(places[view_choice]);
        if(glm::length(eye - focus) > 4.f * distance + 8.f) eye = wanted;      // the first frame, or a jump of the walker
        eye = glm::mix(eye, wanted, 1.f - std::exp(-(float) dt * 2.5f));
        glm::vec3 d = glm::normalize(focus - up * 0.25f - eye);
        auto& cam = reg.ctx().get<Camera>();
        cam.position = eye;
        cam.pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
        cam.yaw = std::atan2(-d.x, -d.z);
    }

    if(markers) {
        if(plane) {
            // The plane under the walker, with lines every metre fixed in the world
            glm::vec3 n = plane->normal();
            glm::quat tilt = glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::cross(n, glm::vec3(0.f, 0.f, 1.f))), n,
                glm::cross(glm::normalize(glm::cross(n, glm::vec3(0.f, 0.f, 1.f))), n))));
            auto on = [&](float x, float z) { return plane->below(glm::vec3(x, 1e4f, z), 1e5f)->point; };
            float cx = std::round(state.centre.x), cz = std::round(state.centre.z);
            const float half = 12.f;
            place(Shape::Box, on(cx, cz) - n * 0.011f, tilt, glm::vec3(2.f * half, 0.02f, 2.f * half), glm::vec4(0.46f, 0.5f, 0.44f, 1.f));
            for(int i = -(int) half; i <= (int) half; i++) {
                glm::vec4 color = (int) std::lround(cx + i) % 5 == 0 ? glm::vec4(0.2f, 0.22f, 0.2f, 1.f) : glm::vec4(0.36f, 0.4f, 0.35f, 1.f);
                bar(on(cx + i, cz - half), on(cx + i, cz + half), 0.012f, color);
                color = (int) std::lround(cz + i) % 5 == 0 ? glm::vec4(0.2f, 0.22f, 0.2f, 1.f) : glm::vec4(0.36f, 0.4f, 0.35f, 1.f);
                bar(on(cx - half, cz + i), on(cx + half, cz + i), 0.012f, color);
            }
        }

        if(state.feet_ready) {
            for(const FootState& f : state.foot) {
                const FootGeometry& g = f.geometry;
                glm::vec3 size(2.f * g.half_width, 0.004f, 2.f * g.half_length);
                glm::vec4 color = f.contact == FootContact::Heel ? glm::vec4(1.f, 0.55f, 0.1f, 1.f)
                    : f.contact == FootContact::Flat ? glm::vec4(0.2f, 0.85f, 0.3f, 1.f)
                    : f.contact == FootContact::Toe ? glm::vec4(0.2f, 0.5f, 1.f, 1.f) : glm::vec4(0.25f, 0.25f, 0.25f, 1.f);
                place(Shape::Box, f.current.centre + f.current.rotation * glm::vec3(0.f, 0.003f, 0.f), f.current.rotation, size, color);
                if(!f.stance && f.planned) {
                    place(Shape::Box, f.next.centre + f.next.rotation * glm::vec3(0.f, 0.003f, 0.f), f.next.rotation, size, glm::vec4(0.75f, 0.75f, 0.75f, 1.f));
                }
                if(auto* target = w.try_get<IKTarget>(f.target)) {
                    place(Shape::Sphere, target->position, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(0.05f), glm::vec4(1.f, 0.9f, 0.1f, 1.f));
                }
            }
        }

        if(balance && balance->valid) {
            glm::vec3 below(balance->com.x, balance->ground + 0.006f, balance->com.z);
            place(Shape::Sphere, balance->com, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(0.07f), glm::vec4(0.95f, 0.15f, 0.15f, 1.f));
            bar(balance->com, below, 0.008f, glm::vec4(0.95f, 0.15f, 0.15f, 1.f));
            place(Shape::Box, below, glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(0.06f, 0.004f, 0.06f), glm::vec4(0.95f, 0.15f, 0.15f, 1.f));
            float xcom_y = ground.height(balance->xcom.x, balance->xcom.z, balance->com.y, balance->ground);
            place(Shape::Sphere, glm::vec3(balance->xcom.x, xcom_y + 0.02f, balance->xcom.z), glm::quat(1.f, 0.f, 0.f, 0.f), glm::vec3(0.06f), glm::vec4(0.9f, 0.2f, 0.9f, 1.f));
            for(size_t i = 0; i < balance->support.size(); i++) {
                glm::vec2 a = balance->support[i], b = balance->support[(i + 1) % balance->support.size()];
                glm::vec3 from(a.x, ground.height(a.x, a.y, balance->com.y, balance->ground) + 0.008f, a.y);
                glm::vec3 to(b.x, ground.height(b.x, b.y, balance->com.y, balance->ground) + 0.008f, b.y);
                bar(from, to, 0.008f, glm::vec4(0.95f, 0.95f, 0.95f, 1.f));
            }
        }
    }
    hide();

    if(!panel) return;
    ImGui::Begin("Gait");
    ImGui::SliderFloat("speed (m/s)", &command.speed, 0.f, 2.2f, "%.2f");
    float heading = command.heading * deg;
    if(ImGui::SliderFloat("heading (deg)", &heading, -180.f, 180.f, "%.0f")) command.heading = heading / deg;
    if(plane) {
        float pitch = plane->pitch(), roll = plane->roll();
        bool changed = ImGui::SliderFloat("plane pitch (deg)", &pitch, -30.f, 30.f, "%.0f");
        changed |= ImGui::SliderFloat("plane roll (deg)", &roll, -30.f, 30.f, "%.0f");
        if(changed) plane->setTilt(pitch, roll);
    }
    ImGui::Checkbox("follow camera", &camera);
    ImGui::SameLine();
    ImGui::Checkbox("markers", &markers);
    if(camera) {
        ImGui::SliderFloat("camera side (deg)", &azimuth, -180.f, 180.f, "%.0f");
        ImGui::SliderFloat("camera distance", &distance, 1.5f, 12.f, "%.1f");
    }
    ImGui::Separator();
    ImGui::Text("walking %.2f m/s (slope asks %.2f), heading %.0f", state.speed, state.wanted_speed, state.heading * deg);
    ImGui::Text("step %.2f m, stride %.2f s, stance %.0f %%, phase %.2f", state.step_length, state.stride_time, state.stance_share * 100.f, state.phase);
    ImGui::Text("slope %.1f deg along, %.1f across; lean %.1f deg", state.slope_pitch * deg, state.slope_roll * deg, state.lean * deg);
    if(state.feet_ready) {
        ImGui::Text("left %s (sole %.0f deg), right %s (sole %.0f deg)", contactName(state.foot[0].contact), state.foot[0].pitch * deg,
            contactName(state.foot[1].contact), state.foot[1].pitch * deg);
    }
    if(balance && balance->valid) {
        ImGui::Text("hips %.0f mm under nominal", balance->crouch * 1e3f);
        ImGui::Text("centre of mass off target %.1f mm (%d passes)", balance->error * 1e3f, balance->iterations);
        ImGui::Text("extrapolated: %.0f mm ahead, lateral margin %.0f mm", balance->lead * 1e3f, balance->lateral_margin * 1e3f);
        ImGui::Text("last heel strike off the rule by %.0f mm", balance->capture_error * 1e3f);
    }
    if(report) {
        ImGui::Separator();
        ImGui::Text("now / worst since %.0f s", report->settle);
        ImGui::Text("sole to ground %.1f / %.1f mm", report->contact_error * 1e3f, report->max_contact_error * 1e3f);
        ImGui::Text("under the ground %.1f / %.1f mm", report->penetration * 1e3f, report->max_penetration * 1e3f);
        ImGui::Text("slip %.1f / %.1f mm", report->slip * 1e3f, report->max_slip * 1e3f);
        ImGui::Text("stance IK %.1f mm %.1f deg / %.1f mm %.1f deg", report->ik_position * 1e3f, report->ik_rotation * deg, report->max_ik_position * 1e3f, report->max_ik_rotation * deg);
        ImGui::Text("limits took or gave %.1f / %.1f deg", report->ik_limit * deg, report->max_ik_limit * deg);
        ImGui::Text("past the body file's limits %.1f / %.1f deg", report->body_limit_excess * deg, report->max_body_limit_excess * deg);
        ImGui::Text("steps %d", report->steps);
    }
    if(params) {
        ImGui::Separator();
        float strike = params->strike_angle * deg, toe = params->toe_angle * deg;
        if(ImGui::SliderFloat("heel strike (deg)", &strike, 0.f, 30.f, "%.0f")) params->strike_angle = strike / deg;
        if(ImGui::SliderFloat("toe-off (deg)", &toe, 0.f, 75.f, "%.0f")) params->toe_angle = toe / deg;
        ImGui::SliderFloat("step width (m)", &params->step_width, 0.05f, 0.3f, "%.2f");
        ImGui::SliderFloat("slope slowdown", &params->slope_slowdown, 0.f, 3.5f, "%.1f");
        ImGui::SliderFloat("lean per slope", &params->lean_slope_gain, 0.f, 1.f, "%.2f");
        ImGui::Checkbox("joint limits give way to a planted foot", &params->yield_limits);
    }
    ImGui::End();

}

void GaitViewSys::finish() {
    for(auto e : boxes) if(reg.valid(e)) reg.destroy(e);
    for(auto e : spheres) if(reg.valid(e)) reg.destroy(e);
    boxes.clear();
    spheres.clear();
}
