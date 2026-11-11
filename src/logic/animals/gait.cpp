#include "gait.h"

#include <algorithm>
#include <cmath>

#include "logic/agents/ground_field.h"
#include "logic/components/transform.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "util/log.h"

using namespace dy;

namespace {

const float pi = 3.14159265f;

float wrap01(float x) {
    return x - std::floor(x);
}

float wrapAngle(float angle) {
    while(angle > pi) angle -= 2.f * pi;
    while(angle < -pi) angle += 2.f * pi;
    return angle;
}

// Is `phase` in the part of the cycle that goes from `from` to `to`
bool inCycle(float phase, float from, float to) {
    return wrap01(phase - from) < wrap01(to - from);
}

float smooth(float a, float b, float x) {
    if(b <= a) return x < a ? 0.f : 1.f;
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

glm::mat4 turnY(float angle) {
    return glm::rotate(glm::mat4(1.f), angle, glm::vec3(0.f, 1.f, 0.f));
}

// A point `a` metres along an arc of curvature k that starts at the origin toward -z, and the heading there
void arc(float k, float a, glm::vec3& point, float& yaw) {
    yaw = k * a;
    if(std::abs(k) < 1e-5f) point = glm::vec3(0.f, 0.f, -a);
    else point = glm::vec3(-(1.f - std::cos(k * a)) / k, 0.f, -std::sin(k * a) / k);
}

// Turns the joints of a leg, from the one above the foot up to the hip, until the foot is on its target
void solveLeg(const GaitLeg& leg, std::vector<glm::mat4>& world, glm::vec3 target) {

    const int n = (int) leg.chain.size();
    const int foot = leg.chain[n - 1];
    const glm::mat3 attitude(world[foot]);

    // A target further than the leg is long : as far as it goes
    float length = 0.f;
    for(int c = 0; c + 1 < n; c++) length += glm::distance(glm::vec3(world[leg.chain[c]][3]), glm::vec3(world[leg.chain[c + 1]][3]));
    const glm::vec3 hip(world[leg.chain[0]][3]);
    glm::vec3 to = target - hip;
    float distance = glm::length(to);
    if(distance > 0.995f * length && distance > 1e-6f) target = hip + to * (0.995f * length / distance);

    for(int iteration = 0; iteration < 32; iteration++) {
        for(int c = n - 2; c >= 0; c--) {
            const glm::vec3 pivot(world[leg.chain[c]][3]);
            glm::vec3 a = glm::vec3(world[foot][3]) - pivot, b = target - pivot;
            float la = glm::length(a), lb = glm::length(b);
            if(la < 1e-6f || lb < 1e-6f) continue;
            glm::vec3 axis = glm::cross(a, b);
            float sine = glm::length(axis);
            float angle = std::atan2(sine, glm::dot(a, b));
            if(angle < 1e-5f || sine < 1e-9f) continue;
            // A joint near the foot turns less than one near the hip : the knee keeps the bend the clip gave it
            angle = std::min(angle, 0.5f);
            glm::mat4 turn = glm::translate(glm::mat4(1.f), pivot) * glm::rotate(glm::mat4(1.f), angle, axis / sine) * glm::translate(glm::mat4(1.f), -pivot);
            for(int j : leg.under[c]) world[j] = turn * world[j];
        }
        if(glm::distance(glm::vec3(world[foot][3]), target) < 2e-4f * std::max(length, 1e-3f)) break;
    }

    // The foot keeps the attitude it had, whatever the leg did
    const glm::vec3 at(world[foot][3]);
    glm::mat4 back = glm::mat4(attitude * glm::inverse(glm::mat3(world[foot])));
    glm::mat4 turn = glm::translate(glm::mat4(1.f), at) * back * glm::translate(glm::mat4(1.f), -at);
    for(int j : leg.under[n - 1]) world[j] = turn * world[j];

}

float groundAt(const GroundField* ground, glm::vec3 near, float reach) {
    if(!ground) return 0.f;
    if(auto hit = ground->below(near + glm::vec3(0.f, reach, 0.f), 3.f * reach)) return hit->point.y;
    if(auto hit = ground->top(near.x, near.z)) return hit->point.y;
    return near.y;
}

}



GaitModel dy::analyseGait(const AnimalSpecies& species, int clip, int samples) {

    GaitModel model;
    model.clip = clip;
    const int joints = (int) species.parents.size();
    if(clip < 0 || clip >= (int) species.clips.size()) {
        model.problem = "no such clip";
        return model;
    }
    model.period = species.clips[clip].duration();
    model.speed = species.clips[clip].speed;
    if(model.period <= 0.f || joints < 3 || samples < 8) {
        model.problem = "not a cycle";
        return model;
    }
    model.height = std::max(species.max.y - species.min.y, 1e-3f);
    model.ground = std::abs(species.min.y) < 0.2f * model.height ? species.min.y : 0.f;
    const float height = model.height;

    // The joints over the cycle, played in place
    std::vector<std::vector<glm::vec3>> at((size_t) samples, std::vector<glm::vec3>((size_t) joints));
    {
        std::vector<JointPose> pose;
        std::vector<glm::mat4> world;
        for(int n = 0; n < samples; n++) {
            species.sample(clip, model.period * (float) n / (float) samples, true, pose);
            species.worldMatrices(pose, world);
            for(int j = 0; j < joints; j++) at[n][j] = glm::vec3(world[j][3]);
        }
    }
    std::vector<float> low((size_t) joints, 1e9f), high((size_t) joints, -1e9f), sweep((size_t) joints);
    for(int j = 0; j < joints; j++) {
        float back = 1e9f, front = -1e9f;
        for(int n = 0; n < samples; n++) {
            low[j] = std::min(low[j], at[n][j].y);
            high[j] = std::max(high[j], at[n][j].y);
            back = std::min(back, at[n][j].z);
            front = std::max(front, at[n][j].z);
        }
        sweep[j] = front - back;
    }

    std::vector<std::vector<int>> children((size_t) joints);
    for(int j = 0; j < joints; j++) {
        if(species.parents[j] >= 0) children[species.parents[j]].push_back(j);
    }
    auto isUnder = [&](int j, int ancestor) {
        for(int p = species.parents[j]; p >= 0; p = species.parents[p]) {
            if(p == ancestor) return true;
        }
        return false;
    };
    auto commonAncestor = [&](int a, int b) {
        for(int p = species.parents[a]; p >= 0; p = species.parents[p]) {
            if(isUnder(b, p)) return p;
        }
        return -1;
    };

    // Feet : joints that come down to the ground, lift off it, and go back and forth as feet do
    float widest = 0.f;
    std::vector<bool> candidate((size_t) joints, false);
    for(int j = 0; j < joints; j++) {
        // Joints that move no skin are controls of the rig (targets of its own inverse kinematics) or the ends of bones
        candidate[j] = low[j] < model.ground + 0.14f * height && high[j] - low[j] > 0.012f * height && species.parents[j] >= 0
            && (species.joint_skin.empty() || species.joint_skin[j] > 0.f);
        if(candidate[j]) widest = std::max(widest, sweep[j]);
    }
    std::vector<int> feet;
    for(int j = 0; j < joints; j++) {
        if(!candidate[j] || sweep[j] < 0.3f * widest) continue;
        bool lowest = true;
        for(int other = j + 1; other < joints && lowest; other++) {
            if(candidate[other] && sweep[other] >= 0.3f * widest && isUnder(other, j)) lowest = false;
        }
        if(lowest) feet.push_back(j);
    }
    // The toes of a foot are one foot : the joint they hang from
    for(bool merged = true; merged;) {
        merged = false;
        for(size_t a = 0; a < feet.size() && !merged; a++) {
            for(size_t b = a + 1; b < feet.size() && !merged; b++) {
                int g = commonAncestor(feet[a], feet[b]);
                if(g < 0 || species.parents[g] < 0) continue;
                if(glm::distance(at[0][g], at[0][feet[a]]) > 0.2f * height || glm::distance(at[0][g], at[0][feet[b]]) > 0.2f * height) continue;
                feet.erase(std::remove_if(feet.begin(), feet.end(), [&](int f) { return isUnder(f, g); }), feet.end());
                feet.push_back(g);
                merged = true;
            }
        }
    }

    std::vector<int> under_count((size_t) joints, 0);
    for(int f : feet) {
        for(int p = f; p >= 0; p = species.parents[p]) under_count[p]++;
    }

    std::string dropped;
    for(int f : feet) {
        GaitLeg leg;
        leg.chain.push_back(f);
        int p = species.parents[f];
        while(p >= 0 && under_count[p] == 1 && leg.chain.size() < 6) {
            leg.chain.push_back(p);
            p = species.parents[p];
        }
        leg.attach = p;
        std::reverse(leg.chain.begin(), leg.chain.end());
        if(p < 0) {
            // The rig moves this foot with a control of its own, the leg is not what holds it : nothing to solve
            dropped += " " + species.joint_names[f] + " (not attached to the body)";
            continue;
        }
        if(leg.chain.size() < 3) {
            dropped += " " + species.joint_names[f] + " (a chain of " + std::to_string(leg.chain.size()) + ")";
            continue;
        }

        // Down : while the foot goes back under the body (the clip plays in place) and is not lifted. The longest
        // such stretch of the cycle
        std::vector<bool> down((size_t) samples);
        float fastest = 0.f;
        for(int n = 0; n < samples; n++) fastest = std::max(fastest, at[(n + 1) % samples][f].z - at[(n + samples - 1) % samples][f].z);
        const float threshold = low[f] + 0.6f * (high[f] - low[f]);
        for(int n = 0; n < samples; n++) {
            float back = at[(n + 1) % samples][f].z - at[(n + samples - 1) % samples][f].z;
            down[n] = at[n][f].y < threshold && (fastest < 1e-4f * height || back > 0.12f * fastest);
        }
        int best_start = 0, best_length = 0;
        for(int start = 0; start < samples; start++) {
            if(!down[start] || down[(start + samples - 1) % samples]) continue;
            int length = 0;
            while(length < samples && down[(start + length) % samples]) length++;
            if(length > best_length) {
                best_length = length;
                best_start = start;
            }
        }
        leg.duty = (float) best_length / (float) samples;
        if(leg.duty < 0.15f || leg.duty > 0.92f) {
            dropped += " " + species.joint_names[f] + " (down " + std::to_string((int) (100.f * leg.duty)) + " % of the cycle)";
            continue;
        }
        leg.touch = (float) best_start / (float) samples;
        leg.lift = wrap01(leg.touch + leg.duty);

        glm::vec3 sum(0.f);
        for(int n = 0; n < best_length; n++) sum += at[(best_start + n) % samples][f];
        leg.stand = sum / (float) best_length;
        leg.travel = std::max(at[(best_start + best_length - 1) % samples][f].z - at[best_start][f].z, 0.f) * (float) best_length / (float) std::max(best_length - 1, 1);
        leg.sole = leg.stand.y - model.ground;
        leg.apex = std::max(high[f] - leg.stand.y, 1e-4f);

        // How it rises over its swing, from 0 to 1 and back to 0
        const int swing = samples - best_length;
        leg.rise.resize(16);
        float peak = 1e-6f;
        for(int k = 0; k < 16; k++) {
            float u = ((float) k + 0.5f) / 16.f;
            float s = (float) (best_start + best_length) + u * (float) swing;
            int n0 = (int) std::floor(s);
            float t = s - (float) n0;
            float h = glm::mix(at[n0 % samples][f].y, at[(n0 + 1) % samples][f].y, t);
            // The ends of the swing are where the threshold cut them : taken down to the ground
            float ends = glm::mix(at[(best_start + best_length) % samples][f].y, at[best_start][f].y, u);
            leg.rise[k] = std::max(h - ends, 0.f);
            peak = std::max(peak, leg.rise[k]);
        }
        for(float& r : leg.rise) r /= peak;

        for(size_t c = 0; c + 1 < leg.chain.size(); c++) leg.reach += glm::distance(at[0][leg.chain[c]], at[0][leg.chain[c + 1]]);
        if(leg.reach < 0.3f * height) {
            // The end of a leg whose upper part hangs elsewhere in the rig : too short to put the foot anywhere
            dropped += " " + species.joint_names[f] + " (a chain " + std::to_string((int) (100.f * leg.reach / height)) + " % of the height long)";
            continue;
        }
        for(int joint : leg.chain) {
            std::vector<int> under { joint };
            for(int j = joint + 1; j < joints; j++) {
                if(isUnder(j, joint)) under.push_back(j);
            }
            leg.under.push_back(std::move(under));
        }
        model.legs.push_back(std::move(leg));
    }

    if(model.legs.size() < 2 || model.legs.size() % 2) {
        model.problem = std::string(model.legs.size() < 2 ? "fewer than two legs" : "an odd number of legs") + " found among " + std::to_string(feet.size()) + " joints near the ground :" + dropped;
        model.legs.clear();
        return model;
    }

    // Two legs under the middle of the body are a biped's; two at one end are half of an animal whose other legs
    // the rig holds some other way
    if(model.legs.size() == 2) {
        const float along = 0.5f * (model.legs[0].stand.z + model.legs[1].stand.z) - 0.5f * (species.min.z + species.max.z);
        if(std::abs(along) > 0.22f * (species.max.z - species.min.z)) {
            model.problem = "two legs found, at one end of the body";
            model.legs.clear();
            return model;
        }
    }

    // Fore and hind, left and right
    float middle = 0.f, back = -1e9f, front = 1e9f;
    for(auto& leg : model.legs) {
        middle += leg.stand.z;
        back = std::max(back, leg.stand.z);
        front = std::min(front, leg.stand.z);
    }
    middle /= (float) model.legs.size();
    const bool four = model.legs.size() >= 4 && back - front > 0.2f * height;
    float hind = 0.f, fore = 0.f;
    int hind_count = 0, fore_count = 0;
    for(auto& leg : model.legs) {
        leg.left = leg.stand.x < 0.f;
        leg.front = four && leg.stand.z < middle;
        float hip = at[0][leg.chain[0]].z;
        if(leg.front) {
            fore += hip;
            fore_count++;
        } else {
            hind += hip;
            hind_count++;
        }
    }
    model.hind = hind_count ? hind / (float) hind_count : 0.f;
    model.fore = fore_count ? fore / (float) fore_count : model.hind;

    // The speed the feet say : with it, a foot planted for its stance goes back under the body as far as in the clip
    float covered = 0.f;
    for(auto& leg : model.legs) covered += leg.travel / (leg.duty * model.period);
    covered /= (float) model.legs.size();
    if(covered > 0.02f * height) model.speed = covered;

    model.anchor.resize((size_t) joints);
    for(int j = 0; j < joints; j++) model.anchor[j] = j;
    for(auto& leg : model.legs) {
        for(int j : leg.under[0]) model.anchor[j] = leg.chain[0];
    }

    model.valid = true;
    return model;

}

const GaitModel& GaitModels::get(const AnimalSpecies& species, int clip) {
    auto& of_species = models[&species];
    auto it = of_species.find(clip);
    if(it == of_species.end()) {
        it = of_species.emplace(clip, analyseGait(species, clip)).first;
        const GaitModel& model = it->second;
        auto& out = dy::log(dy::info) << "Gait : " << species.id << " / " << species.clips[clip].name << " : ";
        if(model.valid) {
            out << model.legs.size() << " legs, " << model.period << " s, " << model.speed << " m/s";
            for(auto& leg : model.legs) {
                out << " | " << species.joint_names[leg.chain.back()] << " " << leg.chain.size() << " joints, down " << leg.touch << " to " << leg.lift;
            }
        } else {
            out << "no procedural walk (" << model.problem << ")";
        }
        out << std::endl;
    }
    return it->second;
}



GaitStyle GaitStyle::parse(const nlohmann::json& j, GaitStyle s) {
    if(!j.is_object()) return s;
    s.stride = j.value("stride", s.stride);
    s.step_height = j.value("step_height", s.step_height);
    s.clearance = j.value("clearance", s.clearance);
    s.soft_depth = j.value("soft_depth", s.soft_depth);
    s.sink_time = j.value("sink_time", s.sink_time);
    s.exit_vertical = j.value("exit_vertical", s.exit_vertical);
    s.entry_vertical = j.value("entry_vertical", s.entry_vertical);
    s.exit_part = j.value("exit_part", s.exit_part);
    s.body_sink = j.value("body_sink", s.body_sink);
    s.crouch = j.value("crouch", s.crouch);
    s.spread = j.value("spread", s.spread);
    s.bend = j.value("bend", s.bend);
    s.max_bend = j.value("max_bend", s.max_bend);
    s.head_lead = j.value("head_lead", s.head_lead);
    s.bank = j.value("bank", s.bank);
    s.turn_step = j.value("turn_step", s.turn_step);
    s.ik = j.value("ik", s.ik);
    return s;
}

const std::vector<std::string>& GaitStyle::names() {
    static const std::vector<std::string> list { "stride", "step_height", "clearance", "soft_depth", "sink_time", "exit_vertical", "entry_vertical",
        "exit_part", "body_sink", "crouch", "spread", "bend", "max_bend", "head_lead", "bank", "turn_step", "ik" };
    return list;
}

std::vector<float> GaitStyle::values() const {
    return { stride, step_height, clearance, soft_depth, sink_time, exit_vertical, entry_vertical, exit_part, body_sink, crouch, spread, bend,
        max_bend, head_lead, bank, turn_step, ik };
}

nlohmann::json GaitStyle::json() const {
    nlohmann::json j = nlohmann::json::object();
    auto v = values();
    for(size_t i = 0; i < v.size(); i++) j[names()[i]] = v[i];
    return j;
}



void dy::proceduralWalk(const AnimalSpecies& species, const GaitModel& model, const Transform& transform, float clip_time, float dt,
    const GroundField* ground, AnimalGait& gait, AnimalPose& pose) {

    const GaitStyle& style = gait.style;
    const float scale = std::max(transform.scale.y, 1e-4f);
    const float weight = gait.weight * std::clamp(style.ik, 0.f, 1.f);
    const size_t joints = species.parents.size();
    if(pose.world.size() != joints || !model.valid) return;

    // How the entity moves
    const glm::vec3 facing = transform.rotation * glm::vec3(0.f, 0.f, -1.f);
    const float yaw = std::atan2(-facing.x, -facing.z);
    const glm::vec2 heading(-std::sin(yaw), -std::cos(yaw));
    if(!gait.started) {
        gait.last_position = transform.position;
        gait.last_yaw = yaw;
        gait.speed = gait.turn = 0.f;
        gait.feet.assign(model.legs.size(), AnimalGait::Foot());
        gait.drop = 0.f;
        gait.started = true;
    }
    if(gait.feet.size() != model.legs.size()) gait.feet.assign(model.legs.size(), AnimalGait::Foot());
    if(dt > 1e-5f) {
        glm::vec3 moved = transform.position - gait.last_position;
        float follow = std::min(12.f * dt, 1.f);
        gait.speed += (glm::dot(glm::vec2(moved.x, moved.z), heading) / dt - gait.speed) * follow;
        gait.turn += (wrapAngle(yaw - gait.last_yaw) / dt - gait.turn) * follow;
    }
    gait.last_position = transform.position;
    gait.last_yaw = yaw;
    gait.phase = wrap01(clip_time / model.period);
    const float period = gait.period > 0.f ? gait.period : model.period;


    // The body along the curve of its path : every joint of the body is carried to the arc, at the place its
    // distance along the body gives it, and turned as the arc is there. A leg goes with its hip.

    const bool biped = std::abs(model.hind - model.fore) < 0.05f * model.height;
    const float span = biped ? 0.5f * model.height : model.hind - model.fore;
    float curvature = 0.f;
    if(std::abs(gait.speed) > 0.05f * model.speed * scale) {
        curvature = gait.turn / std::max(std::abs(gait.speed), 0.3f * model.speed * scale) * scale * style.bend * weight;
    }
    const float limit = glm::radians(style.max_bend) / std::max(span, 1e-3f);
    curvature = std::clamp(curvature, -limit, limit);
    gait.bend = curvature * span;
    const float middle = 0.5f * (model.hind + model.fore);
    const float bank = std::clamp(style.bank * gait.speed * gait.turn / 9.81f, -0.35f, 0.35f) * weight;
    const float lowered = (style.crouch + style.body_sink * style.soft_depth) / scale * weight;

    auto carry = [&](float z, float amount) {
        const float k = curvature * amount;
        const float a = middle - z, ahead = 0.5f * span;
        glm::vec3 point;
        float turned;
        if(a <= ahead || style.head_lead == 0.f) {
            arc(k, a, point, turned);
        } else {
            // The neck and the head turn further into the curve
            arc(k, ahead, point, turned);
            glm::vec3 further;
            float more;
            arc(k * (1.f + style.head_lead), a - ahead, further, more);
            point += glm::vec3(turnY(turned) * glm::vec4(further, 0.f));
            turned += more;
        }
        glm::mat4 m = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, middle) + point) * turnY(turned) * glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -z));
        // Leaning into the turn, about the line of the path on the ground; and lower in soft ground
        m = glm::translate(glm::mat4(1.f), glm::vec3(0.f, model.ground - lowered, 0.f)) * glm::rotate(glm::mat4(1.f), bank, glm::vec3(0.f, 0.f, 1.f))
            * glm::translate(glm::mat4(1.f), glm::vec3(0.f, -model.ground, 0.f)) * m;
        return m;
    };

    std::vector<glm::mat4>& world = pose.world;
    std::vector<float> hip_z(model.legs.size());
    for(size_t i = 0; i < model.legs.size(); i++) hip_z[i] = world[model.legs[i].chain[0]][3].z;
    {
        std::vector<glm::mat4> carried(joints);
        for(size_t j = 0; j < joints; j++) {
            if(model.anchor[j] == (int) j) carried[j] = carry(world[j][3].z, 1.f);
        }
        for(size_t j = 0; j < joints; j++) world[j] = carried[model.anchor[j]] * world[j];
    }


    // Feet : planted where they land until they leave, then over the ground to where they will land next

    const glm::mat4 to_world = transform.matrix();
    const glm::mat4 to_model = glm::inverse(to_world);
    const float reach = model.height * scale;

    std::vector<glm::vec3> targets(model.legs.size());
    for(size_t i = 0; i < model.legs.size(); i++) {
        const GaitLeg& leg = model.legs[i];
        AnimalGait::Foot& foot = gait.feet[i];
        const int joint = leg.chain.back();
        const glm::vec3 clip_point(world[joint][3]);
        const bool stance = inCycle(gait.phase, leg.touch, leg.lift);
        const float sole = leg.sole * scale;

        if(!foot.placed) {
            foot.position = glm::vec3(to_world * glm::vec4(clip_point, 1.f));
            foot.ground = groundAt(ground, foot.position, reach);
            foot.position.y = foot.ground;
            foot.target = foot.position;
            foot.planted = stance;
            foot.since = style.sink_time;
            foot.placed = true;
        }

        // Where it lands next : under its hip half a stance ahead, from where the body will be then
        const float swing_part = 1.f - leg.duty;
        const float u = stance ? 1.f : std::clamp(wrap01(gait.phase - leg.lift) / std::max(swing_part, 1e-3f), 0.f, 1.f);
        if(!stance) {
            const float left = (1.f - u) * swing_part * period;
            // Half of what the body covers while the foot is down : the clip's own figure at the clip's speed and pace
            const float travel = model.speed > 1e-4f ? leg.travel * (gait.speed * period) / (model.speed * scale * model.period) : 0.f;
            glm::vec3 landing = leg.stand + glm::vec3((leg.left ? -1.f : 1.f) * style.spread / scale, 0.f, -0.5f * travel);
            glm::vec3 along = glm::vec3(carry(hip_z[i], style.turn_step) * glm::vec4(landing, 1.f));
            const float then_yaw = gait.turn * left;
            const glm::quat then_rotation = glm::angleAxis(then_yaw, glm::vec3(0.f, 1.f, 0.f)) * transform.rotation;
            const float mid = yaw + 0.5f * then_yaw;
            glm::vec3 then_position = transform.position + glm::vec3(-std::sin(mid), 0.f, -std::cos(mid)) * gait.speed * left;
            foot.target = then_position + then_rotation * (along * scale);
            foot.target.y = groundAt(ground, foot.target, reach);
        }

        if(stance && !foot.planted) {
            foot.planted = true;
            foot.position = foot.target;
            foot.ground = foot.target.y;
            foot.since = 0.f;
        } else if(!stance && foot.planted) {
            // It leaves from where it is, as deep as it sank
            foot.planted = false;
            foot.position.y = foot.ground - style.soft_depth * std::min(foot.since / std::max(style.sink_time, 1e-3f), 1.f);
        }

        glm::vec3 wanted;
        if(foot.planted) {
            foot.since += dt;
            float sunk = style.soft_depth * std::min(foot.since / std::max(style.sink_time, 1e-3f), 1.f);
            wanted = glm::vec3(foot.position.x, foot.ground - sunk + sole, foot.position.z);
        } else {
            const float depth = std::max(foot.ground - foot.position.y, 0.f);
            const float lifted = std::max(leg.apex * scale * style.step_height, style.clearance);
            // Out of the ground first, the more so the deeper it is and the straighter up it is asked to come
            const float out = depth > 1e-4f ? std::clamp(2.f * style.exit_part * depth / (depth + lifted), 0.f, 0.45f) : 0.f;
            const float hold = style.exit_vertical * out;
            const float settle = style.entry_vertical * (style.soft_depth > 1e-4f ? out : 0.f) + 0.02f;
            const float along = smooth(hold, 1.f - settle, u);
            glm::vec2 over = glm::mix(glm::vec2(foot.position.x, foot.position.z), glm::vec2(foot.target.x, foot.target.z), along);
            float floor_y = groundAt(ground, glm::vec3(over.x, glm::mix(foot.ground, foot.target.y, along), over.y), reach);

            float climbing = depth > 1e-4f ? smooth(0.f, out, u) : 1.f;
            float v = std::clamp((u - out) / std::max(1.f - out, 1e-3f), 0.f, 1.f) * 16.f - 0.5f;
            int k = std::clamp((int) std::floor(v), 0, 14);
            float rise = v <= 0.f ? leg.rise[0] * std::max(v + 0.5f, 0.f) * 2.f : glm::mix(leg.rise[k], leg.rise[k + 1], std::clamp(v - (float) k, 0.f, 1.f));
            if(v > 15.f) rise = leg.rise[15] * std::max(1.f - (v - 15.f) * 2.f, 0.f);
            wanted = glm::vec3(over.x, floor_y - depth * (1.f - climbing) + lifted * rise + sole, over.y);
        }
        foot.now = wanted;

        targets[i] = glm::mix(clip_point, glm::vec3(to_model * glm::vec4(wanted, 1.f)), weight);
    }

    // A leg at the end of its reach would let its foot go : the body comes down as much as the shortest leg needs
    float needed = 0.f;
    for(size_t i = 0; i < model.legs.size(); i++) {
        const GaitLeg& leg = model.legs[i];
        float length = 0.f;
        for(size_t c = 0; c + 1 < leg.chain.size(); c++) length += glm::distance(glm::vec3(world[leg.chain[c]][3]), glm::vec3(world[leg.chain[c + 1]][3]));
        const glm::vec3 hip(world[leg.chain[0]][3]);
        const float reachable = 0.985f * length;
        glm::vec2 aside(targets[i].x - hip.x, targets[i].z - hip.z);
        float flat = glm::dot(aside, aside);
        if(flat >= reachable * reachable || hip.y <= targets[i].y) continue;
        // The hip over the foot by what is left of the leg once it has gone that far aside
        float over = std::sqrt(reachable * reachable - flat);
        needed = std::max(needed, (hip.y - targets[i].y) - over);
    }
    needed = std::min(needed, 0.12f * model.height) * (weight > 0.f ? 1.f : 0.f);
    // Down at once, up again slowly
    gait.drop = needed > gait.drop ? needed : gait.drop + (needed - gait.drop) * std::min(6.f * dt, 1.f);
    if(gait.drop > 1e-6f) {
        const glm::mat4 down = glm::translate(glm::mat4(1.f), glm::vec3(0.f, -gait.drop, 0.f));
        for(auto& m : world) m = down * m;
    }
    if(weight > 0.f) {
        for(size_t i = 0; i < model.legs.size(); i++) solveLeg(model.legs[i], world, targets[i]);
    }

    species.skinMatrices(world, pose.skin);

}



void AnimalGaitSys::preinit() {
    reg.storage<AnimalGait>();
    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<Transform>();
    if(!reg.ctx().find<GaitModels>()) reg.ctx().emplace<GaitModels>();
}

void AnimalGaitSys::init() {
    if(auto* field = reg.ctx().find<std::shared_ptr<GroundField>>()) ground = *field;
    else if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());
    else ground = std::make_shared<PlaneField>();
}

void AnimalGaitSys::tick(double dt_seconds) {

    OPTICK_EVENT();

    auto& library = animalLibrary(reg);
    auto& models = reg.ctx().get<GaitModels>();
    const float dt = (float) std::min(dt_seconds, 0.1);
    auto* override_style = reg.ctx().find<GaitStyleOverride>();

    for(auto [entity, transform, animal, gait, pose] : reg.view<const Transform, Animal, AnimalGait, AnimalPose>().each()) {
        auto species = library.species(animal.species);
        if(!species) continue;
        if(override_style && override_style->on) gait.style = override_style->style;

        const GaitModel* model = nullptr;
        if(animal.clip >= 0 && animal.clip < (int) species->clips.size()) {
            const std::string& kind = species->clips[animal.clip].kind;
            if(kind == "walk" || kind == "trot" || kind == "run") model = &models.get(*species, animal.clip);
        }
        if(!model || !model->valid) {
            gait.model = -1;
            gait.weight = 0.f;
            gait.started = false;
            continue;
        }
        if(gait.model != animal.clip) {
            // Another gait : the feet are where the body is
            gait.model = animal.clip;
            gait.started = false;
            gait.weight = 0.f;
        }
        gait.weight = std::min(gait.weight + dt / 0.3f, 1.f);

        // The steps match the speed over the ground : longer or shorter ones at first, then quicker or slower ones
        const float scale = std::max(transform.scale.y, 1e-4f);
        const float stride = std::max(model->speed, 0.05f * model->height) * model->period * scale * std::max(gait.style.stride, 0.1f);
        gait.period = std::clamp(stride / std::max(std::abs(gait.speed), 1e-3f), 0.55f * model->period, 1.8f * model->period);
        animal.rate = model->period / gait.period;

        proceduralWalk(*species, *model, transform, animal.time, dt, ground.get(), gait, pose);
    }

}
