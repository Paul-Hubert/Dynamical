#include "tree_shade.h"

#include <algorithm>
#include <cmath>

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"

#include "logic/components/render_settings.h"
#include "logic/components/transform.h"
#include "logic/vegetation/vegetation.h"

using namespace dy;

TreeShade::TreeShade(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg),
    depth((size_t) size * size, 0.f), texels((size_t) size * size, 0), dirty((size_t) tiles * tiles, 0) {

    vk::SamplerCreateInfo info({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat);
    ctx.tree_shade_sampler = ctx.device->createSampler(info);

    reg.storage<Transform>();
    for(auto entity : reg.view<Plant>()) onPlant(reg, entity);
    reg.on_construct<Plant>().connect<&TreeShade::onPlant>(*this);
    reg.on_destroy<Plant>().connect<&TreeShade::onPlantDestroyed>(*this);

    upload();

}

void TreeShade::onPlant(entt::registry& registry, entt::entity entity) {

    auto* library = registry.ctx().find<VegetationLibrary>();
    auto* transform = registry.try_get<Transform>(entity);
    const Plant& plant = registry.get<Plant>(entity);
    if(!library || !transform || plant.species >= library->species.size() || plants.count(entity)) return;

    const auto& species = library->species[plant.species];
    float tall = species.height * transform->scale.y;
    if(tall < 1.5f) return;

    // Some species have a branch or a root far from the trunk in their bounds
    float crown = std::min(species.radius * transform->scale.y, 0.6f * tall + 1.f);
    float strength = 0.9f * std::clamp((tall - 1.5f) / 4.f, 0.15f, 1.f);
    glm::vec2 root(transform->position.x, transform->position.z);

    // The middle and the top of the crown, where the sun puts them on the ground
    std::array<Disc, 2> discs;
    discs[0] = { root - sun_slope * (0.5f * tall), 0.85f * crown, strength };
    discs[1] = { root - sun_slope * (0.8f * tall), 0.6f * crown, strength };
    for(auto& disc : discs) draw(disc, 1.f);
    plants.emplace(entity, discs);

}

void TreeShade::onPlantDestroyed(entt::registry& registry, entt::entity entity) {
    auto it = plants.find(entity);
    if(it == plants.end()) return;
    for(auto& disc : it->second) draw(disc, -1.f);
    plants.erase(it);
}

void TreeShade::draw(const Disc& disc, float sign) {

    if(disc.radius <= 0.f) return;
    int x0 = (int) std::floor(disc.centre.x - disc.radius), x1 = (int) std::floor(disc.centre.x + disc.radius);
    int z0 = (int) std::floor(disc.centre.y - disc.radius), z1 = (int) std::floor(disc.centre.y + disc.radius);
    const float inverse = 1.f / (disc.radius * disc.radius);
    const int mask = size - 1;

    for(int z = z0; z <= z1; z++) {
        float dz = (float) z + 0.5f - disc.centre.y;
        float* row = &depth[(size_t) (z & mask) * size];
        for(int x = x0; x <= x1; x++) {
            float dx = (float) x + 0.5f - disc.centre.x;
            float inside = 1.f - (dx * dx + dz * dz) * inverse;
            if(inside <= 0.f) continue;
            float& value = row[x & mask];
            value = std::max(value + sign * disc.depth * inside, 0.f);
        }
    }

    for(int tz = z0 >> tile_shift; tz <= z1 >> tile_shift; tz++) {
        for(int tx = x0 >> tile_shift; tx <= x1 >> tile_shift; tx++) {
            dirty[(size_t) (tz & (tiles - 1)) * tiles + (tx & (tiles - 1))] = 1;
        }
    }
    any_dirty = true;

}

void TreeShade::upload() {

    for(int tz = 0; tz < tiles; tz++) {
        for(int tx = 0; tx < tiles; tx++) {
            uint8_t& flag = dirty[(size_t) tz * tiles + tx];
            if(!flag) continue;
            flag = 0;
            for(int z = tz * tile; z < (tz + 1) * tile; z++) {
                size_t row = (size_t) z * size;
                for(int x = tx * tile; x < (tx + 1) * tile; x++) {
                    texels[row + x] = (uint8_t) std::min(depth[row + x] * (255.f / max_depth) + 0.5f, 255.f);
                }
            }
        }
    }
    any_dirty = false;

    if(current.image) retired.push_back(current);

    vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8Unorm, vk::Extent3D(size, size, 1), 1, 1, vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
    current.image = ctx.transfer.createImage(texels.data(), texels.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);
    current.view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, current.image->image, vk::ImageViewType::e2D, vk::Format::eR8Unorm,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
    current.frames = 0;
    ctx.tree_shade_view = current.view;

}

void TreeShade::update() {

    // An image is let go once every frame in flight has been recorded again with the next one
    for(auto it = retired.begin(); it != retired.end();) {
        if(++it->frames > (int) NUM_FRAMES + 1) {
            ctx.device->destroy(it->view);
            it = retired.erase(it);
        } else {
            ++it;
        }
    }

    if(any_dirty) upload();

}

glm::vec4 dy::treeShadeView(const entt::registry& reg) {
    auto* settings = reg.ctx().find<RenderSettings>();
    return glm::vec4(settings ? settings->tree_shade : 0.f, 1.f / (float) TreeShade::size, TreeShade::reach, 0.f);
}

TreeShade::~TreeShade() {
    reg.on_construct<Plant>().disconnect<&TreeShade::onPlant>(*this);
    reg.on_destroy<Plant>().disconnect<&TreeShade::onPlantDestroyed>(*this);
    ctx.device->waitIdle();
    for(auto& image : retired) ctx.device->destroy(image.view);
    if(current.view) ctx.device->destroy(current.view);
    ctx.device->destroy(ctx.tree_shade_sampler);
}
