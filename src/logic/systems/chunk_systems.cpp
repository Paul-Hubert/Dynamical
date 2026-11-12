#include "chunk_systems.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <thread>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include "logic/terrain/terrain_generator.h"
#include "logic/components/camera.h"
#include "logic/components/vr.h"
#include "logic/settings/settings.h"
#include "scene/run_args.h"

using namespace dy;

// Chunks generated in parallel at most
constexpr size_t max_jobs = 16;

static std::vector<glm::vec3> loadCenters(entt::registry& reg) {
    std::vector<glm::vec3> centers;
    if(auto* camera = reg.ctx().find<Camera>()) {
        centers.push_back(camera->position);
    }
    auto* vr_input = reg.ctx().find<VRInput>();
    auto* rig = reg.ctx().find<VRRig>();
    if(vr_input && rig && vr_input->session_running) {
        centers.push_back(rig->position);
    }
    return centers;
}

void ChunkManagerSys::preinit() {
    reg.ctx().emplace<ChunkMap>();
}

void ChunkManagerSys::tick(double dt) {

    auto& map = reg.ctx().get<ChunkMap>();
    // Settings only exist in dynamical; headless runs stream chunks with the default distance
    auto* settings = reg.ctx().find<Settings>();
    float view_distance = (float) (settings ? settings->view_distance : Settings::default_view_distance);

    auto centers = loadCenters(reg);
    if(centers.empty()) return;

    auto distance2 = [&](glm::vec3 p) {
        float best = std::numeric_limits<float>::max();
        for(auto& c : centers) best = std::min(best, glm::distance2(p, c));
        return best;
    };

    // Unload with some hysteresis so chunks on the edge do not flicker
    const float unload = view_distance + chunk::length;
    for(auto it = map.chunks.begin(); it != map.chunks.end();) {
        ChunkC& chunk = reg.get<ChunkC>(it->second);
        if(distance2(chunk.center()) > unload * unload) {
            reg.emplace_or_replace<ChunkDestroying>(it->second);
            it = map.chunks.erase(it);
        } else {
            ++it;
        }
    }

    const int range = (int) std::ceil(view_distance / chunk::length);
    for(auto& center : centers) {
        glm::ivec3 origin = glm::ivec3(glm::floor(center / (float) chunk::length));
        for(int x = -range; x <= range; x++) {
            for(int y = -range; y <= range; y++) {
                for(int z = -range; z <= range; z++) {
                    glm::ivec3 pos = origin + glm::ivec3(x, y, z);
                    if(map.chunks.count(pos)) continue;
                    ChunkC chunk(pos);
                    if(distance2(chunk.center()) > view_distance * view_distance) continue;
                    auto entity = reg.create();
                    reg.emplace<ChunkC>(entity, chunk);
                    map.chunks.emplace(pos, entity);
                }
            }
        }
    }

}



void ChunkGeneratorSys::preinit() {
    generator = std::make_shared<TerrainGenerator>();
    reg.ctx().emplace<std::shared_ptr<TerrainGenerator>>(generator);
}

void ChunkGeneratorSys::collect() {

    for(auto it = jobs.begin(); it != jobs.end();) {
        auto& job = *it;
        if(!job->done.load()) {
            ++it;
            continue;
        }
        if(reg.valid(job->entity)) {
            reg.remove<ChunkGenerating>(job->entity);
            reg.emplace<ChunkGenerated>(job->entity);
            if(job->surface) {
                reg.emplace<ChunkVolume>(job->entity).values = std::move(job->values);
                reg.emplace<ChunkMeshPending>(job->entity);
            } else {
                reg.emplace<ChunkEmpty>(job->entity, job->mean);
            }
        }
        it = jobs.erase(it);
    }

}

void ChunkGeneratorSys::tick(double dt) {

    // Fixed-clock runs generate every pending chunk this frame, so what is loaded at frame N never depends on
    // worker timing
    auto* args = reg.ctx().find<RunArgs>();
    const bool sync = args && args->deterministic();

    collect();

    if(jobs.size() >= max_jobs) return;


    // Start new jobs, nearest chunks first

    auto* camera = reg.ctx().find<Camera>();
    glm::vec3 center = camera ? camera->position : glm::vec3(0.f);

    std::vector<std::pair<float, entt::entity>> candidates;
    auto view = reg.view<ChunkC>(entt::exclude<ChunkGenerated, ChunkGenerating, ChunkDestroying>);
    for(auto entity : view) {
        candidates.emplace_back(glm::distance2(view.get<ChunkC>(entity).center(), center), entity);
    }

    size_t count = sync ? candidates.size() : std::min(candidates.size(), max_jobs - jobs.size());
    std::partial_sort(candidates.begin(), candidates.begin() + count, candidates.end());

    tf::Executor& executor = reg.ctx().get<tf::Executor>();

    for(size_t i = 0; i < count; i++) {
        entt::entity entity = candidates[i].second;
        reg.emplace<ChunkGenerating>(entity);

        auto job = std::make_shared<Job>();
        job->entity = entity;
        job->pos = reg.get<ChunkC>(entity).pos;
        jobs.push_back(job);

        executor.silent_async([job, generator = generator]() {
            DY_ZONE_N("Generate chunk");
            job->surface = generator->generate(job->pos, job->values, job->mean);
            job->done.store(true);
        });
    }

    DY_PLOT("Chunk jobs", (int64_t) jobs.size());

    if(sync) {
        DY_ZONE_N("Wait for the chunks");
        for(auto& job : jobs) {
            while(!job->done.load()) {
                std::this_thread::yield();
            }
        }
        collect();
    }

}

void ChunkGeneratorSys::finish() {

    // Jobs own their data, but do not leave them running past the registry
    for(auto& job : jobs) {
        while(!job->done.load()) {
            std::this_thread::yield();
        }
    }
    jobs.clear();

}
