#include "animal_body.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <thread>

#include "logic/agents/ground_field.h"
#include "logic/animals/gait_walk.h"
#include "scene/world.h"
#include "util/paths.h"

using namespace dy;
using json = nlohmann::json;

namespace {

const float pi = 3.14159265358979f;
const float deg = pi / 180.f;

json toJson(glm::vec3 v) { return json::array({ v.x, v.y, v.z }); }
json toJson(glm::quat q) { return json::array({ q.x, q.y, q.z, q.w }); }
glm::vec3 vec3From(const json& j) { return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); }
glm::quat quatFrom(const json& j) { return glm::normalize(glm::quat(j.at(3).get<float>(), j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>())); }

// The smallest rotation taking a to b, both unit
glm::quat between(glm::vec3 a, glm::vec3 b) {
    float d = glm::dot(a, b);
    if(d < -0.999999f) {
        glm::vec3 axis = std::abs(a.x) < 0.9f ? glm::normalize(glm::cross(a, glm::vec3(1.f, 0.f, 0.f))) : glm::normalize(glm::cross(a, glm::vec3(0.f, 1.f, 0.f)));
        return glm::angleAxis(pi, axis);
    }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

glm::vec3 logOf(glm::quat q) {
    if(q.w < 0.f) q = -q;
    glm::vec3 v(q.x, q.y, q.z);
    float s = glm::length(v);
    if(s < 1e-9f) return glm::vec3(0.f);
    return v * (2.f * std::atan2(s, q.w) / s);
}

glm::quat expOf(glm::vec3 v) {
    float angle = glm::length(v);
    return angle > 1e-9f ? glm::angleAxis(angle, v / angle) : glm::quat(1.f, 0.f, 0.f, 0.f);
}

float angleBetween(glm::quat a, glm::quat b) {
    float d = std::min(std::abs(glm::dot(a, b)), 1.f);
    return 2.f * std::acos(d);
}

// Eigen decomposition of a symmetric matrix (Jacobi): values in decreasing order, vectors as columns
void eigen(glm::dmat3 a, glm::dvec3& values, glm::dmat3& vectors) {
    glm::dmat3 v(1.);
    for(int sweep = 0; sweep < 60; sweep++) {
        double off = std::abs(a[0][1]) + std::abs(a[0][2]) + std::abs(a[1][2]);
        if(off <= 1e-15 * (std::abs(a[0][0]) + std::abs(a[1][1]) + std::abs(a[2][2])) || off < 1e-300) break;
        for(int p = 0; p < 3; p++) {
            for(int q = p + 1; q < 3; q++) {
                if(std::abs(a[p][q]) < 1e-300) continue;
                double theta = (a[q][q] - a[p][p]) / (2. * a[p][q]);
                double t = (theta >= 0. ? 1. : -1.) / (std::abs(theta) + std::sqrt(theta * theta + 1.));
                double c = 1. / std::sqrt(t * t + 1.), s = t * c;
                for(int k = 0; k < 3; k++) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for(int k = 0; k < 3; k++) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for(int k = 0; k < 3; k++) {
                    double vkp = v[p][k], vkq = v[q][k];
                    v[p][k] = c * vkp - s * vkq;
                    v[q][k] = s * vkp + c * vkq;
                }
            }
        }
    }
    int order[3] = { 0, 1, 2 };
    std::sort(order, order + 3, [&](int x, int y) { return a[x][x] > a[y][y]; });
    for(int i = 0; i < 3; i++) {
        values[i] = a[order[i]][order[i]];
        vectors[i] = v[order[i]];
    }
}

// Weighted moments of a set of points
struct Moments {
    double w = 0.;
    glm::dvec3 sum = glm::dvec3(0.);
    glm::dmat3 outer = glm::dmat3(0.);

    void add(glm::dvec3 p, double weight) {
        w += weight;
        sum += p * weight;
        outer += glm::outerProduct(p, p) * weight;
    }
    Moments& operator+=(const Moments& o) { w += o.w; sum += o.sum; outer += o.outer; return *this; }
    glm::dvec3 mean() const { return w > 0. ? sum / w : glm::dvec3(0.); }
    glm::dmat3 covariance() const {
        if(w <= 0.) return glm::dmat3(0.);
        glm::dvec3 m = mean();
        return outer / w - glm::outerProduct(m, m);
    }
    // Volume of the box that a surface with these moments would wrap
    double volume() const {
        if(w <= 0.) return 0.;
        glm::dvec3 values;
        glm::dmat3 vectors;
        eigen(covariance(), values, vectors);
        return 18. * std::sqrt(std::max(values[0], 0.) * std::max(values[1], 0.) * std::max(values[2], 0.));
    }
};

float quantile(std::vector<std::pair<float, float>>& values, float q) {
    // (value, weight), sorted here
    if(values.empty()) return 0.f;
    std::sort(values.begin(), values.end());
    double total = 0.;
    for(auto& v : values) total += v.second;
    double want = q * total, run = 0.;
    for(auto& v : values) {
        run += v.second;
        if(run >= want) return v.first;
    }
    return values.back().first;
}

float plainQuantile(std::vector<float> values, float q) {
    if(values.empty()) return 0.f;
    std::sort(values.begin(), values.end());
    float at = q * (float) (values.size() - 1);
    size_t i = (size_t) at;
    if(i + 1 >= values.size()) return values.back();
    return glm::mix(values[i], values[i + 1], at - (float) i);
}

struct MotionSample {
    std::vector<glm::quat> rotation;    // per joint, model space
    glm::vec3 root = glm::vec3(0.f);    // the trunk's joint
    std::vector<glm::mat4> world;       // kept for the walks only
    bool walk = false;                  // the procedural walk or a gait clip: its whole range is kept
};

struct Fit {
    ShapeDef shape;
    float volume = 0.f;
    glm::vec3 inertia = glm::vec3(1.f); // principal, per unit of mass, in the shape's axes
};

float capsuleVolume(float r, float h) { return pi * r * r * 2.f * h + 4.f / 3.f * pi * r * r * r; }

void finishFit(Fit& fit) {
    ShapeDef& s = fit.shape;
    if(s.type == ShapeDef::Type::Capsule) {
        float r = s.radius, h = s.half_height;
        float vc = pi * r * r * 2.f * h, vh = 4.f / 3.f * pi * r * r * r;
        fit.volume = vc + vh;
        float mc = vc / fit.volume, mh = vh / fit.volume;
        float axis = mc * r * r / 2.f + mh * 2.f * r * r / 5.f;
        float across = mc * (r * r / 4.f + h * h / 3.f) + mh * (2.f * r * r / 5.f + h * h + 0.75f * h * r);
        fit.inertia = glm::vec3(across, axis, across);
    } else if(s.type == ShapeDef::Type::Sphere) {
        fit.volume = 4.f / 3.f * pi * s.radius * s.radius * s.radius;
        fit.inertia = glm::vec3(0.4f * s.radius * s.radius);
    } else {
        glm::vec3 e = s.half_extents;
        fit.volume = 8.f * e.x * e.y * e.z;
        fit.inertia = glm::vec3(e.y * e.y + e.z * e.z, e.x * e.x + e.z * e.z, e.x * e.x + e.y * e.y) / 3.f;
    }
}

json shapeJson(const ShapeDef& s) {
    json j;
    j["type"] = s.type == ShapeDef::Type::Capsule ? "capsule" : s.type == ShapeDef::Type::Sphere ? "sphere" : "box";
    j["center"] = toJson(s.center);
    j["rotation"] = toJson(s.rotation);
    if(s.type == ShapeDef::Type::Box) j["half_extents"] = toJson(s.half_extents);
    else {
        j["radius"] = s.radius;
        if(s.type == ShapeDef::Type::Capsule) j["half_height"] = s.half_height;
    }
    return j;
}

// Jolt's decomposition of a constraint space rotation: q = swing * twist, the twist about X
JointAngles decompose(glm::quat q) {
    if(q.w < 0.f) q = -q;
    JointAngles out;
    float s = std::sqrt(q.x * q.x + q.w * q.w);
    glm::quat twist = s > 1e-9f ? glm::quat(q.w / s, q.x / s, 0.f, 0.f) : glm::quat(1.f, 0.f, 0.f, 0.f);
    glm::quat swing = q * glm::conjugate(twist);
    if(swing.w < 0.f) swing = -swing;
    out.twist = 2.f * std::atan2(twist.x, twist.w);
    if(out.twist > pi) out.twist -= 2.f * pi;
    if(out.twist < -pi) out.twist += 2.f * pi;
    out.swing_y = swing.y;
    out.swing_z = swing.z;
    return out;
}

// The volume inside a mesh that need not be closed, nor wound one way, nor a single skin: points of a grid count
// when the surface winds around them (the generalized winding number), each given to the class of the nearest
// vertex. Per class: the moments of its cells (weights in m3). Returns the cell size
float windingMoments(const std::vector<glm::vec3>& points, const std::vector<int>& point_class, const std::vector<std::array<glm::vec3, 3>>& triangles,
    int classes, float cell, int most, std::vector<Moments>& out) {

    out.assign(classes, Moments());
    if(points.empty() || triangles.empty()) return 0.f;
    glm::vec3 lo(1e9f), hi(-1e9f);
    for(auto& t : triangles) for(auto& v : t) {
        lo = glm::min(lo, v);
        hi = glm::max(hi, v);
    }
    const glm::vec3 extent = hi - lo;
    const float longest = std::max({ extent.x, extent.y, extent.z });
    if(longest <= 0.f) return 0.f;
    cell = std::max(cell, longest / (float) most);
    const int nx = std::max(1, (int) std::ceil(extent.x / cell)), ny = std::max(1, (int) std::ceil(extent.y / cell)), nz = std::max(1, (int) std::ceil(extent.z / cell));
    const unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), 24u));
    std::vector<std::vector<Moments>> sums(workers, std::vector<Moments>(classes));
    const double voxel = (double) cell * cell * cell;
    std::vector<std::thread> threads;
    for(unsigned w = 0; w < workers; w++) {
        threads.emplace_back([&, w]() {
            for(int ix = (int) w; ix < nx; ix += (int) workers) {
                for(int iy = 0; iy < ny; iy++) {
                    for(int iz = 0; iz < nz; iz++) {
                        const glm::vec3 p = lo + (glm::vec3((float) ix, (float) iy, (float) iz) + 0.5f) * cell;
                        double winding = 0.;
                        for(auto& t : triangles) {
                            const glm::dvec3 a(t[0] - p), b(t[1] - p), c(t[2] - p);
                            const double la = glm::length(a), lb = glm::length(b), lc = glm::length(c);
                            const double num = glm::dot(a, glm::cross(b, c));
                            const double den = la * lb * lc + glm::dot(a, b) * lc + glm::dot(b, c) * la + glm::dot(c, a) * lb;
                            winding += 2. * std::atan2(num, den);
                        }
                        if(std::abs(winding) < 2. * 3.14159265358979) continue;     // half of the full solid angle
                        float best = 1e30f;
                        int nearest = 0;
                        for(size_t v = 0; v < points.size(); v++) {
                            const glm::vec3 d = points[v] - p;
                            const float d2 = glm::dot(d, d);
                            if(d2 < best) {
                                best = d2;
                                nearest = point_class[v];
                            }
                        }
                        sums[w][nearest].add(glm::dvec3(p), voxel);
                    }
                }
            }
        });
    }
    for(auto& t : threads) t.join();
    for(auto& sum : sums) for(int c = 0; c < classes; c++) out[c] += sum[c];
    return cell;

}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if(!in) throw AssetError(path + ": cannot open");
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

}

glm::quat dy::matrixRotation(const glm::mat4& m) {
    glm::mat3 r(m);
    if(glm::determinant(r) < 0.f) r[0] = -r[0];
    r[0] = glm::normalize(r[0]);
    r[1] = glm::normalize(r[1] - r[0] * glm::dot(r[0], r[1]));
    r[2] = glm::cross(r[0], r[1]);
    return glm::normalize(glm::quat_cast(r));
}

glm::quat dy::constraintRotation(float twist, float swing_y, float swing_z) {
    float angle = std::sqrt(swing_y * swing_y + swing_z * swing_z);
    glm::quat swing = angle > 1e-7f ? glm::angleAxis(angle, glm::vec3(0.f, swing_y / angle, swing_z / angle)) : glm::quat(1.f, 0.f, 0.f, 0.f);
    return glm::normalize(swing * glm::angleAxis(twist, glm::vec3(1.f, 0.f, 0.f)));
}

AnimalBodyOptions AnimalBodyOptions::parse(const nlohmann::json& overrides, const std::string& species) {
    AnimalBodyOptions o;
    auto apply = [&](const nlohmann::json& j) {
        if(!j.is_object()) return;
        o.density = j.value("density", o.density);
        o.mass = j.value("mass", o.mass);
        o.max_bodies = j.value("max_bodies", o.max_bodies);
        o.max_leg_bodies = j.value("max_leg_bodies", o.max_leg_bodies);
        o.merge_loss = j.value("merge_loss", o.merge_loss);
        o.limit_margin = j.value("limit_margin", o.limit_margin);
        o.limit_margin_part = j.value("limit_margin_part", o.limit_margin_part);
        o.stiffness = j.value("stiffness", o.stiffness);
        o.appendage_stiffness = j.value("appendage_stiffness", o.appendage_stiffness);
        o.damping_time = j.value("damping_time", o.damping_time);
        o.torque_angle = j.value("torque_angle", o.torque_angle);
        o.friction = j.value("friction", o.friction);
        o.min_leg_mass = j.value("min_leg_mass", o.min_leg_mass);
        o.min_mass = j.value("min_mass", o.min_mass);
        o.min_inertia = j.value("min_inertia", o.min_inertia);
        o.velocity_steps = j.value("velocity_steps", o.velocity_steps);
        o.biped_velocity_steps = j.value("biped_velocity_steps", o.biped_velocity_steps);
        o.biped_position_steps = j.value("biped_position_steps", o.biped_position_steps);
        o.min_substeps = j.value("min_substeps", o.min_substeps);
        o.inertia_condition = j.value("inertia_condition", o.inertia_condition);
        o.position_steps = j.value("position_steps", o.position_steps);
        o.stand = j.value("stand", o.stand);
    };
    if(overrides.is_object()) {
        if(auto it = overrides.find("defaults"); it != overrides.end()) apply(*it);
        if(auto it = overrides.find("species"); it != overrides.end() && it->is_object()) {
            if(auto mine = it->find(species); mine != it->end()) apply(*mine);
        }
    }
    return o;
}

nlohmann::json AnimalBodyOptions::json() const {
    return { { "density", density }, { "mass", mass }, { "max_bodies", max_bodies }, { "max_leg_bodies", max_leg_bodies }, { "merge_loss", merge_loss },
        { "limit_margin", limit_margin }, { "limit_margin_part", limit_margin_part }, { "stiffness", stiffness }, { "appendage_stiffness", appendage_stiffness },
        { "damping_time", damping_time }, { "torque_angle", torque_angle }, { "friction", friction }, { "stand", stand },
        { "min_leg_mass", min_leg_mass }, { "min_mass", min_mass }, { "min_inertia", min_inertia }, { "velocity_steps", velocity_steps }, { "biped_velocity_steps", biped_velocity_steps }, { "biped_position_steps", biped_position_steps }, { "min_substeps", min_substeps }, { "inertia_condition", inertia_condition }, { "position_steps", position_steps } };
}



json dy::generateAnimalBody(const AnimalLibrary::Entry& entry, const AnimalSpecies& species, const std::vector<AnimalVertex>& vertices,
    const std::vector<uint32_t>& indices, const GaitModel& walk, const AnimalBodyOptions& options, std::string& error) {

    const int joints = (int) species.parents.size();
    if(!walk.valid || walk.legs.size() < 2) {
        error = "the walk clip is not read: " + walk.problem;
        return json();
    }
    if(species.lods.empty() || vertices.empty()) {
        error = "no mesh";
        return json();
    }
    const auto& parents = species.parents;
    const float height = std::max(species.max.y - species.min.y, 1e-3f);
    const float size = std::max({ height, entry.length, 1e-3f });

    auto isUnder = [&](int j, int ancestor) {
        for(int p = j; p >= 0; p = parents[p]) if(p == ancestor) return true;
        return false;
    };


    // The bind pose (the mesh is given in it) and the standing pose: the idle clip when it stands on its feet

    std::vector<glm::mat4> bind, stand;
    species.worldMatrices(species.rest, bind);
    std::vector<glm::quat> bind_rotation(joints), bind_inverse(joints);
    std::vector<glm::vec3> bind_position(joints);
    for(int j = 0; j < joints; j++) {
        bind_rotation[j] = matrixRotation(bind[j]);
        bind_inverse[j] = glm::inverse(bind_rotation[j]);
        bind_position[j] = glm::vec3(bind[j][3]);
    }
    std::string stand_clip;
    {
        int clip = options.stand.empty() ? species.idle : species.findClip(options.stand);
        std::vector<JointPose> pose;
        bool ok = clip >= 0;
        if(ok) {
            species.sample(clip, 0.f, true, pose);
            species.worldMatrices(pose, stand);
            for(auto& leg : walk.legs) {
                float foot = stand[leg.chain.back()][3].y;
                if(std::abs(foot - (walk.ground + leg.sole)) > 0.06f * height) ok = false;
            }
        }
        // Else the bind pose when its feet are on the ground: they are flat there
        bool bind_stands = true;
        for(auto& leg : walk.legs) {
            if(std::abs(bind[leg.chain.back()][3].y - (walk.ground + leg.sole)) > 0.06f * height) bind_stands = false;
        }
        if(ok) stand_clip = species.clips[clip].name;
        else if(bind_stands) stand = bind;
        else {
            // No idle that stands on its feet: the walk clip where the most feet are down, and as near the ground as
            // they get (the bind pose of a man holds his arms out)
            const int steps = 48;
            int best_down = -1;
            float best_spread = 1e9f, best_time = 0.f;
            std::vector<glm::mat4> world;
            for(int n = 0; n < steps; n++) {
                const float time = walk.period * (float) n / (float) steps, phase = (float) n / (float) steps;
                species.sample(walk.clip, time, true, pose);
                species.worldMatrices(pose, world);
                int down = 0;
                float spread = 0.f;
                for(auto& leg : walk.legs) {
                    float since = phase - leg.touch;
                    since -= std::floor(since);
                    if(since < leg.duty) down++;
                    spread = std::max(spread, std::abs(world[leg.chain.back()][3].y - (walk.ground + leg.sole)));
                }
                if(down > best_down || (down == best_down && spread < best_spread)) {
                    best_down = down;
                    best_spread = spread;
                    best_time = time;
                }
            }
            if(best_down == (int) walk.legs.size() && best_spread < 0.06f * height) {
                species.sample(walk.clip, best_time, true, pose);
                species.worldMatrices(pose, stand);
                stand_clip = species.clips[walk.clip].name + " @ " + std::to_string(best_time);
            } else stand = bind;
        }
    }
    std::vector<glm::quat> stand_rotation(joints);
    std::vector<glm::vec3> stand_position(joints);
    for(int j = 0; j < joints; j++) {
        // The rotation of the segment that follows joint j: the identity in the bind pose
        stand_rotation[j] = glm::normalize(matrixRotation(stand[j]) * bind_inverse[j]);
        stand_position[j] = glm::vec3(stand[j][3]);
    }


    // The skin: what each joint moves, weighted by the surface

    const AnimalLod& lod = species.lods[0];
    std::vector<float> area(vertices.size(), 0.f);
    double mesh_volume = 0.;
    for(auto& draw : lod.draws) {
        for(uint32_t i = draw.first_index; i + 2 < draw.first_index + draw.index_count && i + 2 < indices.size(); i += 3) {
            const glm::vec3 a = vertices[indices[i]].position, b = vertices[indices[i + 1]].position, c = vertices[indices[i + 2]].position;
            float tri = 0.5f * glm::length(glm::cross(b - a, c - a));
            area[indices[i]] += tri / 3.f;
            area[indices[i + 1]] += tri / 3.f;
            area[indices[i + 2]] += tri / 3.f;
            mesh_volume += glm::dot(glm::dvec3(a), glm::cross(glm::dvec3(b), glm::dvec3(c))) / 6.;
        }
    }
    mesh_volume = std::abs(mesh_volume);
    std::vector<int> dominant(vertices.size(), -1);
    std::vector<Moments> own(joints);
    double skin_total = 0.;
    for(size_t v = 0; v < vertices.size(); v++) {
        if(area[v] <= 0.f) continue;
        int best = 0;
        for(int k = 1; k < 4; k++) if(vertices[v].weights[k] > vertices[v].weights[best]) best = k;
        dominant[v] = vertices[v].joints[best];
        own[dominant[v]].add(glm::dvec3(vertices[v].position), area[v]);
        skin_total += area[v];
    }
    if(skin_total <= 0.) {
        error = "the mesh has no surface";
        return json();
    }

    // The volume each joint moves. The signed volume of the triangles is only right for one closed skin wound one
    // way: fur cards, feathers, eyes and teeth inside or a second skin break it, and a wing or an antler is all
    // surface. What the surface winds around holds for all of them: cells of a grid, each given to the joint of the
    // nearest vertex. A first pass sizes the cells: a sixteenth of the side of the cube of the same volume
    std::vector<Moments> solid(joints);
    float cell = 0.f;
    {
        std::vector<glm::vec3> points;
        std::vector<int> point_joint;
        for(size_t v = 0; v < vertices.size(); v++) {
            if(dominant[v] < 0) continue;
            points.push_back(vertices[v].position);
            point_joint.push_back(dominant[v]);
        }
        std::vector<std::array<glm::vec3, 3>> triangles;
        for(auto& draw : lod.draws) {
            for(uint32_t i = draw.first_index; i + 2 < draw.first_index + draw.index_count && i + 2 < indices.size(); i += 3) {
                triangles.push_back({ vertices[indices[i]].position, vertices[indices[i + 1]].position, vertices[indices[i + 2]].position });
            }
        }
        windingMoments(points, point_joint, triangles, joints, 0.f, 32, solid);
        double rough = 0.;
        for(auto& m : solid) rough += m.w;
        if(rough > 0.) cell = windingMoments(points, point_joint, triangles, joints, (float) std::cbrt(rough) / 16.f, 144, solid);
    }
    double volume = 0.;
    for(auto& m : solid) volume += m.w;
    if(volume <= 0.) {
        error = "the mesh encloses no volume";
        return json();
    }
    const double cell_volume = (double) cell * cell * cell;


    // How the animal moves: every clip, and the procedural walk over its ranges

    std::vector<MotionSample> samples;
    {
        std::vector<JointPose> pose;
        std::vector<glm::mat4> world;
        auto keep = [&](const std::vector<glm::mat4>& w, bool is_walk, bool on_hard_ground = true) {
            MotionSample s;
            s.walk = is_walk;
            s.rotation.resize(joints);
            for(int j = 0; j < joints; j++) s.rotation[j] = matrixRotation(w[j]);
            if(is_walk && on_hard_ground) s.world = w;
            samples.push_back(std::move(s));
        };
        keep(stand, true);
        for(size_t c = 0; c < species.clips.size(); c++) {
            const AnimalClip& clip = species.clips[c];
            const bool gait_clip = clip.kind == "walk" || clip.kind == "trot" || clip.kind == "run";
            const int step = std::max(1, (int) clip.frames / 160);
            for(uint32_t f = 0; f < clip.frames; f += step) {
                species.sample((int) c, (float) f / clip.fps, true, pose);
                species.worldMatrices(pose, world);
                keep(world, gait_clip);
            }
        }
        GaitWalk gw;
        std::shared_ptr<const AnimalSpecies> alias(std::shared_ptr<void>(), &species);
        if(gw.setup(alias, walk.clip, entry.length)) {
            WorldRng rng(0x5EED0000ull + (uint64_t) joints);
            GaitRanges ranges = GaitRanges::defaults();
            PlaneField plane;
            const float clip_speed = species.clips[walk.clip].speed > 0.02f ? species.clips[walk.clip].speed : walk.speed;
            const float dt = 1.f / 60.f;
            for(int n = 0; n < 16; n++) {
                GaitStyle style = drawStyle(ranges, height, rng);
                gw.start(style, glm::vec3(0.f), (float) rng.uniform(0., 2. * pi), (float) rng.uniform());
                WalkCommand command;
                startCommand(command, ranges, clip_speed, gw.length, gw.walker.yaw, rng);
                for(int tick = 0; tick < 420; tick++) {
                    stepCommand(command, ranges, clip_speed, gw.length, rng, dt);
                    gw.step(command.speed, command.turn, &plane, dt);
                    if(tick >= 60 && tick % 3 == 0) keep(gw.pose.world, true, style.soft_depth <= 0.f);
                }
            }
        }
    }

    // How far each joint turns in its parent while the animal walks, trots or runs, against how it stands: 98
    // samples in 100. What only moves in other clips (a wing that flies, a jaw that eats) is not what a walker needs
    // as a body; its limits, below, are from every clip
    std::vector<float> turn(joints, 0.f);
    for(int j = 0; j < joints; j++) {
        if(parents[j] < 0) continue;
        glm::quat reference = glm::inverse(samples[0].rotation[parents[j]]) * samples[0].rotation[j];
        std::vector<float> angles;
        angles.reserve(samples.size());
        for(auto& s : samples) if(s.walk) angles.push_back(angleBetween(reference, glm::inverse(s.rotation[parents[j]]) * s.rotation[j]));
        turn[j] = plainQuantile(std::move(angles), 0.98f);
    }
    // The same on both sides: the joint mirrored in x
    std::vector<int> mirror(joints);
    for(int j = 0; j < joints; j++) {
        mirror[j] = j;
        glm::vec3 want(-bind_position[j].x, bind_position[j].y, bind_position[j].z);
        if(std::abs(bind_position[j].x) < 0.01f * size) continue;
        float best = 0.03f * size;
        for(int k = 0; k < joints; k++) {
            float d = glm::distance(bind_position[k], want);
            if(k != j && d < best) {
                best = d;
                mirror[j] = k;
            }
        }
    }
    for(int j = 0; j < joints; j++) if(mirror[mirror[j]] != j) mirror[j] = j;
    {
        std::vector<float> both(turn);
        for(int j = 0; j < joints; j++) both[j] = 0.5f * (turn[j] + turn[mirror[j]]);
        turn = both;
    }


    // Legs: the chains of the gait analysis, the foot at the end. A bone too short or that does not turn rides on
    // the one above; the same choice for the legs of a pair

    std::vector<int> leg_of(joints, -1);
    for(size_t l = 0; l < walk.legs.size(); l++) {
        for(int j : walk.legs[l].under[0]) leg_of[j] = (int) l;
    }
    std::vector<char> is_body(joints, 0);
    std::vector<std::vector<int>> leg_bodies(walk.legs.size());
    {
        std::vector<char> done(walk.legs.size(), 0);
        for(size_t l = 0; l < walk.legs.size(); l++) {
            if(done[l]) continue;
            const GaitLeg& leg = walk.legs[l];
            std::vector<size_t> group;
            for(size_t m = l; m < walk.legs.size(); m++) {
                if(!done[m] && walk.legs[m].front == leg.front && walk.legs[m].chain.size() == leg.chain.size()) group.push_back(m);
            }
            const int n = (int) leg.chain.size();
            std::vector<float> bone(n, 0.f), moved(n, 0.f);
            float reach = 0.f;
            for(size_t m : group) {
                const auto& chain = walk.legs[m].chain;
                for(int c = 0; c + 1 < n; c++) {
                    float length = glm::distance(stand_position[chain[c]], stand_position[chain[c + 1]]);
                    bone[c] += length / (float) group.size();
                    reach += length / (float) group.size();
                    moved[c] += turn[chain[c]] / (float) group.size();
                }
            }
            std::vector<char> kept(n, 1);
            int count = n;
            for(int c = 0; c + 1 < n && count > 3; c++) {
                if(bone[c] < 0.06f * reach || moved[c] < 0.1f) {
                    kept[c] = 0;
                    count--;
                }
            }
            while(count > std::max(options.max_leg_bodies, 3)) {
                int least = -1;
                for(int c = 0; c + 1 < n; c++) {
                    if(kept[c] && (least < 0 || bone[c] * moved[c] < bone[least] * moved[least])) least = c;
                }
                if(least < 0) break;
                kept[least] = 0;
                count--;
            }
            for(size_t m : group) {
                done[m] = 1;
                for(int c = 0; c < n; c++) {
                    if(!kept[c]) continue;
                    is_body[walk.legs[m].chain[c]] = 1;
                    leg_bodies[m].push_back(walk.legs[m].chain[c]);
                }
            }
        }
    }


    // The trunk: the lowest joint that has the legs and all the skin under it

    std::vector<double> under_skin(joints, 0.);
    for(int j = joints - 1; j >= 0; j--) {
        under_skin[j] += own[j].w;
        if(parents[j] >= 0) under_skin[parents[j]] += under_skin[j];
    }
    int root = -1;
    for(int j = joints - 1; j >= 0; j--) {
        if(leg_of[j] >= 0 || under_skin[j] < 0.995 * skin_total) continue;
        bool all_legs = true;
        for(auto& leg : walk.legs) if(!isUnder(leg.chain[0], j)) all_legs = false;
        if(all_legs) {
            root = j;
            break;
        }
    }
    if(root < 0) {
        error = "no joint carries the legs and the skin";
        return json();
    }
    is_body[root] = 1;


    // Everything else: a joint is a body when what it moves is worth it. Each joint starts as its own cluster of
    // skin; the cluster that loses the least by riding on its parent (its part of the mass x the lever from the joint
    // to its centre x how far the joint turns in the clips) is merged, until none is under the threshold and the
    // count fits. So a spine becomes two or three bodies, ears and a jaw ride on the head, a tail stays when it weighs

    std::vector<Moments> cluster(solid);
    std::vector<char> candidate(joints, 0);
    // Skin of joints outside the root's subtree, and of the leg joints that are not bodies, goes where it rides
    auto rideOn = [&](int j) {
        int p = parents[j];
        while(p >= 0 && !is_body[p] && !candidate[p]) p = parents[p];
        return p >= 0 ? p : root;
    };
    for(int j = 0; j < joints; j++) {
        if(j == root || is_body[j]) continue;
        if(leg_of[j] < 0 && isUnder(j, root)) candidate[j] = 1;
    }
    for(int j = joints - 1; j >= 0; j--) {
        if(is_body[j] || candidate[j]) continue;
        int to = rideOn(j);
        cluster[to] += cluster[j];
        cluster[j] = Moments();
    }
    auto loss = [&](int j) {
        if(cluster[j].w <= 0.) return 0.;
        double lever = glm::length(cluster[j].mean() - glm::dvec3(bind_position[j])) / size;
        return cluster[j].w * lever * turn[j];
    };
    const double volume_total = volume;
    auto bodyCount = [&]() {
        int n = 0;
        for(int j = 0; j < joints; j++) n += is_body[j] || candidate[j];
        return n;
    };
    json merged = json::array();
    while(true) {
        int least = -1;
        double least_loss = 0.;
        for(int j = 0; j < joints; j++) {
            if(!candidate[j]) continue;
            double l = 0.5 * (loss(j) + (candidate[mirror[j]] ? loss(mirror[j]) : loss(j))) / volume_total;
            if(least < 0 || l < least_loss) {
                least = j;
                least_loss = l;
            }
        }
        if(least < 0) break;
        const bool empty_root = cluster[root].w < 0.006 * volume_total;
        if(least_loss >= options.merge_loss && bodyCount() <= options.max_bodies && !empty_root) break;
        if(empty_root && least_loss >= options.merge_loss && bodyCount() <= options.max_bodies) {
            // A trunk with no skin of its own takes the child that loses the least
            least = -1;
            for(int j = 0; j < joints; j++) {
                if(!candidate[j] || rideOn(j) != root) continue;
                double l = loss(j) / volume_total;
                if(least < 0 || l < least_loss) {
                    least = j;
                    least_loss = l;
                }
            }
            if(least < 0) break;
        }
        for(int j : { least, mirror[least] }) {
            if(!candidate[j]) continue;
            candidate[j] = 0;
            int to = rideOn(j);
            cluster[to] += cluster[j];
            cluster[j] = Moments();
            merged.push_back({ { "joint", species.joint_names[j] }, { "onto", species.joint_names[to] }, { "loss", least_loss } });
        }
    }
    json kept = json::array();
    for(int j = 0; j < joints; j++) {
        if(!candidate[j]) continue;
        is_body[j] = 1;
        kept.push_back({ { "joint", species.joint_names[j] }, { "loss", loss(j) / volume_total }, { "part", cluster[j].w / volume_total }, { "turn", turn[j] } });
    }

    // Segments, parents first (the skeleton's order), and what every joint rides on
    std::vector<int> segment_joint;
    std::vector<int> joint_segment(joints, -1);
    for(int j = 0; j < joints; j++) {
        if(!is_body[j]) continue;
        joint_segment[j] = (int) segment_joint.size();
        segment_joint.push_back(j);
    }
    const int segments = (int) segment_joint.size();
    const int root_segment = joint_segment[root];
    for(int j = 0; j < joints; j++) {
        if(is_body[j]) continue;
        int p = parents[j];
        while(p >= 0 && !is_body[p]) p = parents[p];
        joint_segment[j] = p >= 0 && isUnder(j, root) ? joint_segment[p] : root_segment;
    }
    std::vector<int> segment_parent(segments, -1);
    for(int s = 0; s < segments; s++) {
        if(segment_joint[s] == root) continue;
        int p = parents[segment_joint[s]];
        while(p >= 0 && !is_body[p]) p = parents[p];
        segment_parent[s] = p >= 0 ? joint_segment[p] : root_segment;
    }
    // The root first: the shared build and the agent expect parents before children
    if(root_segment != 0) {
        error = "the trunk is not the first body (a body joint above it)";
        return json();
    }
    std::vector<int> segment_leg(segments, -1);
    std::vector<char> foot(segments, 0), may_touch(segments, 0);
    for(size_t l = 0; l < leg_bodies.size(); l++) {
        for(int j : leg_bodies[l]) segment_leg[joint_segment[j]] = (int) l;
        int f = joint_segment[leg_bodies[l].back()];
        foot[f] = 1;
        may_touch[f] = 1;
        if(leg_bodies[l].size() >= 2) may_touch[joint_segment[leg_bodies[l][leg_bodies[l].size() - 2]]] = 1;
    }


    // Shapes, fitted to the vertices each segment carries, in the segment's frame at the bind pose (its origin at
    // the joint, the model's axes). A capsule where the section is round, a box where it is flat; a foot is a box
    // that lies flat as the animal stands

    std::vector<std::vector<uint32_t>> carried(segments);
    for(size_t v = 0; v < vertices.size(); v++) {
        if(dominant[v] >= 0) carried[joint_segment[dominant[v]]].push_back((uint32_t) v);
    }
    std::vector<Fit> fits(segments);
    std::vector<double> segment_solid(segments, 0.);
    const float thin = 0.012f * height;
    for(int s = 0; s < segments; s++) {
        const int j = segment_joint[s];
        const glm::vec3 origin = bind_position[j];
        Fit& fit = fits[s];
        Moments m;
        for(uint32_t v : carried[s]) m.add(glm::dvec3(vertices[v].position - origin), area[v]);

        // Where the bone goes: to the first segment below it, else to the first joint below it
        glm::vec3 bone(0.f);
        for(int c = 0; c < segments && bone == glm::vec3(0.f); c++) if(segment_parent[c] == s && segment_leg[c] == segment_leg[s]) bone = bind_position[segment_joint[c]] - origin;
        for(int c = j + 1; c < joints && bone == glm::vec3(0.f); c++) if(parents[c] == j) bone = bind_position[c] - origin;

        // What the segment holds of the volume, in its frame
        Moments inside;
        for(int k = 0; k < joints; k++) if(joint_segment[k] == s) inside += solid[k];
        segment_solid[s] = inside.w;
        bool heel = false;
        if(segment_leg[s] >= 0 && may_touch[s] && !foot[s]) {
            float lowest = 1e9f;
            for(uint32_t v : carried[s]) lowest = std::min(lowest, (stand_position[j] + stand_rotation[j] * (vertices[v].position - origin)).y);
            heel = lowest < walk.ground + 0.03f * height;
        }
        if(!foot[s] && !heel && inside.w >= 10. * cell_volume) {
            glm::dvec3 values;
            glm::dmat3 vectors;
            eigen(inside.covariance(), values, vectors);
            glm::vec3 sigma(std::sqrt(std::max(values[0], 0.)), std::sqrt(std::max(values[1], 0.)), std::sqrt(std::max(values[2], 0.)));
            sigma = glm::max(sigma, glm::vec3(0.29f * cell));
            glm::vec3 axes[3] = { glm::normalize(glm::vec3(vectors[0])), glm::normalize(glm::vec3(vectors[1])), glm::vec3(0.f) };
            axes[1] = glm::normalize(axes[1] - axes[0] * glm::dot(axes[0], axes[1]));
            axes[2] = glm::cross(axes[0], axes[1]);
            fit.shape.center = glm::vec3(inside.mean()) - origin;
            const float root3 = 1.7320508f;
            if(sigma.y > 1.6f * sigma.z) {
                // A solid box of these moments
                fit.shape.type = ShapeDef::Type::Box;
                fit.shape.half_extents = glm::max(root3 * sigma, glm::vec3(thin));
                fit.shape.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
            } else {
                // A solid cylinder of these moments, its ends rounded
                float radius = std::max(std::sqrt(2.f * (sigma.y * sigma.y + sigma.z * sigma.z)), thin);
                float half_height = root3 * sigma.x - 0.6f * radius;
                if(half_height < 0.15f * radius) {
                    fit.shape.type = ShapeDef::Type::Sphere;
                    fit.shape.radius = std::max(std::sqrt(5.f * (sigma.x * sigma.x + sigma.y * sigma.y + sigma.z * sigma.z) / 3.f), thin);
                } else {
                    fit.shape.type = ShapeDef::Type::Capsule;
                    fit.shape.radius = radius;
                    fit.shape.half_height = half_height;
                    fit.shape.rotation = between(glm::vec3(0.f, 1.f, 0.f), axes[0]);
                }
            }
            finishFit(fit);
            continue;
        }

        if(m.w < 1e-9 * skin_total || carried[s].size() < 4) {
            // No skin of its own (a bone inside the skin of its neighbours): a capsule along the bone
            float length = std::max(glm::length(bone), 0.04f * height);
            glm::vec3 along = glm::length(bone) > 1e-6f ? glm::normalize(bone) : glm::vec3(0.f, -1.f, 0.f);
            fit.shape.type = ShapeDef::Type::Capsule;
            fit.shape.radius = std::max(0.12f * length, thin);
            fit.shape.half_height = std::max(0.5f * length - fit.shape.radius, 0.1f * fit.shape.radius);
            fit.shape.center = along * 0.5f * length;
            fit.shape.rotation = between(glm::vec3(0.f, 1.f, 0.f), along);
            finishFit(fit);
            continue;
        }

        bool sole = foot[s];
        if(!sole && segment_leg[s] >= 0 && may_touch[s]) {
            // The segment above the foot stands on the ground too (a heel: the foot of the gait analysis is the toes)
            float lowest = 1e9f;
            for(uint32_t v : carried[s]) lowest = std::min(lowest, (stand_position[j] + stand_rotation[j] * (vertices[v].position - origin)).y);
            sole = lowest < walk.ground + 0.03f * height;
        }
        if(sole) {
            // Axis aligned as the animal stands, its lowest vertex on the sole
            glm::quat standing = stand_rotation[j];
            std::vector<std::pair<float, float>> xs, ys, zs;
            float lowest = 1e9f;
            for(uint32_t v : carried[s]) {
                glm::vec3 p = standing * (vertices[v].position - origin);
                xs.emplace_back(p.x, area[v]);
                ys.emplace_back(p.y, area[v]);
                zs.emplace_back(p.z, area[v]);
                lowest = std::min(lowest, p.y);
            }
            glm::vec3 lo(quantile(xs, 0.02f), lowest, quantile(zs, 0.02f)), hi(quantile(xs, 0.98f), quantile(ys, 0.98f), quantile(zs, 0.98f));
            glm::vec3 half = glm::max(0.5f * (hi - lo), glm::vec3(thin));
            glm::vec3 centre = 0.5f * (hi + lo);
            centre.y = lowest + half.y;
            fit.shape.type = ShapeDef::Type::Box;
            fit.shape.half_extents = half;
            fit.shape.rotation = glm::inverse(standing);
            fit.shape.center = fit.shape.rotation * centre;
            finishFit(fit);
            continue;
        }

        glm::dvec3 values;
        glm::dmat3 vectors;
        eigen(m.covariance(), values, vectors);
        glm::vec3 sigma(std::sqrt(std::max(values[0], 0.)), std::sqrt(std::max(values[1], 0.)), std::sqrt(std::max(values[2], 0.)));
        glm::vec3 axes[3] = { glm::normalize(glm::vec3(vectors[0])), glm::normalize(glm::vec3(vectors[1])), glm::vec3(0.f) };
        axes[1] = glm::normalize(axes[1] - axes[0] * glm::dot(axes[0], axes[1]));
        axes[2] = glm::cross(axes[0], axes[1]);
        const glm::vec3 mean(m.mean());
        float lo[3], hi[3];
        for(int a = 0; a < 3; a++) {
            std::vector<std::pair<float, float>> along;
            for(uint32_t v : carried[s]) along.emplace_back(glm::dot(vertices[v].position - origin, axes[a]), area[v]);
            lo[a] = quantile(along, 0.02f);
            hi[a] = quantile(along, 0.98f);
        }
        const bool flat = sigma.y > 1.8f * std::max(sigma.z, 1e-6f);
        if(flat) {
            fit.shape.type = ShapeDef::Type::Box;
            glm::vec3 half(0.f), centre(0.f);
            for(int a = 0; a < 3; a++) {
                half[a] = std::max(0.5f * (hi[a] - lo[a]), thin);
                centre += axes[a] * 0.5f * (hi[a] + lo[a]);
            }
            fit.shape.half_extents = half;
            fit.shape.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
            fit.shape.center = centre;
        } else {
            float radius = std::max(std::sqrt(sigma.y * sigma.y + sigma.z * sigma.z), thin);
            float half_length = 0.5f * (hi[0] - lo[0]);
            glm::vec3 centre = axes[0] * 0.5f * (hi[0] + lo[0]) + axes[1] * glm::dot(mean, axes[1]) + axes[2] * glm::dot(mean, axes[2]);
            fit.shape.center = centre;
            if(half_length - 0.8f * radius < 0.15f * radius) {
                fit.shape.type = ShapeDef::Type::Sphere;
                fit.shape.radius = std::max(radius, half_length);
            } else {
                fit.shape.type = ShapeDef::Type::Capsule;
                fit.shape.radius = radius;
                fit.shape.half_height = half_length - 0.8f * radius;
                fit.shape.rotation = between(glm::vec3(0.f, 1.f, 0.f), axes[0]);
            }
        }
        finishFit(fit);
    }


    // Mass: the volume of the mesh at the density, shared between the segments by the volume of their shapes

    double shape_volume = 0.;
    for(auto& fit : fits) shape_volume += fit.volume;
    const float total_mass = options.mass > 0.f ? options.mass : (float) (volume * options.density);
    // Each segment its part of the volume; one too thin for the grid keeps a part of its shape's
    std::vector<float> mass(segments);
    {
        double sum = 0.;
        std::vector<double> part(segments);
        for(int s = 0; s < segments; s++) {
            part[s] = std::max(segment_solid[s], 0.25 * fits[s].volume * volume / shape_volume);
            // No segment too light for what it carries: the solver does not hold a 300 kg trunk on a 400 g hoof
            part[s] = std::max(part[s], (segment_leg[s] >= 0 ? options.min_leg_mass : options.min_mass) * volume);
            sum += part[s];
        }
        for(int s = 0; s < segments; s++) mass[s] = total_mass * (float) (part[s] / sum);
        // ... nor too thin to be turned about its own axis
        for(auto& fit : fits) {
            float most = std::max({ fit.inertia.x, fit.inertia.y, fit.inertia.z });
            fit.inertia = glm::max(fit.inertia, glm::vec3(options.min_inertia * most));
        }
    }


    // The standing pose on the ground: the lowest sole at y = 0

    std::vector<glm::vec3> spawn_position(segments), offset(segments, glm::vec3(0.f));
    std::vector<glm::quat> spawn_rotation(segments);
    for(int s = 0; s < segments; s++) {
        spawn_rotation[s] = stand_rotation[segment_joint[s]];
        spawn_position[s] = stand_position[segment_joint[s]];
        if(segment_parent[s] >= 0) {
            int p = segment_parent[s];
            offset[s] = glm::inverse(stand_rotation[segment_joint[p]]) * (stand_position[segment_joint[s]] - stand_position[segment_joint[p]]);
        }
    }
    float lowest = 1e9f;
    for(int s = 0; s < segments; s++) {
        if(!(segment_leg[s] >= 0 && may_touch[s] && fits[s].shape.type == ShapeDef::Type::Box)) continue;
        const ShapeDef& shape = fits[s].shape;
        for(int c = 0; c < 8; c++) {
            glm::vec3 corner = shape.center + shape.rotation * (shape.half_extents * glm::vec3(c & 1 ? 1.f : -1.f, c & 2 ? 1.f : -1.f, c & 4 ? 1.f : -1.f));
            lowest = std::min(lowest, (spawn_position[s] + spawn_rotation[s] * corner).y);
        }
    }
    // A body the animal's own walk drags on the ground (a long tail) may touch it, as the feet and the lower legs may
    json drags = json::array();
    {
        for(auto& sample : samples) {
            if(!sample.walk || sample.world.empty()) continue;
            std::vector<glm::vec3> at(segments);
            std::vector<glm::quat> turned(segments);
            // The ground of this pose: where its lowest sole is
            float near = 1e9f;
            for(int s = 0; s < segments; s++) {
                const int j = segment_joint[s];
                turned[s] = glm::normalize(sample.rotation[j] * bind_inverse[j]);
                at[s] = segment_parent[s] < 0 ? glm::vec3(sample.world[j][3]) : at[segment_parent[s]] + turned[segment_parent[s]] * offset[s];
                if(!foot[s]) continue;
                const ShapeDef& shape = fits[s].shape;
                for(int c = 0; c < 8; c++) near = std::min(near, (at[s] + turned[s] * (shape.center + shape.rotation * (shape.half_extents * glm::vec3(c & 1 ? 1.f : -1.f, c & 2 ? 1.f : -1.f, c & 4 ? 1.f : -1.f)))).y);
            }
            for(int s = 0; s < segments; s++) {
                const int j = segment_joint[s];
                turned[s] = glm::normalize(sample.rotation[j] * bind_inverse[j]);
                at[s] = segment_parent[s] < 0 ? glm::vec3(sample.world[j][3]) : at[segment_parent[s]] + turned[segment_parent[s]] * offset[s];
                if(may_touch[s]) continue;
                SegmentDef seg;
                seg.shapes.push_back(fits[s].shape);
                const ShapeDef& shape = fits[s].shape;
                glm::vec3 centre = at[s] + turned[s] * shape.center;
                float low = centre.y;
                if(shape.type == ShapeDef::Type::Box) {
                    for(int c = 0; c < 8; c++) low = std::min(low, (centre + turned[s] * (shape.rotation * (shape.half_extents * glm::vec3(c & 1 ? 1.f : -1.f, c & 2 ? 1.f : -1.f, c & 4 ? 1.f : -1.f)))).y);
                } else {
                    float along = shape.type == ShapeDef::Type::Capsule ? std::abs((turned[s] * (shape.rotation * glm::vec3(0.f, shape.half_height, 0.f))).y) : 0.f;
                    low = centre.y - along - shape.radius;
                }
                // A leg: when it comes down to the ground. Anything else: when it comes near (a wing tip, a belly)
                if(low < near + (s == 0 ? 0.08f : segment_leg[s] >= 0 ? 0.015f : 0.05f) * height) {
                    may_touch[s] = 1;
                    drags.push_back(species.joint_names[j]);
                }
            }
        }
    }

    const float lift = 0.001f - lowest;
    for(auto& p : spawn_position) p.y += lift;
    glm::dvec3 com(0.);
    for(int s = 0; s < segments; s++) com += glm::dvec3(spawn_position[s] + spawn_rotation[s] * fits[s].shape.center) * (double) mass[s];
    com /= (double) total_mass;
    const float com_height = std::max((float) com.y, 0.05f * height);
    const float stand_height = spawn_position[0].y;


    // Joints: the axes and the limits of each from the range it covers. The reference of the constraint is the
    // middle of that range; a joint that turns about one axis is a hinge about it (the twist), the others twist
    // about their bone and swing in a cone

    struct Made {
        glm::quat middle = glm::quat(1.f, 0.f, 0.f, 0.f);  // child in parent at the constraint's reference
        glm::quat frame = glm::quat(1.f, 0.f, 0.f, 0.f);   // constraint space in the child's frame
        float twist_min = 0.f, twist_max = 0.f, swing_y = 0.f, swing_z = 0.f;
        bool hinge = false;
        glm::vec3 stand = glm::vec3(0.f);
        float spread = 0.f;
    };
    std::vector<Made> made(segments);
    for(int s = 1; s < segments; s++) {
        const int j = segment_joint[s], pj = segment_joint[segment_parent[s]];
        Made& out = made[s];
        std::vector<glm::quat> relative(samples.size());
        for(size_t n = 0; n < samples.size(); n++) {
            glm::quat parent = samples[n].rotation[pj] * bind_inverse[pj], child = samples[n].rotation[j] * bind_inverse[j];
            relative[n] = glm::normalize(glm::inverse(parent) * child);
        }
        // The mean rotation, then the deviations from it as rotation vectors
        glm::quat mean = relative[0];
        for(int iteration = 0; iteration < 4; iteration++) {
            glm::vec3 sum(0.f);
            for(auto& q : relative) sum += logOf(glm::inverse(mean) * q);
            mean = glm::normalize(mean * expOf(sum / (float) relative.size()));
        }
        glm::dmat3 covariance(0.);
        for(auto& q : relative) {
            glm::dvec3 v(logOf(glm::inverse(mean) * q));
            covariance += glm::outerProduct(v, v);
        }
        covariance /= (double) relative.size();
        glm::dvec3 values;
        glm::dmat3 vectors;
        eigen(covariance, values, vectors);
        const float s1 = (float) std::sqrt(std::max(values[0], 0.)), s2 = (float) std::sqrt(std::max(values[1], 0.));
        out.spread = s1;
        out.hinge = s1 > 2.f * deg && s2 < 0.3f * s1 && s2 < 10.f * deg;

        glm::vec3 bone = fits[s].shape.center;
        bone = glm::length(bone) > 1e-5f ? glm::normalize(bone) : glm::vec3(0.f, -1.f, 0.f);
        glm::vec3 main = glm::normalize(glm::vec3(vectors[0]));
        glm::vec3 x, y;
        if(out.hinge) {
            x = main;
            y = bone - x * glm::dot(bone, x);
        } else {
            x = bone;
            y = main - x * glm::dot(main, x);
        }
        if(glm::length(y) < 1e-3f) {
            glm::vec3 other = std::abs(x.x) < 0.9f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f);
            y = other - x * glm::dot(other, x);
        }
        y = glm::normalize(y);
        out.frame = glm::normalize(glm::quat_cast(glm::mat3(x, y, glm::cross(x, y))));

        // Twist and swing of every sample; the reference moved to the middle of the swings
        auto measure = [&](std::vector<JointAngles>& angles) {
            angles.resize(relative.size());
            for(size_t n = 0; n < relative.size(); n++) angles[n] = decompose(glm::inverse(out.frame) * glm::inverse(out.middle) * relative[n] * out.frame);
        };
        // Per component: the whole range of the walks, 99 in 100 of the other clips
        auto range = [&](const std::vector<JointAngles>& angles, float JointAngles::* member, float& lo, float& hi) {
            std::vector<float> others;
            lo = 1e9f;
            hi = -1e9f;
            for(size_t n = 0; n < angles.size(); n++) {
                float v = angles[n].*member;
                if(samples[n].walk) {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                } else others.push_back(v);
            }
            if(!others.empty()) {
                lo = std::min(lo, plainQuantile(others, 0.005f));
                hi = std::max(hi, plainQuantile(others, 0.995f));
            }
        };
        out.middle = mean;
        std::vector<JointAngles> angles;
        for(int iteration = 0; iteration < 6; iteration++) {
            measure(angles);
            float ylo, yhi, zlo, zhi;
            range(angles, &JointAngles::swing_y, ylo, yhi);
            range(angles, &JointAngles::swing_z, zlo, zhi);
            float cy = 0.5f * (ylo + yhi), cz = 0.5f * (zlo + zhi);
            glm::quat shift(std::sqrt(std::max(1.f - cy * cy - cz * cz, 0.f)), 0.f, cy, cz);
            out.middle = glm::normalize(out.middle * out.frame * shift * glm::inverse(out.frame));
        }
        measure(angles);
        float tlo, thi, ylo, yhi, zlo, zhi;
        range(angles, &JointAngles::twist, tlo, thi);
        range(angles, &JointAngles::swing_y, ylo, yhi);
        range(angles, &JointAngles::swing_z, zlo, zhi);
        float sy = std::max(std::abs(ylo), std::abs(yhi)), sz = std::max(std::abs(zlo), std::abs(zhi));
        sy = std::max(sy, std::sin(0.5f * deg));
        sz = std::max(sz, std::sin(0.5f * deg));
        // The cone is an ellipse: wide enough for the samples in its corners (the walks, and the others but for 1 in 100)
        float corner = 1.f;
        {
            std::vector<float> others;
            for(size_t n = 0; n < angles.size(); n++) {
                float r = std::sqrt(angles[n].swing_y * angles[n].swing_y / (sy * sy) + angles[n].swing_z * angles[n].swing_z / (sz * sz));
                if(samples[n].walk) corner = std::max(corner, r);
                else others.push_back(r);
            }
            if(!others.empty()) corner = std::max(corner, plainQuantile(others, 0.99f));
        }
        auto margin = [&](float range_radians) { return options.limit_margin * deg + options.limit_margin_part * range_radians; };
        float swing_y = 2.f * std::asin(std::min(sy * corner, 0.999f)), swing_z = 2.f * std::asin(std::min(sz * corner, 0.999f));
        out.swing_y = std::min(swing_y + margin(2.f * swing_y), 170.f * deg);
        out.swing_z = std::min(swing_z + margin(2.f * swing_z), 170.f * deg);
        float twist_margin = margin(thi - tlo);
        out.twist_min = std::max(tlo - twist_margin, -175.f * deg);
        out.twist_max = std::min(thi + twist_margin, 175.f * deg);
        JointAngles standing = angles[0];
        out.stand = glm::vec3(standing.twist, 2.f * std::asin(std::clamp(standing.swing_y, -1.f, 1.f)), 2.f * std::asin(std::clamp(standing.swing_z, -1.f, 1.f)));
    }

    // The constraints' reference pose: every joint in the middle of its range, from the trunk as it stands
    std::vector<glm::vec3> rest_position(segments);
    std::vector<glm::quat> rest_rotation(segments);
    rest_position[0] = spawn_position[0];
    rest_rotation[0] = spawn_rotation[0];
    for(int s = 1; s < segments; s++) {
        int p = segment_parent[s];
        rest_rotation[s] = glm::normalize(rest_rotation[p] * made[s].middle);
        rest_position[s] = rest_position[p] + rest_rotation[p] * offset[s];
    }


    // Drives, by what each joint carries. A joint between a foot and the trunk carries the animal: the man's gains,
    // scaled by weight x height of the centre of mass. The others carry what hangs from them, at its lever

    std::vector<double> below_mass(segments, 0.);
    std::vector<glm::dvec3> below_moment(segments, glm::dvec3(0.));
    std::vector<char> carries(segments, 0);
    for(int s = segments - 1; s >= 0; s--) {
        below_mass[s] += mass[s];
        below_moment[s] += glm::dvec3(spawn_position[s] + spawn_rotation[s] * fits[s].shape.center) * (double) mass[s];
        if(foot[s]) carries[s] = 1;
        if(segment_parent[s] >= 0) {
            below_mass[segment_parent[s]] += below_mass[s];
            below_moment[segment_parent[s]] += below_moment[s];
            if(carries[s]) carries[segment_parent[s]] = 1;
        }
    }
    const float g = 9.81f;
    // On two legs the animal balances on its joints: in series they must still hold it up, each is that many times
    // stiffer. On four it stands on its feet whatever the joints give
    float series = 1.f;
    if(walk.legs.size() == 2) series = (float) std::max(leg_bodies[0].size(), leg_bodies[1].size());
    const float support_stiffness = options.stiffness * series * total_mass * g * com_height;
    const float time_scale = std::sqrt(com_height / 1.03f);
    const float damping_time = options.damping_time * time_scale;


    std::vector<float> stiffness(segments, 0.f);
    for(int s = 1; s < segments; s++) {
        if(carries[s]) {
            stiffness[s] = support_stiffness;
        } else {
            glm::dvec3 centre = below_moment[s] / std::max(below_mass[s], 1e-9);
            float lever = std::max((float) glm::length(centre - glm::dvec3(spawn_position[s])), 0.02f * height);
            stiffness[s] = std::clamp(options.appendage_stiffness * (float) below_mass[s] * g * lever, 0.02f * support_stiffness, support_stiffness);
        }
    }
    const int substeps = std::clamp((int) std::ceil(0.7f / time_scale), std::max(options.min_substeps, 1), std::max(options.min_substeps, 4));
    // A drive much stiffer than the bodies it joins are heavy to turn is beyond what the solver resolves in its few
    // iterations: the leg of a deer buckles under gains that would hold it. The moments of inertia of both bodies are
    // raised to what the drive can be solved against in one step (their masses are not touched)
    std::vector<glm::vec3> inertia(segments);
    for(int s = 0; s < segments; s++) inertia[s] = fits[s].inertia * mass[s];
    json raised = json::array();
    if(options.inertia_condition > 0.f) {
        const float dt = 1.f / (60.f * (float) substeps);
        for(int s = 1; s < segments; s++) {
            const float needed = (stiffness[s] * damping_time * dt + stiffness[s] * dt * dt) / options.inertia_condition;
            for(int body : { s, segment_parent[s] }) {
                glm::vec3 before = inertia[body];
                inertia[body] = glm::max(inertia[body], glm::vec3(needed));
                if(inertia[body] != before && body == s) raised.push_back({ { "segment", species.joint_names[segment_joint[body]] }, { "from", std::min({ before.x, before.y, before.z }) }, { "to", needed } });
            }
        }
    }


    // The file

    json out;
    out["format_version"] = 1;
    out["kind"] = "animal_body";
    out["frame"] = "engine";
    out["species"] = species.id;
    out["name"] = species.id;
    out["behavior"] = "Animal_" + species.id;
    out["generator"] = { { "version", 1 }, { "options", options.json() } };
    out["stand_clip"] = stand_clip;
    out["walk_clip"] = species.clips[walk.clip].name;
    out["total_mass"] = total_mass;
    out["height"] = height;
    out["length"] = entry.length;
    out["com_height"] = com_height;
    out["stand_height"] = stand_height;
    out["walk_speed"] = species.clips[walk.clip].speed > 0.02f ? species.clips[walk.clip].speed : walk.speed;
    out["walk_period"] = walk.period;
    {
        // The run, when the animal has one whose legs the procedural walk reads
        float run_speed = 0.f;
        std::string run_clip;
        if(species.run >= 0 && species.run != walk.clip) {
            GaitModel run = analyseGait(species, species.run);
            const float speed = species.clips[species.run].speed > 0.02f ? species.clips[species.run].speed : run.speed;
            if(run.valid && speed > out["walk_speed"].get<float>()) {
                run_speed = speed;
                run_clip = species.clips[species.run].name;
            }
        }
        out["run_speed"] = run_speed;
        out["run_clip"] = run_clip;
    }
    out["friction"] = options.friction;
    // Time runs as the square root of the size: a small animal turns faster and needs shorter steps
    out["physics_hz"] = 60;
    out["control_hz"] = 30;
    out["substeps"] = substeps;
    // On two legs the balance is the solver's to find between the soles and the joints: four times the iterations
    // (with 10 a man standing on stiff joints rocks back on his heels and falls)
    const bool two_legs = walk.legs.size() == 2;
    out["velocity_steps"] = two_legs ? options.biped_velocity_steps : options.velocity_steps;
    out["position_steps"] = two_legs ? options.biped_position_steps : options.position_steps;
    // The solver lets bodies sink this far before it pushes them out: with Jolt's 2 cm a man's heels dig in until he
    // tips over backwards
    out["penetration_slop"] = std::clamp(0.0025f * height, 0.0005f, 0.02f);
    out["speculative_distance"] = std::min(0.02f, 0.02f * height);
    out["max_angular_velocity"] = std::min(20.f / time_scale, 60.f);
    out["max_target_angular_velocity"] = std::min(1.0472f / time_scale, 4.f);
    out["volume"] = { { "used", volume }, { "signed_triangles", mesh_volume }, { "shapes", shape_volume }, { "cell", cell } };

    json segs = json::array();
    for(int s = 0; s < segments; s++) {
        const int j = segment_joint[s];
        json seg;
        seg["name"] = species.joint_names[j];
        seg["joint"] = j;
        seg["parent"] = segment_parent[s];
        seg["leg"] = segment_leg[s];
        seg["role"] = s == 0 ? "trunk" : foot[s] ? "foot" : segment_leg[s] >= 0 ? (may_touch[s] ? "lower_leg" : "leg") : may_touch[s] ? "drags" : "body";
        seg["may_touch"] = (bool) may_touch[s];
        seg["mass"] = mass[s];
        seg["rest_position"] = toJson(rest_position[s]);
        seg["rest_rotation"] = toJson(rest_rotation[s]);
        seg["spawn_position"] = toJson(spawn_position[s]);
        seg["spawn_rotation"] = toJson(spawn_rotation[s]);
        seg["offset"] = toJson(offset[s]);
        seg["inertia"] = toJson(inertia[s]);
        seg["shape"] = shapeJson(fits[s].shape);
        seg["vertices"] = carried[s].size();
        segs.push_back(seg);
    }
    out["segments"] = segs;

    json jts = json::array();
    for(int s = 1; s < segments; s++) {
        const Made& m = made[s];
        const int p = segment_parent[s];
        json jt;
        jt["name"] = species.joint_names[segment_joint[s]];
        jt["parent"] = p;
        jt["child"] = s;
        jt["anchor"] = toJson(rest_position[s]);
        glm::quat frame = glm::normalize(rest_rotation[s] * m.frame);
        jt["twist_axis"] = toJson(frame * glm::vec3(1.f, 0.f, 0.f));
        jt["normal_axis"] = toJson(frame * glm::vec3(0.f, 1.f, 0.f));
        jt["plane_axis"] = toJson(frame * glm::vec3(0.f, 0.f, 1.f));
        jt["twist_min"] = m.twist_min;
        jt["twist_max"] = m.twist_max;
        jt["swing_y"] = m.swing_y;
        jt["swing_z"] = m.swing_z;
        jt["hinge"] = m.hinge;
        jt["stand"] = toJson(m.stand);
        jt["limits_deg"] = { m.twist_min / deg, m.twist_max / deg, m.swing_y / deg, m.swing_z / deg };
        jt["support"] = (bool) carries[s];
        jt["stiffness"] = stiffness[s];
        jt["damping"] = stiffness[s] * damping_time;
        jt["max_torque"] = stiffness[s] * options.torque_angle;
        jts.push_back(jt);
    }
    out["joints"] = jts;

    json legs = json::array();
    for(size_t l = 0; l < leg_bodies.size(); l++) {
        json leg;
        json list = json::array();
        for(int j : leg_bodies[l]) list.push_back(joint_segment[j]);
        leg["segments"] = list;
        leg["front"] = walk.legs[l].front;
        leg["left"] = walk.legs[l].left;
        legs.push_back(leg);
    }
    out["legs"] = legs;
    out["skeleton"] = joint_segment;
    out["merged"] = merged;
    out["kept"] = kept;
    out["inertia_raised"] = raised;
    out["drags"] = drags;
    return out;

}



std::shared_ptr<const AnimalBody> dy::parseAnimalBody(const json& j, const std::string& hash) {

    auto out = std::make_shared<AnimalBody>();
    AnimalBody& b = *out;
    b.species = j.value("species", "");
    const std::string where = "animal body " + b.species;
    if(j.value("format_version", 0) != 1 || j.value("kind", "") != "animal_body") throw AssetError(where + ": not an animal body of format 1");
    if(j.value("frame", "") != "engine") throw AssetError(where + ": expected frame engine");
    b.behavior = j.value("behavior", "Animal_" + b.species);
    b.stand_clip = j.value("stand_clip", "");
    b.file_hash = hash;
    b.mass = j.at("total_mass").get<float>();
    b.height = j.value("height", 1.f);
    b.length = j.value("length", 1.f);
    b.com_height = j.value("com_height", 1.f);
    b.stand_height = j.value("stand_height", 1.f);
    b.walk_speed = j.value("walk_speed", 1.f);
    b.run_speed = j.value("run_speed", 0.f);
    b.run_clip = j.value("run_clip", "");
    b.max_angular_velocity = j.value("max_angular_velocity", 20.f);
    b.max_target_angular_velocity = j.value("max_target_angular_velocity", 1.0472f);
    b.substeps = j.value("substeps", 1);

    BodyDef& d = b.def;
    d.name = b.species;
    d.file_hash = hash;
    d.total_mass = b.mass;
    d.height = b.height;
    d.friction = j.value("friction", 1.f);
    d.restitution = 0.f;

    for(auto& s : j.at("segments")) {
        SegmentDef seg;
        seg.name = s.value("name", "");
        seg.parent = s.at("parent").get<int>();
        seg.role = s.value("role", "");
        if(seg.parent >= (int) d.segments.size()) throw AssetError(where + ": parent after child at " + seg.name);
        seg.rest_position = vec3From(s.at("rest_position"));
        seg.rest_rotation = quatFrom(s.at("rest_rotation"));
        seg.spawn_position = vec3From(s.at("spawn_position"));
        seg.spawn_rotation = quatFrom(s.at("spawn_rotation"));
        seg.percentage = 100.f * s.at("mass").get<float>() / b.mass;
        seg.inertia = vec3From(s.at("inertia"));
        const json& sh = s.at("shape");
        ShapeDef shape;
        const std::string type = sh.value("type", "");
        shape.center = vec3From(sh.at("center"));
        shape.rotation = quatFrom(sh.at("rotation"));
        if(type == "capsule") {
            shape.type = ShapeDef::Type::Capsule;
            shape.radius = sh.at("radius").get<float>();
            shape.half_height = sh.at("half_height").get<float>();
        } else if(type == "sphere") {
            shape.type = ShapeDef::Type::Sphere;
            shape.radius = sh.at("radius").get<float>();
        } else if(type == "box") {
            shape.type = ShapeDef::Type::Box;
            shape.half_extents = vec3From(sh.at("half_extents"));
            // Nearly sharp: a sole rounded as much as it is thick is a rocker, and an animal on two of them falls over
            shape.convex_radius = std::min(0.02f, 0.1f * std::min({ shape.half_extents.x, shape.half_extents.y, shape.half_extents.z }));
        } else throw AssetError(where + ": unknown shape type " + type);
        seg.center_of_mass = shape.center;
        seg.inertia_rotation = shape.rotation;
        seg.shapes.push_back(shape);
        b.segment_joint.push_back(s.value("joint", -1));
        b.offset.push_back(vec3From(s.at("offset")));
        b.may_touch.push_back(s.value("may_touch", false) ? 1 : 0);
        b.is_foot.push_back(seg.role == "foot" ? 1 : 0);
        d.segments.push_back(std::move(seg));
    }
    if(d.segments.empty() || d.segments[0].parent != -1) throw AssetError(where + ": the first segment must be the trunk");
    b.root = 0;
    b.segment_parent_joint.assign(d.segments.size(), -1);

    for(auto& jd : j.at("joints")) {
        JointDef joint;
        joint.name = jd.value("name", "");
        joint.parent = jd.at("parent").get<int>();
        joint.child = jd.at("child").get<int>();
        if(joint.child <= 0 || joint.child >= (int) d.segments.size() || d.segments[joint.child].parent != joint.parent) throw AssetError(where + ": joint " + joint.name + " does not join a child to its parent");
        joint.anchor = vec3From(jd.at("anchor"));
        joint.twist_axis = glm::normalize(vec3From(jd.at("twist_axis")));
        // The shared build names "plane axis" the one Jolt keeps as its Z: swing_y turns about normal = plane x twist
        joint.plane_axis = glm::normalize(vec3From(jd.at("plane_axis")));
        joint.twist_min = jd.at("twist_min").get<float>();
        joint.twist_max = jd.at("twist_max").get<float>();
        joint.swing_y = jd.at("swing_y").get<float>();
        joint.swing_z = jd.at("swing_z").get<float>();
        glm::quat child = glm::inverse(d.segments[joint.child].rest_rotation);
        joint.unity_axis = child * joint.twist_axis;
        joint.unity_secondary = child * joint.plane_axis;
        AnimalJointDrive drive;
        drive.stiffness = jd.at("stiffness").get<float>();
        drive.damping = jd.at("damping").get<float>();
        drive.max_torque = jd.at("max_torque").get<float>();
        drive.hinge = jd.value("hinge", false);
        drive.support = jd.value("support", false);
        b.segment_parent_joint[joint.child] = (int) d.joints.size();
        d.joints.push_back(joint);
        b.drives.push_back(drive);
    }
    if(d.joints.size() + 1 != d.segments.size()) throw AssetError(where + ": every segment but the trunk needs one joint");

    for(auto& l : j.at("legs")) {
        AnimalLegDef leg;
        leg.segments = l.at("segments").get<std::vector<int>>();
        leg.front = l.value("front", false);
        leg.left = l.value("left", false);
        b.legs.push_back(std::move(leg));
    }
    b.joint_segment = j.at("skeleton").get<std::vector<int>>();

    // Constraint space in each body's frame, Jolt's axes: X twist, Y normal = plane x twist, Z plane
    b.frame1.resize(d.joints.size());
    b.frame2.resize(d.joints.size());
    for(size_t n = 0; n < d.joints.size(); n++) {
        const JointDef& joint = d.joints[n];
        glm::vec3 x = joint.twist_axis, z = glm::normalize(joint.plane_axis - x * glm::dot(joint.plane_axis, x));
        glm::vec3 y = glm::cross(z, x);
        glm::quat frame = glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
        b.frame1[n] = glm::normalize(glm::inverse(d.segments[joint.parent].rest_rotation) * frame);
        b.frame2[n] = glm::normalize(glm::inverse(d.segments[joint.child].rest_rotation) * frame);
    }
    for(size_t n = 0; n < d.joints.size(); n++) {
        const JointDef& joint = d.joints[n];
        JointAngles a = jointAngles(b, (int) n, d.segments[joint.parent].spawn_rotation, d.segments[joint.child].spawn_rotation);
        // The swing as a rotation vector: an angle about an axis in the Y Z plane
        float sine = std::sqrt(a.swing_y * a.swing_y + a.swing_z * a.swing_z);
        float scale = sine > 1e-7f ? 2.f * std::asin(std::min(sine, 1.f)) / sine : 2.f;
        b.drives[n].stand = glm::vec3(a.twist, a.swing_y * scale, a.swing_z * scale);
    }
    return out;

}

std::shared_ptr<const AnimalBody> dy::loadAnimalBody(const std::string& path) {
    json j;
    try {
        j = json::parse(readFile(path));
    } catch(const json::exception& e) {
        throw AssetError(path + ": " + e.what());
    }
    return parseAnimalBody(j, fileSha256(path));
}

std::filesystem::path dy::animalBodyFile(const std::string& species) {
    return libraryResourceFile("animals/bodies/" + species + ".body.json");
}

std::string dy::animalBodyPath(const std::string& given) {
    std::error_code ec;
    if(std::filesystem::exists(given, ec) && !std::filesystem::is_directory(given, ec)) return given;
    // A species id: the library's body
    if(given.find('/') == std::string::npos && given.find('\\') == std::string::npos && given.find(".json") == std::string::npos)
        return libraryResourceFile("animals/bodies/" + given + ".body.json").string();
    return libraryResourceFile(given).string();
}

JointAngles dy::jointAngles(const AnimalBody& body, int joint, glm::quat parent, glm::quat child) {
    // Jolt: q = c1^-1 * R1^-1 * R2 * c2
    glm::quat q = glm::inverse(body.frame1[joint]) * glm::inverse(parent) * child * body.frame2[joint];
    return decompose(glm::normalize(q));
}

float dy::limitExcess(const AnimalBody& body, int joint, const JointAngles& a) {
    const JointDef& j = body.def.joints[joint];
    float excess = std::max({ j.twist_min - a.twist, a.twist - j.twist_max, 0.f });
    // The cone: an ellipse on the swing quaternion's y and z, half axes sin(limit / 2)
    float sy = std::sin(0.5f * j.swing_y), sz = std::sin(0.5f * j.swing_z);
    float r = std::sqrt(a.swing_y * a.swing_y / (sy * sy) + a.swing_z * a.swing_z / (sz * sz));
    if(r > 1.f) {
        float swing = 2.f * std::asin(std::min(std::sqrt(a.swing_y * a.swing_y + a.swing_z * a.swing_z), 1.f));
        excess = std::max(excess, swing * (1.f - 1.f / r));
    }
    return excess;
}

AnimalBodyBind dy::animalBodyBind(const AnimalBody& body, const AnimalSpecies& species) {
    AnimalBodyBind out;
    species.worldMatrices(species.rest, out.bind);
    out.rotation_inverse.resize(out.bind.size());
    for(size_t j = 0; j < out.bind.size(); j++) out.rotation_inverse[j] = glm::inverse(matrixRotation(out.bind[j]));
    // The standing pose the body was made from
    out.stand = out.bind;
    if(!body.stand_clip.empty()) {
        std::string name = body.stand_clip;
        float time = 0.f;
        if(auto at = name.find(" @ "); at != std::string::npos) {
            time = std::stof(name.substr(at + 3));
            name = name.substr(0, at);
        }
        int clip = species.findClip(name);
        if(clip >= 0) {
            std::vector<JointPose> pose;
            species.sample(clip, time, true, pose);
            species.worldMatrices(pose, out.stand);
        }
    }
    return out;
}

void dy::segmentPoses(const AnimalBody& body, const AnimalBodyBind& bind, const std::vector<glm::mat4>& joints, const glm::mat4& to_world,
    std::vector<glm::vec3>& positions, std::vector<glm::quat>& rotations) {
    const size_t n = body.def.segments.size();
    positions.resize(n);
    rotations.resize(n);
    const glm::quat turn = matrixRotation(to_world);
    for(size_t s = 0; s < n; s++) {
        const int j = body.segment_joint[s];
        rotations[s] = glm::normalize(turn * matrixRotation(joints[j]) * bind.rotation_inverse[j]);
        positions[s] = glm::vec3(to_world * glm::vec4(glm::vec3(joints[j][3]), 1.f));
    }
}

void dy::skeletonFromSegments(const AnimalBody& body, const AnimalBodyBind& bind, const AnimalSpecies& species,
    const std::vector<glm::vec3>& positions, const std::vector<glm::quat>& rotations, std::vector<glm::mat4>& world) {
    const size_t joints = species.parents.size();
    world.resize(joints);
    std::vector<char> is_body(joints, 0);
    for(size_t s = 0; s < body.segment_joint.size(); s++) is_body[body.segment_joint[s]] = 1;
    for(size_t j = 0; j < joints; j++) {
        const int s = body.joint_segment[j];
        const int sj = body.segment_joint[s];
        // Where the segment took its own joint since the bind pose; a joint that is not a body rides on it, placed as
        // it is against that joint in the standing pose
        glm::mat4 moved = glm::translate(glm::mat4(1.f), positions[s]) * glm::mat4_cast(rotations[s]) * glm::translate(glm::mat4(1.f), -glm::vec3(bind.bind[sj][3]));
        world[j] = (int) j == sj ? moved * bind.bind[j] : moved * bind.bind[sj] * glm::inverse(bind.stand[sj]) * bind.stand[j];
    }
}
