#include "soil_maps.h"

#include "renderer/context/context.h"

#include "logic/components/render_settings.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/soil.h"

#include <algorithm>
#include <vector>

using namespace dy;

// Maps baked again per frame after their parameters changed (new chunks are not counted)
constexpr int max_rebakes = 16;

SoilMaps::SoilMaps(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    // Texels are on the terrain vertices, the edges of a map are the edges of its chunk
    const uint32_t size = soil::size;
    const uint32_t layers = std::min<uint32_t>(TerrainSys::max_slots, ctx.device.physical.getProperties().limits.maxImageArrayLayers);

    // No soil anywhere until a chunk has its map
    std::vector<uint8_t> blank(soil::num_texels * soil::channels * layers, 0);
    vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(size, size, 1), 1, layers,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
    image = ctx.transfer.createImage(blank.data(), blank.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);
    ctx.soil_view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, image->image, vk::ImageViewType::e2DArray, vk::Format::eR8G8B8A8Unorm,
        vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, layers)));

    ctx.soil_sampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge
    ));

    reg.ctx().emplace<SoilLayers>(SoilLayers { ChunkLayers(layers) });

}

void SoilMaps::update() {

    auto* settings = reg.ctx().find<RenderSettings>();
    auto* buffers = reg.ctx().find<TerrainBuffers>();
    auto& layers = reg.ctx().get<SoilLayers>();
    if(!settings || !buffers) return;

    layers.enabled = settings->soil;

    const uint64_t key = soilKey(settings->soil_params);
    if(!field || field->key() != key) {
        field = std::make_unique<SoilField>(settings->soil_params);
    }

    layers.chunks.update(reg, buffers->vertices.size(), key, max_rebakes, [&](uint32_t layer, entt::entity chunk) {
        glm::ivec3 pos = reg.get<ChunkC>(chunk).pos;
        uint8_t texels[soil::num_texels * soil::channels];
        field->bake(glm::ivec2(pos.x, pos.z), texels);
        ctx.transfer.updateImage(texels, sizeof(texels), image->image,
            vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, layer, 1), vk::Offset3D(0, 0, 0),
            vk::Extent3D(soil::size, soil::size, 1), vk::ImageLayout::eShaderReadOnlyOptimal);
    });

}

SoilMaps::~SoilMaps() {
    reg.ctx().erase<SoilLayers>();
    ctx.device->waitIdle();
    ctx.device->destroy(ctx.soil_view);
    ctx.device->destroy(ctx.soil_sampler);
}
