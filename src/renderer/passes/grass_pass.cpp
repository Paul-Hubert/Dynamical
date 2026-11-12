#include "grass_pass.h"

#include "terrain_pass.h"

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"
#include "renderer/terrain/soil_maps.h"
#include "renderer/terrain/terrain_sys.h"
#include "renderer/util/pipeline.h"

#include "logic/components/render_settings.h"
#include "logic/grass/grass_bake.h"
#include "logic/grass/grass_height.h"
#include "logic/grass/grass_species.h"
#include "logic/grass/grass_textures.h"
#include "logic/settings/settings.h"
#include "logic/terrain/chunk.h"

#include "util/log.h"
#include "util/tiled_noise.h"

#include <algorithm>
#include <chrono>
#include <vector>

using namespace dy;

constexpr int noise_size = 512;
constexpr int max_mips = 7;

// Height maps baked again per frame after their parameters changed (new chunks are not counted)
constexpr int max_rebakes = 16;


// Push constants of grass.vert.glsl and grass.frag.glsl
struct GrassPC {
    glm::vec4 base_normal;      // w : follow_terrain
    glm::vec4 chunk;            // of each draw. xy : origin on the ground, z : layer of its height map, w : of its soil map (< 0 : none)
    float map_scale;            // texels of those maps per metre
    float tile_size;
    float grass_height;
    float patch_size;
    float map_top;              // what the height map holds at its highest, as a part of grass_height (GrassTopField::top)
    float noise_frequency;
    float noise_amplitude;
    float wind_strength;
    float wind_speed;
    float top_start;
    int32_t layers;             // and how much the patches of the species overlap, in thousandths, over the low byte
    float layer_spread;
    float layer_patch;
    float height_amount;        // how much shorter a kind is where its height noise is lowest
    float colour_variation;
    float dryness;
    float curve;
    float shadow;
    float canopy;
    float silhouette;
    float azimuth_dither;
    float baked_colour;
    float species_size;
    float species_shade;
};

namespace {

// A file of its own for each kind : one that changes does not make the others stale
GrassRaycast loadOrBake(GrassKind kind) {

    GrassBakeParams params;
    params.model.kind = kind;
    const std::string file = std::string("./resources/grass_raycast_") + grassKindInfo(kind).name + ".bin";
    GrassRaycast data;
    if(loadGrassRaycast(data, file) && data.key == grassBakeKey(params)) {
        return data;
    }

    auto start = std::chrono::steady_clock::now();
    data = bakeGrass(params);
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    dy::log(dy::info) << "Baked the grass rays of " << grassKindInfo(kind).name << " (" << data.size << " x " << data.size << " x " << data.slices() << ") in "
        << elapsed.count() << " s\n";

    if(!saveGrassRaycast(data, file)) {
        dy::log(dy::warning) << "Could not write " << file << ", the grass rays will be baked again next time\n";
    }
    return data;

}

// BC4, BC5 and BC1 textures that the sampler can filter
bool blocksSupported(vk::PhysicalDevice physical) {

    for(vk::Format format : {vk::Format::eBc4UnormBlock, vk::Format::eBc5UnormBlock, vk::Format::eBc1RgbSrgbBlock}) {
        auto features = physical.getFormatProperties(format).optimalTilingFeatures;
        if(!(features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) return false;
        try {
            physical.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, {});
        } catch(const vk::SystemError&) {
            return false;
        }
    }
    return true;

}

}

GrassPass::GrassPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    reg.ctx().get<RenderSettings>().grass_pass = true;

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 11),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, 1, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eVertex),
        vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(5, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(6, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(7, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(8, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(9, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(10, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment)
    };
    descLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));
    descSet = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &descLayout))[0];

    // The tile repeats
    sampler = ctx.device->createSampler(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
        0.f, VK_FALSE, 1.f, VK_FALSE, vk::CompareOp::eNever, 0.f, VK_LOD_CLAMP_NONE
    ));

    // The baked rays are seen at grazing angles most of the time : trilinear, 16 times anisotropic unless the
    // level of the graphics says less (RenderSettings::textures)
    {
        auto* render_settings = reg.ctx().find<RenderSettings>();
        const float anisotropy = render_settings ? (float) render_settings->textures.grass_anisotropy : 16.f;
        vk::SamplerCreateInfo info(
            {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
            0.f, VK_FALSE, 1.f, VK_FALSE, vk::CompareOp::eNever, 0.f, VK_LOD_CLAMP_NONE
        );
        if(ctx.device.sampler_anisotropy) {
            info.anisotropyEnable = VK_TRUE;
            info.maxAnisotropy = std::clamp(anisotropy, 1.f, ctx.device.physical.getProperties().limits.maxSamplerAnisotropy);
        }
        raySampler = ctx.device->createSampler(info);
    }

    // Baked rays of every kind of tile (GrassKind), with mips for the grass far away
    {
        // grass_kind=clover : that kind everywhere, instead of the patches of the species
        auto* run = reg.ctx().find<Settings>();
        if(run && !run->grass_kind.empty()) {
            GrassKind forced;
            if(grassKindFromName(run->grass_kind, forced)) {
                rayLayout.forced_kind = static_cast<int32_t>(forced);
            } else {
                dy::log(dy::warning) << "Unknown grass_kind " << run->grass_kind << "\n";
            }
        }

        // A layer per azimuth, the elevations one after the other, the kinds one after the other : mips are
        // per layer, the azimuths are blended in the shader. The heights, which every layer of the grass
        // reads, are apart from the normals, the blade ids and the colours, which only the closest hit
        // reads; in blocks where the GPU has them
        const bool compressed = blocksSupported(ctx.device.physical);
        const vk::Format one = compressed ? vk::Format::eBc4UnormBlock : vk::Format::eR8Unorm;
        const vk::Format two = compressed ? vk::Format::eBc5UnormBlock : vk::Format::eR8G8Unorm;
        const vk::Format colour = compressed ? vk::Format::eBc1RgbSrgbBlock : vk::Format::eR8G8B8A8Srgb;

        uint32_t size = 0, slices = 0;
        std::vector<GrassTextures> oblique(grass_kinds), top(grass_kinds);
        std::vector<GrassTexture> oblique_colour(grass_kinds), top_colour(grass_kinds);

        auto start = std::chrono::steady_clock::now();
        for(int k = 0; k < grass_kinds; k++) {
            const GrassKind kind = static_cast<GrassKind>(k);
            GrassRaycast raycast = loadOrBake(kind);
            if(k == 0) {
                size = static_cast<uint32_t>(raycast.size);
                slices = static_cast<uint32_t>(raycast.slices());
                for(int e = 0; e < grass_elevations; e++) {
                    rayLayout.angles[e] = raycast.angles[e];
                }
                rayLayout.low_from = raycast.low_from;
                rayLayout.low_to = raycast.low_to;
            }
            oblique[k] = makeGrassTextures(raycast.oblique, raycast.size, raycast.slices(), max_mips, compressed);
            top[k] = makeGrassTextures(raycast.top, raycast.size, 1, max_mips, compressed);
            oblique_colour[k] = makeGrassColour(raycast.oblique_colour, raycast.size, raycast.slices(), max_mips, compressed);
            top_colour[k] = makeGrassColour(raycast.top_colour, raycast.size, 1, max_mips, compressed);

            // The size the kind was modelled at, and how it is chosen (logic/grass/grass_species.h)
            GrassModelParams model;
            model.kind = kind;
            GrassModel tile = generateGrassTile(model);
            const GrassKindInfo& info = grassKindInfo(kind);
            rayLayout.tiles[k] = tile.tile_size;
            rayLayout.heights[k] = tile.height;
            rayLayout.abundance[k] = info.abundance;
            if(info.small) rayLayout.small_mask |= 1 << k;
        }

        // The same level of every kind, kind after kind
        auto merge = [&](auto&& texture) {
            GrassTexture out;
            const size_t levels = texture(0).level_sizes.size();
            std::vector<size_t> offsets(grass_kinds, 0);
            for(size_t level = 0; level < levels; level++) {
                const size_t before = out.data.size();
                for(int k = 0; k < grass_kinds; k++) {
                    const GrassTexture& from = texture(k);
                    out.data.insert(out.data.end(), from.data.begin() + offsets[k], from.data.begin() + offsets[k] + from.level_sizes[level]);
                    offsets[k] += from.level_sizes[level];
                }
                out.level_sizes.push_back(out.data.size() - before);
            }
            return out;
        };

        auto upload = [&](const GrassTexture& texture, vk::Format format, uint32_t layers, Baked& out) {
            const uint32_t levels = static_cast<uint32_t>(texture.level_sizes.size());
            vk::ImageCreateInfo info({}, vk::ImageType::e2D, format, vk::Extent3D(size, size, 1), levels, layers,
                vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
            out.image = ctx.transfer.createImageMips(texture.data.data(), texture.level_sizes, info, vk::ImageLayout::eShaderReadOnlyOptimal);
            out.view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, out.image->image, vk::ImageViewType::e2DArray, format,
                vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, levels, 0, layers)));
            return texture.data.size();
        };

        const uint32_t kinds = grass_kinds, layers = slices * kinds;
        if(layers > ctx.device.physical.getProperties().limits.maxImageArrayLayers) {
            dy::log(dy::error) << "The grass rays have " << layers << " azimuths, more than the layers of an image on this GPU\n";
        }

        size_t bytes = 0;
        bytes += upload(merge([&](int k) -> const GrassTexture& { return oblique[k].height; }), one, layers, baked[0]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return top[k].height; }), one, kinds, baked[1]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return oblique[k].normal; }), two, layers, baked[2]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return oblique[k].id; }), one, layers, baked[3]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return top[k].normal; }), two, kinds, baked[4]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return top[k].id; }), one, kinds, baked[5]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return oblique_colour[k]; }), colour, layers, baked[6]);
        bytes += upload(merge([&](int k) -> const GrassTexture& { return top_colour[k]; }), colour, kinds, baked[7]);

        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        dy::log(dy::info) << "Grass rays of " << kinds << " kinds : " << bytes / 1e6 << " MB" << (compressed ? " in blocks, ready in " : ", ready in ")
            << elapsed.count() << " s\n";
    }

    // The noises the kinds are chosen by : it repeats (logic/grass/grass_species.h)
    {
        std::vector<int8_t> data = bakeSpeciesNoise();
        vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Snorm, vk::Extent3D(grass_species::size, grass_species::size, 1), 1, 1,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        speciesImage = ctx.transfer.createImage(data.data(), data.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);
        speciesView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, speciesImage->image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Snorm,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
    }

    // Tileable noise to break the repetition of the tiles
    {
        TiledNoise noise;
        std::vector<int8_t> data(4 * noise_size * noise_size, 0);
        for(int x = 0; x < noise_size; x++) {
            for(int y = 0; y < noise_size; y++) {
                for(int c = 0; c < 3; c++) {
                    data[(x * noise_size + y) * 4 + c] = static_cast<int8_t>(std::clamp(noise.octaveNoise(x * 10. / noise_size, y * 10. / noise_size, c * 1000., 2) * 128., -127., 127.));
                }
            }
        }

        vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Snorm, vk::Extent3D(noise_size, noise_size, 1), 1, 1,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        noiseImage = ctx.transfer.createImage(data.data(), data.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);

        noiseView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, noiseImage->image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Snorm,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
    }

    // Height maps of the chunks, the top of their plants (GrassTopField) : a layer each, filled as the
    // chunks come (update). Texels are on the terrain vertices, the edges of a map are the edges of its chunk
    {
        const uint32_t size = grass_height::size;
        const uint32_t layers = std::min<uint32_t>(TerrainSys::max_slots, ctx.device.physical.getProperties().limits.maxImageArrayLayers);

        std::vector<uint8_t> blank(grass_height::num_texels * layers, 255);
        vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8Unorm, vk::Extent3D(size, size, 1), 1, layers,
            vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        heightImage = ctx.transfer.createImage(blank.data(), blank.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);
        heightView = ctx.device->createImageView(vk::ImageViewCreateInfo({}, heightImage->image, vk::ImageViewType::e2DArray, vk::Format::eR8Unorm,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, layers)));

        heightSampler = ctx.device->createSampler(vk::SamplerCreateInfo(
            {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge
        ));

        heightLayers = ChunkLayers(layers);
    }

    // Bindings of grass.frag.glsl : the heights of the rays first, their normals and ids after the other maps, their colours last
    const uint32_t baked_bindings[8] = {0, 1, 4, 5, 6, 7, 8, 9};
    std::array<vk::DescriptorImageInfo, 8> bakedInfos;
    std::vector<vk::WriteDescriptorSet> writes;
    for(size_t i = 0; i < baked.size(); i++) {
        bakedInfos[i] = vk::DescriptorImageInfo(raySampler, baked[i].view, vk::ImageLayout::eShaderReadOnlyOptimal);
        writes.push_back(vk::WriteDescriptorSet(descSet, baked_bindings[i], 0, 1, vk::DescriptorType::eCombinedImageSampler, &bakedInfos[i], nullptr, nullptr));
    }
    auto noiseInfo = vk::DescriptorImageInfo(sampler, noiseView, vk::ImageLayout::eShaderReadOnlyOptimal);
    auto heightInfo = vk::DescriptorImageInfo(heightSampler, heightView, vk::ImageLayout::eShaderReadOnlyOptimal);
    writes.push_back(vk::WriteDescriptorSet(descSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &noiseInfo, nullptr, nullptr));
    writes.push_back(vk::WriteDescriptorSet(descSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &heightInfo, nullptr, nullptr));
    auto speciesInfo = vk::DescriptorImageInfo(sampler, speciesView, vk::ImageLayout::eShaderReadOnlyOptimal);
    writes.push_back(vk::WriteDescriptorSet(descSet, 10, 0, 1, vk::DescriptorType::eCombinedImageSampler, &speciesInfo, nullptr, nullptr));
    ctx.device->updateDescriptorSets(writes, {});

    auto layouts = std::vector { ctx.view_layout, descLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(GrassPC));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

}

void GrassPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/grass.vert.glsl.spv";
    desc.fragment_shader = sceneFragmentShader(ctx, "grass");
    desc.color_attachments = ctx.sceneAttachments();
    desc.bindings = terrainVertexBindings();
    desc.attributes = terrainVertexAttributes();
    desc.layout = layout;
    desc.renderpass = renderpass;

    // How the oblique rays are laid out : azimuths of each elevation (constant_id 0 to 3), heights of
    // the lowest rays (4 and 5). The kinds of tiles : their tile size (6 to 15), height (16 to 25) and
    // abundance (26 to 35), which are small (36), the one that is everywhere if any (37)
    vk::SpecializationMapEntry entries[38];
    for(uint32_t k = 0; k < grass_kinds; k++) {
        entries[6 + k] = vk::SpecializationMapEntry(6 + k, offsetof(RayLayout, tiles) + k * sizeof(float), sizeof(float));
        entries[16 + k] = vk::SpecializationMapEntry(16 + k, offsetof(RayLayout, heights) + k * sizeof(float), sizeof(float));
        entries[26 + k] = vk::SpecializationMapEntry(26 + k, offsetof(RayLayout, abundance) + k * sizeof(float), sizeof(float));
    }
    entries[36] = vk::SpecializationMapEntry(36, offsetof(RayLayout, small_mask), sizeof(int32_t));
    entries[37] = vk::SpecializationMapEntry(37, offsetof(RayLayout, forced_kind), sizeof(int32_t));
    for(uint32_t e = 0; e < 4; e++) {
        entries[e] = vk::SpecializationMapEntry(e, offsetof(RayLayout, angles) + e * sizeof(int32_t), sizeof(int32_t));
    }
    entries[4] = vk::SpecializationMapEntry(4, offsetof(RayLayout, low_from), sizeof(float));
    entries[5] = vk::SpecializationMapEntry(5, offsetof(RayLayout, low_to), sizeof(float));
    auto specialization = vk::SpecializationInfo(38, entries, sizeof(RayLayout), &rayLayout);
    desc.fragment_specialization = &specialization;

    if(!ctx.sceneStencil()) {
        pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);
        return;
    }

    // The front faces : whole where the stencil is 0, seen through where the back faces marked it
    desc.stencil_test = true;
    desc.stencil_compare = vk::CompareOp::eEqual;
    desc.stencil_reference = 0;
    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);
    desc.stencil_reference = 1;
    cuts[index(target)] = createGraphicsPipeline(ctx.device, desc);

    // The back faces, where they are in front of the ground : nothing drawn but the mark
    desc.fragment_shader = "./resources/shaders/grass_back.frag.glsl.spv";
    desc.fragment_specialization = nullptr;
    desc.cull = vk::CullModeFlagBits::eFront;
    desc.depth_write = false;
    desc.color_write = false;
    desc.stencil_compare = vk::CompareOp::eAlways;
    desc.stencil_pass = vk::StencilOp::eReplace;
    backs[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

void GrassPass::update(uint32_t frame_index) {

    auto* settings = reg.ctx().find<RenderSettings>();
    auto* buffers = reg.ctx().find<TerrainBuffers>();
    if(!settings || !buffers) return;

    // The top of the plants : the kinds of each place and how tall they grow there, as the shader has them
    GrassTopParams top;
    top.species = settings->grass_params.species;
    top.amount = settings->grass_params.patch_amount;
    top.forced = rayLayout.forced_kind;
    top.height_map = settings->grass_params.height_map;
    for(int k = 0; k < grass_kinds; k++) top.heights[k] = rayLayout.heights[k];
    const uint64_t key = grassTopKey(top);
    if(!topField || topField->key() != key) {
        topField = std::make_unique<GrassTopField>(top);
    }

    // A chunk gets its map with its mesh, before it is drawn. After a change of the parameters the maps
    // are replaced a few per frame, chunks keep the one they have until then
    heightLayers.update(reg, buffers->vertices.size(), key, max_rebakes, [&](uint32_t layer, entt::entity chunk) {
        glm::ivec3 pos = reg.get<ChunkC>(chunk).pos;
        uint8_t texels[grass_height::num_texels];
        topField->bake(glm::ivec2(pos.x, pos.z), texels);
        ctx.transfer.updateImage(texels, sizeof(texels), heightImage->image,
            vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, layer, 1), vk::Offset3D(0, 0, 0),
            vk::Extent3D(grass_height::size, grass_height::size, 1), vk::ImageLayout::eShaderReadOnlyOptimal);
    });

}

void GrassPass::record(vk::CommandBuffer command, const RenderView& view) {

    const entt::registry& creg = reg;

    auto* settings = creg.ctx().find<RenderSettings>();
    auto* buffers = creg.ctx().find<TerrainBuffers>();
    auto* soil = creg.ctx().find<SoilLayers>();
    if(!buffers || !settings || !settings->grass || !topField) return;

    auto& params = settings->grass_params;
    GrassPC pc {
        glm::vec4(params.base_normal, params.follow_terrain), glm::vec4(0.f),
        1.f / (float) chunk::cube_size,
        std::max(params.tile_size, 1e-3f), std::max(params.height, 1e-3f),
        std::max(params.patch_size, 1e-3f), topField->top(),
        params.noise_frequency, params.noise_amplitude,
        params.wind_strength, params.wind_speed,
        params.top_start, std::clamp(params.layers, 1, grass_species::layers) | (static_cast<int32_t>(std::clamp(params.species.overlap, 0.f, 1.f) * 1000.f) << 8),
        params.layer_spread, params.layer_patch, std::clamp(params.patch_amount, 0.f, 1.f),
        params.colour_variation, params.dryness, params.curve,
        params.shadow, params.canopy, params.silhouette, params.azimuth_dither,
        params.baked_colour, params.species.size, params.species.shade
    };

    const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set, descSet}, {});

    // The shell of every chunk with one pipeline. silhouette : how much the blades are seen through
    auto draw = [&](vk::Pipeline pipeline, float silhouette) {
        pc.silhouette = silhouette;
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        command.pushConstants(layout, stages, 0, sizeof(GrassPC), &pc);

        for(auto [entity, position, gpu] : creg.view<const ChunkC, const ChunkGPU, const ChunkReady>().each()) {

            // No grass without the height map of the chunk (update gave it one, unless the layers ran out)
            int32_t height_layer = heightLayers.layer(gpu.slot, entity);
            if(height_layer < 0) continue;

            // Its soil map says where there is none (SoilMaps)
            glm::vec3 origin = position.origin();
            glm::vec4 where(origin.x, origin.z, (float) height_layer, soil ? (float) soil->layer(gpu.slot, entity) : -1.f);
            command.pushConstants(layout, stages, offsetof(GrassPC, chunk), sizeof(glm::vec4), &where);

            command.bindVertexBuffers(0, {buffers->vertices[gpu.slot]}, {0});
            command.drawIndirect(buffers->indirect, gpu.slot * buffers->indirect_stride, 1, 0);
        }
    };

    // Seen through only where the ray comes out of the grass again : where the back of the shell is in
    // front of the ground, which its back faces mark in the stencil first. A ray that ends on the ground
    // stays in the grass all the way (cutting it there showed the dark ground under the crests)
    const bool see_through = params.silhouette > 0.f && cuts[index(view.target)];
    if(see_through) draw(backs[index(view.target)], 0.f);
    draw(pipelines[index(view.target)], 0.f);
    if(see_through) draw(cuts[index(view.target)], params.silhouette);

}

GrassPass::~GrassPass() {

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    for(auto pipeline : backs) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    for(auto pipeline : cuts) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(heightView);
    ctx.device->destroy(heightSampler);
    ctx.device->destroy(noiseView);
    ctx.device->destroy(speciesView);
    for(auto& texture : baked) {
        ctx.device->destroy(texture.view);
    }
    ctx.device->destroy(raySampler);
    ctx.device->destroy(sampler);
    ctx.device->destroy(descLayout);
    ctx.device->destroy(descPool);

}
