#include "camera_walk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <queue>
#include <vector>

#include "logic/agents/ground_field.h"
#include "logic/components/camera.h"
#include "logic/components/transform.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "logic/vegetation/forest.h"
#include "logic/vegetation/vegetation.h"
#include "util/log.h"
#include "view/capture.h"

using namespace dy;

namespace {

const float pi = 3.14159265f;

glm::vec2 along(float yaw) {
    return glm::vec2(-std::sin(yaw), -std::cos(yaw));
}

float wrap(float angle) {
    while(angle > pi) angle -= 2.f * pi;
    while(angle < -pi) angle += 2.f * pi;
    return angle;
}

}

CameraWalkSys::CameraWalkSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    distance = params.value("distance", distance);
    speed = params.value("speed", speed);
    eye_height = params.value("eye_height", eye_height);
    sway = params.value("sway", sway);
    search = params.value("search", search);
    auto s = params.find("start");
    auto h = params.find("heading");
    if(s != params.end() && s->is_array() && s->size() == 2 && h != params.end() && h->is_number()) {
        start = glm::vec2(s->at(0).get<float>(), s->at(1).get<float>());
        heading = glm::radians(h->get<float>());
        has_line = true;
    }
}

void CameraWalkSys::preinit() {
    reg.ctx().emplace<CameraWalk>();
}

// The line that best goes from open land through forest to open land again, on gentle ground
void CameraWalkSys::plan() {

    auto* found = reg.ctx().find<std::shared_ptr<ForestField>>();
    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    if(!found || !generator) return;
    const ForestField& field = **found;

    const float node = ForestField::node;
    const int reach = (int) std::ceil((search + distance) / node) + 1;
    const int side = 2 * reach + 1;
    std::vector<float> cover, stand;
    field.nodes(glm::ivec2(-reach), glm::ivec2(side), cover, stand);

    // Slope of the heightfield at each node, degrees
    std::vector<float> heights, slope((size_t) side * side);
    (*generator)->surfaceHeights(glm::ivec2(-reach - 1), glm::ivec2(side + 2), node, heights);
    for(int j = 0; j < side; j++) {
        for(int i = 0; i < side; i++) {
            const float* h = &heights[(size_t) (j + 1) * (side + 2) + i + 1];
            float dx = (h[1] - h[-1]) / (2.f * node), dz = (h[side + 2] - h[-(side + 2)]) / (2.f * node);
            slope[(size_t) j * side + i] = glm::degrees(std::atan(std::sqrt(dx * dx + dz * dz)));
        }
    }

    auto index = [&](glm::vec2 p) {
        int i = std::clamp((int) std::lround(p.x / node) + reach, 0, side - 1), j = std::clamp((int) std::lround(p.y / node) + reach, 0, side - 1);
        return (size_t) j * side + i;
    };

    // Open for the first and the last `lead` metres, forest in between
    const float lead = std::min(45.f, distance * 0.18f);
    const float margin = 20.f;
    struct Line {
        float score;
        glm::vec2 start;
        float heading;
    };
    std::vector<Line> lines;
    const float step = 48.f;
    for(float x = -search; x <= search; x += step) {
        for(float z = -search; z <= search; z += step) {
            if(x * x + z * z > search * search) continue;
            for(int h = 0; h < 16; h++) {
                float yaw = (float) h * pi / 8.f;
                glm::vec2 from(x, z), dir = along(yaw);
                float open = 0.f, densest = 0.f, forest = 0.f, stand_min = 1.f, stand_max = 0.f, steep = 0.f, steepest = 0.f;
                int open_count = 0, forest_count = 0, count = 0;
                for(float s = 0.f; s <= distance; s += node) {
                    size_t at = index(from + dir * s);
                    steep += slope[at];
                    steepest = std::max(steepest, slope[at]);
                    count++;
                    if(s < lead || s > distance - lead) {
                        open += cover[at];
                        densest = std::max(densest, cover[at]);
                        open_count++;
                    } else if(s > lead + margin && s < distance - lead - margin) {
                        forest += cover[at];
                        forest_count++;
                        if(cover[at] > 0.5f) {
                            stand_min = std::min(stand_min, stand[at]);
                            stand_max = std::max(stand_max, stand[at]);
                        }
                    }
                }
                // A walk through a forest first, the best ground second
                if(!open_count || !forest_count || forest / forest_count < 0.8f || open / open_count > 0.15f) continue;
                float score = forest / forest_count - 2.f * open / open_count - densest + 0.6f * std::max(stand_max - stand_min, 0.f)
                    - 0.1f * std::sqrt(x * x + z * z) / search
                    - std::max(steep / count - 5.f, 0.f) / 10.f - std::max(steepest - 14.f, 0.f) / 14.f;
                lines.push_back({ score, from, yaw });
            }
        }
    }
    if(lines.empty()) return;

    // Among the best ones, the gentlest real ground : the heightfield does not have the bumps and the overhangs
    size_t keep = std::min(lines.size(), (size_t) 30);
    std::partial_sort(lines.begin(), lines.begin() + keep, lines.end(), [](const Line& a, const Line& b) { return a.score > b.score; });
    Line best = lines[0];
    float best_score = -1e9f;
    for(size_t i = 0; i < keep; i++) {
        glm::vec2 dir = along(lines[i].heading), aside(dir.y, -dir.x);
        const float stride = 4.f, width = 3.f;
        float steep = 0.f, steepest = 0.f, before = 0.f;
        int count = 0;
        bool first = true, broken = false;
        for(float s = 0.f; s <= distance; s += stride) {
            glm::vec2 p = lines[i].start + dir * s, l = p - aside * width, r = p + aside * width;
            auto centre = ground->top(p.x, p.y), left = ground->top(l.x, l.y), right = ground->top(r.x, r.y);
            if(!centre || !left || !right) { broken = true; break; }
            float across = std::abs(left->point.y - right->point.y) / (2.f * width);
            float ahead = first ? 0.f : std::abs(centre->point.y - before) / stride;
            float here = glm::degrees(std::atan(std::max(across, ahead)));
            steep += here;
            steepest = std::max(steepest, here);
            count++;
            before = centre->point.y;
            first = false;
        }
        if(broken || !count) continue;
        float score = lines[i].score - std::max(steep / count - 8.f, 0.f) / 8.f - std::max(steepest - 22.f, 0.f) / 10.f;
        if(score > best_score) {
            best_score = score;
            best = lines[i];
        }
    }

    start = best.start;
    heading = best.heading;
    has_line = true;
    dy::log(dy::info) << "CameraWalk : from " << start.x << " " << start.y << " heading " << glm::degrees(heading) << " degrees for "
        << distance << " m (score " << best_score << ")\n";

}

float CameraWalkSys::groundHeight(float x, float z, float near_y) const {
    if(ground) {
        if(auto hit = ground->below(glm::vec3(x, near_y + 6.f, z), 40.f)) return hit->point.y;
        if(auto hit = ground->top(x, z)) return hit->point.y;
    }
    if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) return (*generator)->surfaceHeight(x, z);
    return near_y;
}

void CameraWalkSys::place() {
    auto& camera = reg.ctx().get<Camera>();
    camera.position = glm::vec3(position.x, height, position.y);
    // Looks from side to side and a little up and down, along the slope
    camera.yaw = direction + glm::radians(sway) * std::sin((float) time * 2.f * pi / 13.f);
    camera.pitch = std::clamp(0.6f * std::atan(climb) + 0.03f + 0.05f * std::sin((float) time * 2.f * pi / 9.f), -1.f, 1.f);
}

void CameraWalkSys::init() {

    if(auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) {
        ground = std::make_shared<TerrainField>(*generator, std::make_shared<MarchingCubes>());
    }

    if(!has_line && ground) plan();
    if(!has_line) {
        // Straight ahead of the spawn point
        auto& camera = reg.ctx().get<Camera>();
        start = glm::vec2(camera.position.x, camera.position.z);
        heading = camera.yaw;
    }

    position = start;
    direction = heading;
    if(auto hit = ground ? ground->top(start.x, start.y) : std::nullopt) height = hit->point.y + eye_height;
    else height = groundHeight(start.x, start.y, 0.f) + eye_height;

    auto& walk = reg.ctx().get<CameraWalk>();
    walk.start = start;
    walk.heading = heading;
    walk.distance = distance;
    place();

}

// Where the geometry of a species is, around its foot and by height : read once from its most detailed level
const CameraWalkSys::Reach& CameraWalkSys::reach(const VegetationLibrary& library, uint32_t species) {

    auto found = reaches.find(species);
    if(found != reaches.end()) return found->second;
    Reach& reach = reaches[species];

    try {
        VegetationSpeciesDef def = library.loadSpecies(species);
        std::vector<uint32_t> indices;
        auto vertices = library.loadGeometry(def, indices);
        if(def.lods.empty()) return reach;
        const VegetationLod& lod = def.lods[0];
        for(uint32_t v = lod.first_vertex; v < lod.first_vertex + lod.vertex_count && v < vertices.size(); v++) {
            glm::vec3 p = vertices[v].position;
            int bin = (int) std::floor(p.y / Reach::bin);
            if(bin < 0 || bin >= Reach::bins) continue;
            float& radius = reach.radius[bin][Reach::sector(p.x, p.z)];
            radius = std::max(radius, std::sqrt(p.x * p.x + p.z * p.z));
        }
        // A branch between two sectors is in both
        for(auto& ring : reach.radius) {
            auto copy = ring;
            for(int k = 0; k < Reach::sectors; k++) {
                ring[k] = std::max({ copy[k], copy[(k + 1) % Reach::sectors], copy[(k + Reach::sectors - 1) % Reach::sectors] });
            }
        }
        reach.known = true;
    } catch(const std::exception&) {
        // No geometry to read (a library made for a test) : the size of the species will do
    }
    return reach;

}

// The way through the plants : the cheapest path on a grid over a corridor around the line, where the trunks
// are walls and what a plant has at the height of the eye (from its geometry) costs more the deeper the path
// goes through it, then smoothed
void CameraWalkSys::route() {

    const glm::vec2 line = along(heading), aside(line.y, -line.x);
    const float cell = 0.5f, half_width = 36.f, before = 4.f;
    const int ns = (int) std::ceil((distance + 2.f * before) / cell) + 1, nt = 2 * (int) (half_width / cell) + 1;
    const int mid = nt / 2;
    std::vector<float> cost((size_t) ns * nt, 0.f);

    if(auto* library = reg.ctx().find<VegetationLibrary>()) {
        for(auto [entity, transform, plant] : reg.view<const Transform, const Plant>().each()) {
            if(plant.species >= library->species.size()) continue;
            const auto& species = library->species[plant.species];
            const float scale = transform.scale.y;
            float tall = species.height * scale;
            if(tall < 1.2f) continue;
            glm::vec2 to = glm::vec2(transform.position.x, transform.position.z) - start;
            float s = glm::dot(to, line) + before, t = glm::dot(to, aside);
            if(s < -20.f || s > distance + 2.f * before + 20.f || std::abs(t) > half_width + 20.f) continue;

            // How far the plant reaches at the height of the eye, in each direction around it
            const float trunk = 0.03f * tall + 0.45f;
            std::array<float, Reach::sectors> around;
            const Reach& reach_of = reach(*library, plant.species);
            if(reach_of.known) {
                around.fill(0.f);
                int low = std::clamp((int) std::floor((eye_height - 0.7f) / scale / Reach::bin), 0, Reach::bins - 1);
                int high = std::clamp((int) std::floor((eye_height + 0.6f) / scale / Reach::bin), 0, Reach::bins - 1);
                for(int bin = low; bin <= high; bin++) {
                    for(int k = 0; k < Reach::sectors; k++) around[k] = std::max(around[k], reach_of.radius[bin][k] * scale);
                }
            } else {
                // A bush is walked around, a small tree has its branches at eye height, a tall one has them higher up
                float crown = std::min(species.radius * scale, 0.6f * tall + 1.f);
                around.fill(crown * (0.2f + 0.8f * std::clamp((14.f - tall) / 9.f, 0.f, 1.f)));
            }
            float widest = trunk;
            for(float& r : around) {
                r = std::max(r, trunk);
                widest = std::max(widest, r);
            }
            const float clear = 1.2f;       // a little cost this far from the branches : the path keeps its distance
            float far = widest + clear;

            // Plants are turned about the vertical
            const float yaw = 2.f * std::atan2(transform.rotation.y, transform.rotation.w);
            const float cos_yaw = std::cos(yaw), sin_yaw = std::sin(yaw);

            int s0 = std::max((int) std::floor((s - far) / cell), 0), s1 = std::min((int) std::ceil((s + far) / cell), ns - 1);
            int t0 = std::max((int) std::floor((t - far) / cell) + mid, 0), t1 = std::min((int) std::ceil((t + far) / cell) + mid, nt - 1);
            for(int i = s0; i <= s1; i++) {
                for(int j = t0; j <= t1; j++) {
                    glm::vec2 offset = line * (i * cell - s) + aside * ((j - mid) * cell - t);
                    float d = glm::length(offset);
                    if(d >= far) continue;
                    // Back in the space of the model
                    float mx = offset.x * cos_yaw - offset.y * sin_yaw, mz = offset.x * sin_yaw + offset.y * cos_yaw;
                    float branches = around[Reach::sector(mx, mz)];
                    float c = 0.f;
                    if(d < trunk) c = 2000.f;
                    else if(d < branches) c = 30.f + 30.f * (branches - d) / branches;
                    else if(d < branches + clear) c = 12.f * (branches + clear - d) / clear * (branches + clear - d) / clear;
                    float& value = cost[(size_t) i * nt + j];
                    value = std::max(value, c);
                }
            }
        }
    }

    // Steep ground costs too, rock most of all : heights every 2 m
    if(ground) {
        const int coarse = 4;
        const int cs = (ns - 1) / coarse + 2, ct = (nt - 1) / coarse + 2;
        std::vector<float> heights((size_t) cs * ct, 0.f);
        float last = 0.f;
        for(int i = 0; i < cs; i++) {
            for(int j = 0; j < ct; j++) {
                glm::vec2 p = start + line * (i * coarse * cell - before) + aside * ((j * coarse - mid) * cell);
                if(auto hit = ground->top(p.x, p.y)) last = hit->point.y;
                heights[(size_t) i * ct + j] = last;
            }
        }
        for(int i = 0; i + 1 < cs; i++) {
            for(int j = 0; j + 1 < ct; j++) {
                const float* h = &heights[(size_t) i * ct + j];
                float ds = (h[ct] - h[0] + h[ct + 1] - h[1]) * 0.5f, dt = (h[1] - h[0] + h[ct + 1] - h[ct]) * 0.5f;
                float slope = glm::degrees(std::atan(std::sqrt(ds * ds + dt * dt) / (coarse * cell)));
                float steep = std::clamp((slope - 12.f) / 18.f, 0.f, 1.f);
                float c = 10.f * steep * steep + (slope > 34.f ? 40.f : 0.f);
                for(int a = i * coarse; a < std::min((i + 1) * coarse, ns); a++) {
                    for(int b = j * coarse; b < std::min((j + 1) * coarse, nt); b++) cost[(size_t) a * nt + b] += c;
                }
            }
        }
    }

    // Dijkstra from the start of the line to its far end
    const size_t total = (size_t) ns * nt;
    std::vector<float> best(total, 1e30f);
    std::vector<int> from(total, -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    const int origin = (int) std::lround(before / cell) * nt + mid;
    best[origin] = 0.f;
    queue.push({ 0.f, origin });
    int goal = -1;
    while(!queue.empty()) {
        auto [d, at] = queue.top();
        queue.pop();
        if(d > best[at]) continue;
        int i = at / nt, j = at % nt;
        if(i == ns - 1) {
            goal = at;
            break;
        }
        for(int di = -1; di <= 1; di++) {
            for(int dj = -1; dj <= 1; dj++) {
                int ni = i + di, nj = j + dj;
                if((!di && !dj) || ni < 0 || nj < 0 || ni >= ns || nj >= nt) continue;
                int next = ni * nt + nj;
                // Back toward the line, a little
                float step = (di && dj ? 1.41421356f : 1.f) * cell * (1.f + cost[next] + 0.012f * std::abs(nj - mid) * cell);
                if(d + step < best[next]) {
                    best[next] = d + step;
                    from[next] = at;
                    queue.push({ d + step, next });
                }
            }
        }
    }

    path.clear();
    for(int at = goal; at >= 0; at = from[at]) {
        path.push_back(start + line * ((at / nt) * cell - before) + aside * ((at % nt - mid) * cell));
    }
    std::reverse(path.begin(), path.end());
    if(path.size() < 3) {
        path = { start, start + line * distance };
        return;
    }

    // The grid only has 8 directions
    std::vector<glm::vec2> smooth(path.size());
    for(int pass = 0; pass < 60; pass++) {
        smooth.front() = path.front();
        smooth.back() = path.back();
        for(size_t i = 1; i + 1 < path.size(); i++) smooth[i] = 0.5f * path[i] + 0.25f * (path[i - 1] + path[i + 1]);
        path.swap(smooth);
    }

}

void CameraWalkSys::tick(double dt) {

    OPTICK_EVENT();

    auto& walk = reg.ctx().get<CameraWalk>();
    frames++;

    // The plants are counted by the renderer, a frame late; a Forest plants a few chunks a frame
    if(!walk.started && frames > 4 && terrainReady(reg) && vegetationReady(reg)) {
        auto unplanted = reg.view<ChunkVolume>(entt::exclude<ChunkForest, ChunkDestroying>);
        if(!reg.ctx().find<std::shared_ptr<ForestField>>() || unplanted.begin() == unplanted.end()) {
            route();
            walk.started = true;
        }
    }
    if(!walk.started || walk.finished || dt <= 0.) {
        place();
        return;
    }

    const float h = (float) std::min(dt, 0.1);
    time += h;


    // Toward the point of the path some metres ahead

    while(path_at + 1 < path.size() && glm::distance(path[path_at + 1], position) <= glm::distance(path[path_at], position)) path_at++;
    size_t target = path_at;
    float ahead = 0.f;
    while(target + 1 < path.size() && ahead < 4.f) {
        ahead += glm::distance(path[target], path[target + 1]);
        target++;
    }
    glm::vec2 to_target = path[target] - position;
    float wanted = glm::length(to_target) > 0.05f ? std::atan2(-to_target.x, -to_target.y) : direction;

    // Critically damped turn toward it
    const float settle = 0.3f;
    float error = wrap(wanted - direction);
    turn_rate += (error / (settle * settle) - 2.f * turn_rate / settle) * h;
    turn_rate = std::clamp(turn_rate, -glm::radians(70.f), glm::radians(70.f));
    direction = wrap(direction + turn_rate * h);

    glm::vec2 moved = along(direction) * speed * h;
    position += moved;


    // The eye follows the ground without its bumps

    float target_height = groundHeight(position.x, position.y, height - eye_height) + eye_height;
    float follow = 1.f - std::exp(-h / 0.3f);
    float before = height;
    height += (target_height - height) * follow;
    climb += ((height - before) / std::max(glm::length(moved), 1e-4f) - climb) * (1.f - std::exp(-h / 1.2f));

    walk.progress = glm::dot(position - start, along(heading));
    if(walk.progress >= distance || path_at + 2 >= path.size()) walk.finished = true;

    place();

}
