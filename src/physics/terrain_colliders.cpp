#include "terrain_colliders.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/for_each.hpp>

#include "logic/terrain/marching_cubes.h"
#include "logic/terrain/terrain_generator.h"

#include "util/log.h"

#include <algorithm>
#include <cstring>
#include <thread>

using namespace dy;

static constexpr int cell_samples = TerrainColliders::cell_cubes + 1;
static constexpr int cells_per_chunk = chunk::cubes / TerrainColliders::cell_cubes;
static_assert(chunk::cubes % TerrainColliders::cell_cubes == 0, "collision cells must tile chunks");

static bool lessCell(const glm::ivec3& a, const glm::ivec3& b) {
    if(a.x != b.x) return a.x < b.x;
    if(a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

static glm::ivec3 floorDiv(glm::ivec3 a, int b) {
    return glm::ivec3(glm::floor(glm::vec3(a) / (float) b));
}

TerrainColliders::TerrainColliders(entt::registry& reg, PhysicsWorld& world) : reg(reg), world(world) {

    marching_cubes = std::make_shared<MarchingCubes>();

    if(auto* gen = reg.ctx().find<std::shared_ptr<TerrainGenerator>>()) {
        generator = *gen;
    } else {
        generator = std::make_shared<TerrainGenerator>();
    }

}

void TerrainColliders::cellsOverlapping(const Bounds& bounds, std::vector<glm::ivec3>& out) const {
    glm::ivec3 low = glm::ivec3(glm::floor(bounds.min / cell_size));
    glm::ivec3 high = glm::ivec3(glm::floor(bounds.max / cell_size));
    for(int x = low.x; x <= high.x; x++)
        for(int y = low.y; y <= high.y; y++)
            for(int z = low.z; z <= high.z; z++)
                out.emplace_back(x, y, z);
}

std::shared_ptr<TerrainColliders::Job> TerrainColliders::makeJob(glm::ivec3 cell) {

    auto job = std::make_shared<Job>();
    job->cell = cell;

    // Reuse the streamed chunk densities when they are there (copied : the chunk may unload meanwhile)
    if(auto* map = reg.ctx().find<ChunkMap>()) {
        glm::ivec3 chunk_pos = floorDiv(cell, cells_per_chunk);
        entt::entity entity = map->get(chunk_pos);
        if(entity != entt::null && reg.valid(entity)) {
            if(reg.all_of<ChunkEmpty>(entity)) {
                job->empty = true;
                job->has_samples = true;
            } else if(auto* volume = reg.try_get<ChunkVolume>(entity)) {
                glm::ivec3 first = (cell - chunk_pos * cells_per_chunk) * cell_cubes + chunk::border;
                job->samples.resize((size_t) cell_samples * cell_samples * cell_samples);
                for(int x = 0; x < cell_samples; x++) {
                    for(int z = 0; z < cell_samples; z++) {
                        const float* src = &volume->values[chunk::index(first.x + x, first.y, first.z + z)];
                        std::memcpy(&job->samples[((size_t) x * cell_samples + z) * cell_samples], src, cell_samples * sizeof(float));
                    }
                }
                job->has_samples = true;
            }
        }
    }

    return job;

}

void TerrainColliders::run(Job& job, const MarchingCubes& mc, const TerrainGenerator& generator) {

    if(!job.empty) {

        if(!job.has_samples) {
            generator.sample(job.cell * cell_cubes, glm::ivec3(cell_samples), job.samples);
        }

        IndexedMesh mesh;
        mc.mesh(job.samples.data(), glm::ivec3(cell_samples), (float) chunk::cube_size, mesh);

        if(mesh.indices.empty()) {
            job.empty = true;
        } else {
            JPH::VertexList vertices;
            vertices.reserve(mesh.vertices.size());
            for(auto& v : mesh.vertices) vertices.push_back(JPH::Float3(v.x, v.y, v.z));

            JPH::IndexedTriangleList triangles;
            triangles.reserve(mesh.indices.size() / 3);
            for(size_t i = 0; i < mesh.indices.size(); i += 3) {
                triangles.push_back(JPH::IndexedTriangle(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2], 0));
            }

            JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
            auto result = settings.Create();
            if(result.HasError()) {
                dy::log(dy::warning) << "Terrain collider : " << result.GetError().c_str() << "\n";
                job.empty = true;
            } else {
                job.shape = result.Get();
                job.triangles = (uint32_t) (mesh.indices.size() / 3);
            }
        }

    }

    job.samples.clear();
    job.samples.shrink_to_fit();
    job.done.store(true);

}

void TerrainColliders::activate(glm::ivec3 position, Cell& cell) {

    if(cell.state == State::Active || cell.state == State::Empty) return;

    // A prefetched cell that is needed now : finish it here
    while(!cell.job->done.load()) {
        std::this_thread::yield();
    }

    if(cell.job->empty || !cell.job->shape) {
        cell.state = State::Empty;
    } else {
        JPH::BodyCreationSettings settings(cell.job->shape, JPH::RVec3(toJolt(glm::vec3(position) * cell_size)), JPH::Quat::sIdentity(),
            JPH::EMotionType::Static, PhysicsLayers::static_layer);
        settings.mFriction = 0.8f;
        cell.body = world.bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        cell.triangles = cell.job->triangles;
        triangle_count += cell.triangles;
        cell.state = State::Active;
    }

    cell.job.reset();

}

void TerrainColliders::update(const std::vector<Bounds>& required, const std::vector<Bounds>& prefetch, double time) {

    auto& executor = reg.ctx().get<tf::Executor>();

    // Required cells, sorted so bodies are always added in the same order (deterministic simulation)

    std::vector<glm::ivec3> needed;
    for(auto& bounds : required) cellsOverlapping(bounds, needed);
    std::sort(needed.begin(), needed.end(), lessCell);
    needed.erase(std::unique(needed.begin(), needed.end()), needed.end());

    std::vector<std::shared_ptr<Job>> immediate;
    for(auto& position : needed) {
        auto [it, inserted] = cells.try_emplace(position);
        if(inserted) {
            it->second.job = makeJob(position);
            immediate.push_back(it->second.job);
        }
        it->second.last_used = time;
    }

    if(!immediate.empty()) {
        auto mc = marching_cubes;
        auto gen = generator;
        tf::Taskflow flow;
        flow.for_each(immediate.begin(), immediate.end(), [mc, gen](std::shared_ptr<Job>& job) {
            run(*job, *mc, *gen);
        });
        executor.run(flow).wait();
    }

    for(auto& position : needed) {
        activate(position, cells[position]);
    }


    // Prefetch along the predicted paths, bodies are only added once required

    std::vector<glm::ivec3> ahead;
    for(auto& bounds : prefetch) cellsOverlapping(bounds, ahead);
    for(auto& position : ahead) {
        auto [it, inserted] = cells.try_emplace(position);
        if(inserted) {
            auto job = makeJob(position);
            it->second.job = job;
            executor.silent_async([job, mc = marching_cubes, gen = generator]() {
                run(*job, *mc, *gen);
            });
        }
        it->second.last_used = std::max(it->second.last_used, time);
    }

    for(auto& [position, cell] : cells) {
        if(cell.state == State::Building && cell.job && cell.job->done.load()) {
            cell.state = State::Built;
        }
    }


    // Evict cells no body came close to for a while

    std::vector<JPH::BodyID> removed;
    for(auto it = cells.begin(); it != cells.end();) {
        auto& cell = it->second;
        bool idle = time - cell.last_used > eviction_delay;
        if(idle && cell.state != State::Building) {
            if(cell.state == State::Active) {
                removed.push_back(cell.body);
                triangle_count -= cell.triangles;
            }
            it = cells.erase(it);
        } else {
            ++it;
        }
    }

    if(!removed.empty()) {
        world.bodies().RemoveBodies(removed.data(), (int) removed.size());
        world.bodies().DestroyBodies(removed.data(), (int) removed.size());
    }

}

void TerrainColliders::clear() {

    std::vector<JPH::BodyID> removed;
    for(auto& [position, cell] : cells) {
        if(cell.job) {
            while(!cell.job->done.load()) std::this_thread::yield();
        }
        if(cell.state == State::Active) removed.push_back(cell.body);
    }
    if(!removed.empty()) {
        world.bodies().RemoveBodies(removed.data(), (int) removed.size());
        world.bodies().DestroyBodies(removed.data(), (int) removed.size());
    }
    cells.clear();
    triangle_count = 0;

}

uint32_t TerrainColliders::activeCells() const {
    uint32_t count = 0;
    for(auto& [position, cell] : cells) {
        if(cell.state == State::Active) count++;
    }
    return count;
}

TerrainColliders::~TerrainColliders() {
    clear();
}
