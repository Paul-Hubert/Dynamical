#include "ground_field.h"

#include <algorithm>
#include <cmath>

#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"
#include "scene/system_registry.h"

using namespace dy;

namespace {

int floorDiv(int a, int b) {
    return (int) std::floor((float) a / (float) b);
}

}

void PlaneField::setTilt(float pitch, float roll) {
    pitch_degrees = pitch;
    roll_degrees = roll;
    float p = glm::radians(pitch), r = glm::radians(roll);
    n = glm::normalize(glm::vec3(-std::cos(p) * std::sin(r), std::cos(p) * std::cos(r), std::sin(p)));
    version++;
}

std::optional<GroundField::Hit> PlaneField::below(glm::vec3 from, float distance) const {
    float y = origin.y - (n.x * (from.x - origin.x) + n.z * (from.z - origin.z)) / n.y;
    if(y > from.y + 1e-4f || from.y - y > distance) return std::nullopt;
    return Hit { glm::vec3(from.x, y, from.z), n };
}

TerrainField::TerrainField(std::shared_ptr<TerrainGenerator> generator, std::shared_ptr<MarchingCubes> marching_cubes)
    : generator(std::move(generator)), marching_cubes(std::move(marching_cubes)) {}

const TerrainField::Block& TerrainField::block(glm::ivec3 index) const {
    auto it = cache.find(index);
    if(it != cache.end()) return it->second;
    Block& b = cache[index];
    const glm::ivec3 start = index * block_cubes;
    const glm::ivec3 count(block_cubes + 1);
    generator->sample(start, count, b.samples);
    IndexedMesh mesh;
    marching_cubes->mesh(b.samples.data(), count, (float) chunk::cube_size, mesh);
    const glm::vec3 origin = glm::vec3(start) * (float) chunk::cube_size;
    b.triangles.reserve(mesh.indices.size());
    for(uint32_t i : mesh.indices) b.triangles.push_back(origin + mesh.vertices[i]);
    return b;
}

float TerrainField::density(glm::vec3 p) const {
    glm::vec3 g = p / (float) chunk::cube_size;
    glm::ivec3 i = glm::ivec3(glm::floor(g));
    glm::vec3 f = g - glm::vec3(i);
    glm::ivec3 index(floorDiv(i.x, block_cubes), floorDiv(i.y, block_cubes), floorDiv(i.z, block_cubes));
    glm::ivec3 local = i - index * block_cubes;
    const Block& b = block(index);
    const int count = block_cubes + 1;
    auto value = [&](int x, int y, int z) { return b.samples[((size_t) (local.x + x) * count + (local.z + z)) * count + (local.y + y)]; };
    float c00 = glm::mix(value(0, 0, 0), value(1, 0, 0), f.x), c10 = glm::mix(value(0, 1, 0), value(1, 1, 0), f.x);
    float c01 = glm::mix(value(0, 0, 1), value(1, 0, 1), f.x), c11 = glm::mix(value(0, 1, 1), value(1, 1, 1), f.x);
    return glm::mix(glm::mix(c00, c10, f.y), glm::mix(c01, c11, f.y), f.z);
}

std::optional<GroundField::Hit> TerrainField::below(glm::vec3 from, float distance) const {

    const float size = (float) (block_cubes * chunk::cube_size);
    const int bx = (int) std::floor(from.x / size), bz = (int) std::floor(from.z / size);
    const int top = (int) std::floor(from.y / size), bottom = (int) std::floor((from.y - distance) / size);

    for(int by = top; by >= bottom; by--) {
        const Block& b = block(glm::ivec3(bx, by, bz));
        bool found = false;
        Hit best { glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f) };
        for(size_t t = 0; t + 2 < b.triangles.size(); t += 3) {
            const glm::vec3& a = b.triangles[t];
            const glm::vec3& c = b.triangles[t + 1];
            const glm::vec3& d = b.triangles[t + 2];
            // Barycentric coordinates of (x, z) in the triangle seen from above
            float area = (c.x - a.x) * (d.z - a.z) - (d.x - a.x) * (c.z - a.z);
            if(std::abs(area) < 1e-9f) continue;
            float u = ((from.x - a.x) * (d.z - a.z) - (d.x - a.x) * (from.z - a.z)) / area;
            float v = ((c.x - a.x) * (from.z - a.z) - (from.x - a.x) * (c.z - a.z)) / area;
            const float eps = 1e-4f;
            if(u < -eps || v < -eps || u + v > 1.f + eps) continue;
            float y = a.y + u * (c.y - a.y) + v * (d.y - a.y);
            if(y > from.y + 1e-4f || from.y - y > distance) continue;
            if(found && y <= best.point.y) continue;
            glm::vec3 normal = glm::normalize(glm::cross(c - a, d - a));
            if(normal.y < 0.f) normal = -normal;
            best = Hit { glm::vec3(from.x, y, from.z), normal };
            found = true;
        }
        if(found) return best;
    }
    return std::nullopt;

}

GroundField& dy::groundField(entt::registry& reg) {
    auto* field = reg.ctx().find<std::shared_ptr<GroundField>>();
    if(!field) field = &reg.ctx().emplace<std::shared_ptr<GroundField>>(std::make_shared<PlaneField>());
    return **field;
}

GroundPlaneSys::GroundPlaneSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    pitch = params.value("pitch", pitch);
    roll = params.value("roll", roll);
}

void GroundPlaneSys::preinit() {
    reg.ctx().insert_or_assign<std::shared_ptr<GroundField>>(std::make_shared<PlaneField>(pitch, roll));
}

GroundTerrainSys::GroundTerrainSys(entt::registry& reg, const nlohmann::json& params) : System(reg) {
    seed = params.value("seed", seed);
}

void GroundTerrainSys::preinit() {
    std::shared_ptr<TerrainGenerator> generator;
    if(auto* existing = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) generator = *existing;
    else generator = reg.ctx().emplace<std::shared_ptr<TerrainGenerator>>(std::make_shared<TerrainGenerator>(seed));
    reg.ctx().insert_or_assign<std::shared_ptr<GroundField>>(std::make_shared<TerrainField>(generator, std::make_shared<MarchingCubes>()));
}

void dy::registerGroundField() {
    registerSystem("GroundPlane", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<GroundPlaneSys>(reg, params); }, SystemScope::World });
    registerSystem("GroundTerrain", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<GroundTerrainSys>(reg, params); }, SystemScope::World });
}
