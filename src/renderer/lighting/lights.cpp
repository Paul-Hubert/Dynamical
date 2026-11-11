#include "lights.h"

#include <algorithm>

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "logic/components/camera.h"
#include "logic/components/point_light.h"
#include "logic/components/render_settings.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "logic/sky/sky.h"

using namespace dy;

Lights::Lights(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg), per_frame(NUM_FRAMES) {

    // A scene without the Sky system : the sun stays where it always was
    if(!reg.ctx().find<Sky>()) reg.ctx().emplace<Sky>();

    reg.storage<PointLight>();

    for(uint32_t i = 0; i < NUM_FRAMES; i++) {
        VmaAllocationCreateInfo alloc {};
        alloc.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        per_frame[i].buffer = VmaBuffer(ctx.device, &alloc, vk::BufferCreateInfo({}, sizeof(LightsUBO), vk::BufferUsageFlagBits::eUniformBuffer,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
        VmaAllocationInfo info;
        vmaGetAllocationInfo(ctx.device, per_frame[i].buffer.allocation, &info);
        per_frame[i].pointer = static_cast<LightsUBO*>(info.pMappedData);
        ctx.lights_buffers[i] = per_frame[i].buffer;
    }

}

void Lights::update(uint32_t frame_index, float time) {

    SkyLight sky = skyLight(reg.ctx().get<Sky>());

    LightsUBO ubo {};
    ubo.sun_direction = glm::vec4(sky.direction, sky.radius);
    ubo.sun_color = glm::vec4(sky.color, sky.is_sun ? 1.f : 0.f);
    ubo.sky_zenith = glm::vec4(sky.zenith, 0.f);
    ubo.sky_horizon = glm::vec4(sky.horizon, sky.night);
    ubo.sky_glow = glm::vec4(sky.glow, sky.glow_power);
    ubo.sun_disc = glm::vec4(sky.sun, sky.sun_disc);
    ubo.moon_disc = glm::vec4(sky.moon, sky.moon_disc);
    ubo.ambient_sky = glm::vec4(sky.ambient_sky, sky_light_scale);
    ubo.ambient_ground = glm::vec4(sky.ambient_ground, 0.f);
    ubo.frame = glm::vec4((float) (frames++ % 65536), time, 0.f, 0.f);

    // The point lights nearest to a viewer
    std::vector<glm::vec3> viewers;
    if(auto* camera = reg.ctx().find<Camera>()) viewers.push_back(camera->position);
    if(auto* input = reg.ctx().find<VRInput>(); input && input->session_running) {
        if(auto* rig = reg.ctx().find<VRRig>()) viewers.push_back(glm::vec3(rig->transform() * glm::vec4(input->head.position, 1.f)));
    }

    struct Near {
        float distance;
        LightsUBO::Point point;
    };
    std::vector<Near> lights;
    for(auto [entity, transform, light] : reg.view<const Transform, const PointLight>().each()) {
        if(light.intensity <= 0.f || light.radius <= 0.f) continue;
        float distance = 1e30f;
        for(auto& viewer : viewers) distance = std::min(distance, glm::distance(viewer, transform.position));
        lights.push_back({ distance - light.radius, { glm::vec4(transform.position, light.radius), glm::vec4(light.color * light.intensity, 0.f) } });
    }
    std::sort(lights.begin(), lights.end(), [](const Near& a, const Near& b) { return a.distance < b.distance; });
    int count = std::min((int) lights.size(), max_point_lights);
    for(int i = 0; i < count; i++) ubo.points[i] = lights[i].point;
    ubo.ambient_ground.w = (float) count;

    *per_frame[frame_index].pointer = ubo;
    last = ubo;

}

Lights::~Lights() {
    for(auto& buffer : ctx.lights_buffers) buffer = nullptr;
}

glm::vec4 dy::lightingView(const entt::registry& reg, RenderTarget target) {
    auto* settings = reg.ctx().find<RenderSettings>();
    if(!settings) return glm::vec4(0.f);
    const auto& tier = settings->lighting[index(target)];
    return glm::vec4(tier.traced_range, (float) tier.sun_rays, (float) tier.shadowed_torches, 0.f);
}
