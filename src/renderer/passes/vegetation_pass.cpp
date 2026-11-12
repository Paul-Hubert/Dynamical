#include "vegetation_pass.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>

#include "renderer/context/context.h"
#include "renderer/lighting/ray_meshes.h"
#include "renderer/util/pipeline.h"

#include "logic/components/camera.h"
#include "logic/components/render_settings.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "util/log.h"

using namespace dy;

constexpr uint32_t max_materials = 1024;
constexpr size_t max_jobs = 6;

// Part of the light a leaf lets through, in the rays of the lighting : its brightest colour channel
constexpr float leaf_translucency = 0.45f;

namespace {

struct PlantPC {
    glm::mat4 model;
    glm::vec4 tint;     // w : height of the plant
    glm::vec4 params;   // x : alpha cutoff, y : billboard cards, z : normal map, w : subsurface
};

struct MapSlot {
    std::string VegetationMaterial::* path;
    bool srgb;
    bool alpha_test;
};

// Bindings 0 to 3 of the material set
// Push constants of vegetation_volume.*.glsl
struct VolumePC {
    glm::mat4 model;
    glm::vec4 low;      // w : index of the textures of the volume
    glm::vec4 size;     // w : a texel in metres
};

const MapSlot map_slots[] = {
    { &VegetationMaterial::color, true, true },
    { &VegetationMaterial::normal, false, false },
    { &VegetationMaterial::extra, false, false },
    { &VegetationMaterial::subsurface, true, false },
};

}

VegetationPass::VegetationPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    reg.storage<Plant>();
    reg.storage<Transform>();
    reg.ctx().emplace<VegetationStats>();
    species.resize(vegetationLibrary(reg).species.size());
    if(reg.ctx().find<RayMeshes>()) reg.ctx().emplace<RayPlantMeshes>().mesh.assign(species.size(), -1);

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, max_materials * 4),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, max_materials, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
    };
    materialLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

    vk::SamplerCreateInfo samplerInfo({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat);
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if(ctx.device.sampler_anisotropy) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        auto* render_settings = reg.ctx().find<RenderSettings>();
        samplerInfo.maxAnisotropy = std::clamp(render_settings ? (float) render_settings->textures.vegetation_anisotropy : 8.f,
            1.f, ctx.device.physical.getProperties().limits.maxSamplerAnisotropy);
    }
    sampler = ctx.device->createSampler(samplerInfo);
    samplerInfo.maxLod = 5.f;
    samplerInfo.mipLodBias = -0.5f;
    atlas_sampler = ctx.device->createSampler(samplerInfo);

    white = makeSolidTexture(ctx, 0xFFFFFFFF);
    flat_normal = makeSolidTexture(ctx, 0xFFFF8080);       // (0.5, 0.5, 1)
    default_extra = makeSolidTexture(ctx, 0xFFFF0000);     // rough, not metallic, no occlusion
    black = makeSolidTexture(ctx, 0xFF000000);

    auto layouts = std::vector { ctx.view_layout, materialLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PlantPC));
    auto volumeRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(VolumePC));
    volume_layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, ctx.view_layout, volumeRange));

    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

}

void VegetationPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/vegetation.vert.glsl.spv";
    desc.fragment_shader = sceneFragmentShader(ctx, "vegetation");
    desc.color_attachments = ctx.sceneAttachments();
    desc.bindings = {
        vk::VertexInputBindingDescription(0, sizeof(VegetationVertex), vk::VertexInputRate::eVertex),
    };
    desc.attributes = {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(VegetationVertex, position)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR8G8B8A8Snorm, offsetof(VegetationVertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR8G8B8A8Snorm, offsetof(VegetationVertex, tangent)),
        vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat, offsetof(VegetationVertex, uv)),
    };
    desc.layout = layout;
    desc.renderpass = renderpass;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

    desc.cull = vk::CullModeFlagBits::eNone;
    two_sided_pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

    // The box of a plant far away, both sides drawn (its fragment shader keeps the near one)
    if(ctx.device.ray.enabled()) {
        GraphicsPipelineDesc box;
        box.vertex_shader = "./resources/shaders/vegetation_volume.vert.glsl.spv";
        box.fragment_shader = "./resources/shaders/vegetation_volume.rt.frag.glsl.spv";
        box.color_attachments = ctx.sceneAttachments();
        box.layout = volume_layout;
        box.renderpass = renderpass;
        box.cull = vk::CullModeFlagBits::eNone;
        volume_pipelines[index(target)] = createGraphicsPipeline(ctx.device, box);
    }

}

void VegetationPass::request(uint32_t index) {

    species[index] = std::make_unique<Species>();

    auto job = std::make_shared<Job>();
    job->species = index;
    jobs.push_back(job);

    // The worker reads its own copy of what it needs
    auto library = std::make_shared<const VegetationLibrary>(reg.ctx().get<VegetationLibrary>());
    auto loaded = std::make_shared<std::set<std::string>>();
    for(auto& [path, texture] : textures) loaded->insert(path);

    reg.ctx().get<tf::Executor>().silent_async([job, library, loaded]() {
        DY_ZONE_N("Load plant species");
        try {
            job->def = library->loadSpecies(job->species);
            job->vertices = library->loadGeometry(job->def, job->indices);

            for(auto& material : job->def.materials) {
                for(auto& slot : map_slots) {
                    const std::string& path = material.*slot.path;
                    if(path.empty() || loaded->count(path)) continue;
                    loaded->insert(path);
                    Job::Texture texture;
                    texture.path = path;
                    texture.srgb = slot.srgb;
                    texture.alpha_cutoff = slot.alpha_test ? job->def.alpha_cutoff : 0.f;
                    job->textures.push_back(std::move(texture));
                }
            }
            for(auto& texture : job->textures) {
                try {
                    texture.pixels = decodeTextureMips((library->directory / texture.path).string(), texture.srgb, texture.alpha_cutoff);
                } catch(const std::exception&) {
                    texture.failed = true;
                }
            }
        } catch(const std::exception& e) {
            job->error = e.what();
            if(job->error.empty()) job->error = "unknown error";
        }
        job->done.store(true);
    });

}

vk::ImageView VegetationPass::viewOf(const std::shared_ptr<VmaImage>& image) {
    auto it = default_views.find(image.get());
    if(it == default_views.end()) {
        vk::ImageView view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, image->image, vk::ImageViewType::e2D, image->format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        it = default_views.emplace(image.get(), view).first;
    }
    return it->second;
}

vk::ImageView VegetationPass::textureView(const std::string& path, const std::shared_ptr<VmaImage>& fallback) {
    if(!path.empty()) {
        auto it = textures.find(path);
        if(it != textures.end() && it->second.image) return it->second.view;
    }
    return viewOf(fallback);
}

void VegetationPass::finishJob(Job& job) {

    Species& s = *species[job.species];
    const auto& entry = reg.ctx().get<VegetationLibrary>().species[job.species];

    if(!job.error.empty()) {
        dy::log(dy::error) << "Vegetation : cannot load " << entry.id << " : " << job.error << "\n";
        s.state = Species::State::Failed;
        return;
    }

    for(auto& texture : job.textures) {
        if(textures.count(texture.path)) continue;
        Texture t;
        if(texture.failed) {
            dy::log(dy::warning) << "Vegetation : cannot load texture " << texture.path << " of " << entry.id << "\n";
        } else {
            vk::Format format = texture.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
            t.image = uploadTexture(ctx, texture.pixels, format);
            t.cutout = texture.pixels.cutout;
            // The smallest level is the mean colour of what is not cut out : what the rays that bounce read
            if(texture.srgb && texture.pixels.data.size() >= 4) {
                const uint8_t* last = texture.pixels.data.data() + texture.pixels.data.size() - 4;
                t.mean = glm::pow(glm::vec3(last[0], last[1], last[2]) / 255.f, glm::vec3(2.2f));
            }
            t.view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, t.image->image, vk::ImageViewType::e2D, format,
                vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, (uint32_t) texture.pixels.level_sizes.size(), 0, 1)));
        }
        // A texture that failed stays in the map without an image, so it is not decoded again
        textures.emplace(texture.path, t);
    }

    s.def = std::move(job.def);
    s.height = std::max(s.def.max.y, 0.01f);
    // Sphere around the bounds, from the middle of the height
    glm::vec3 reach = glm::max(glm::abs(s.def.min), glm::abs(s.def.max));
    s.radius = glm::length(glm::vec3(reach.x, std::max(s.def.max.y - s.height * 0.5f, s.height * 0.5f - s.def.min.y), reach.z));

    s.vertices = ctx.transfer.createBuffer(job.vertices.data(), vk::BufferCreateInfo({}, job.vertices.size() * sizeof(VegetationVertex),
        vk::BufferUsageFlagBits::eVertexBuffer | ctx.rayInputUsage(), vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
    s.indices = ctx.transfer.createBuffer(job.indices.data(), vk::BufferCreateInfo({}, job.indices.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eIndexBuffer | ctx.rayInputUsage(), vk::SharingMode::eExclusive, 1, &ctx.device.g_i));

    for(auto& def : s.def.materials) {
        Material material;
        material.tint = def.tint;
        material.two_sided = def.two_sided;
        material.billboard = def.billboard;

        // Every map of a material shares the layout of its color map
        auto color = textures.find(def.color);
        vk::Sampler material_sampler = color != textures.end() && color->second.cutout ? atlas_sampler : sampler;

        const std::shared_ptr<VmaImage>* fallbacks[] = { &white, &flat_normal, &default_extra, &black };
        vk::DescriptorImageInfo images[4];
        bool present[4];
        for(int i = 0; i < 4; i++) {
            const std::string& path = def.*map_slots[i].path;
            auto it = textures.find(path);
            present[i] = !path.empty() && it != textures.end() && it->second.image;
            images[i] = vk::DescriptorImageInfo(material_sampler, textureView(path, *fallbacks[i]), vk::ImageLayout::eShaderReadOnlyOptimal);
        }
        material.has_normal = present[1];
        material.has_subsurface = present[3];

        material.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &materialLayout))[0];
        std::vector<vk::WriteDescriptorSet> writes;
        for(uint32_t i = 0; i < 4; i++) {
            writes.push_back(vk::WriteDescriptorSet(material.set, i, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[i], nullptr, nullptr));
        }
        ctx.device->updateDescriptorSets(writes, {});

        s.materials.push_back(material);
    }

    // With ray tracing the most detailed level goes to the rays : bark stops the light, leaves let it
    // through where their texture is, tinted (docs/lighting.md)
    if(auto* ray_meshes = reg.ctx().find<RayMeshes>(); ray_meshes && !s.def.lods.empty() && !s.def.lods[0].billboard) {
        const VegetationLod& lod = s.def.lods[0];
        RayMesh mesh;
        mesh.kind = RayMesh::Kind::Plant;
        mesh.vertices = s.vertices;
        mesh.indices = s.indices;
        mesh.vertex_stride = sizeof(VegetationVertex);
        mesh.first_vertex = lod.first_vertex;
        mesh.vertex_count = lod.vertex_count;
        for(auto& draw : lod.draws) {
            const VegetationMaterial& def = s.def.materials[draw.material];
            RayMesh::Part part;
            part.first_index = draw.first_index;
            part.index_count = draw.index_count;
            auto color = textures.find(def.color);
            bool textured = color != textures.end() && color->second.image;
            glm::vec3 mean = (textured ? color->second.mean : glm::vec3(0.4f)) * glm::vec3(def.tint);
            part.color = mean;
            if(textured && color->second.cutout) {
                part.texture = ray_meshes->texture(color->second.view, atlas_sampler);
                part.opaque = part.texture < 0;
                part.alpha_cutoff = s.def.alpha_cutoff;
                // What a leaf lets through has its colour, a little greyer
                part.transmission = glm::mix(mean, glm::vec3(glm::dot(mean, glm::vec3(0.3f, 0.6f, 0.1f))), 0.25f) * leaf_translucency / std::max(std::max(mean.r, mean.g), std::max(mean.b, 0.05f));
            }
            mesh.parts.push_back(part);
        }
        auto& plant_meshes = reg.ctx().get<RayPlantMeshes>();
        if(plant_meshes.mesh.size() < species.size()) plant_meshes.mesh.resize(species.size(), -1);
        if(plant_meshes.min.size() < species.size()) {
            plant_meshes.min.resize(species.size(), glm::vec3(0.f));
            plant_meshes.max.resize(species.size(), glm::vec3(0.f));
        }
        plant_meshes.min[job.species] = s.def.min;
        plant_meshes.max[job.species] = s.def.max;
        plant_meshes.mesh[job.species] = (int32_t) ray_meshes->add(std::move(mesh));
    }

    s.state = Species::State::Ready;

    uint32_t triangles = 0;
    for(auto& draw : s.def.lods[0].draws) triangles += draw.index_count / 3;
    dy::log(dy::info) << "Vegetation : loaded " << entry.id << " (" << s.def.lods.size() << " levels, " << triangles << " triangles, "
        << job.textures.size() << " textures)\n";

}

void VegetationPass::update(uint32_t frame_index) {

    for(auto& list : instances) list.clear();
    for(auto& list : volume_instances) list.clear();
    auto* volumes = reg.ctx().find<PlantVolumeSet>();

    for(auto it = jobs.begin(); it != jobs.end();) {
        if((*it)->done.load()) {
            finishJob(**it);
            it = jobs.erase(it);
        } else {
            ++it;
        }
    }

    auto& stats = reg.ctx().get<VegetationStats>();
    stats = VegetationStats();

    if(auto* preload = reg.ctx().find<VegetationPreload>()) {
        for(uint32_t s : preload->species) {
            if(s >= species.size() || species[s]) continue;
            if(jobs.size() < max_jobs) request(s);
            else stats.species_loading++;
        }
    }
    for(auto& s : species) {
        if(!s) continue;
        if(s->state == Species::State::Ready) stats.species_loaded++;
        if(s->state == Species::State::Loading) stats.species_loading++;
    }

    auto* settings = reg.ctx().find<RenderSettings>();
    if(settings && !settings->vegetation) return;
    float lod_bias = settings ? settings->vegetation_params.lod_bias : 2.f;
    int force_lod = settings ? settings->vegetation_params.force_lod : -1;


    // Where each target looks from

    struct Eye {
        bool active = false;
        glm::vec3 position = glm::vec3(0.f);
        float tan_half_fov = 1.f;
        // Plants outside this cone around `forward` are not drawn
        bool cull = false;
        glm::vec3 forward = glm::vec3(0.f, 0.f, -1.f);
        float cone_sin = 0.f, cone_cos = 1.f;
    };
    std::array<Eye, NUM_RENDER_TARGETS> eyes;

    if(auto* camera = reg.ctx().find<Camera>(); camera && pipelines[index(RenderTarget::Desktop)]) {
        Eye& eye = eyes[index(RenderTarget::Desktop)];
        eye = { true, camera->position, std::tan(camera->fov * 0.5f) };
        // The cone around the corners of the widest window (21:9)
        float cone = std::atan(eye.tan_half_fov * std::sqrt(1.f + 2.4f * 2.4f));
        eye.cull = settings ? settings->vegetation_params.cull : true;
        eye.forward = camera->forward();
        eye.cone_sin = std::sin(cone);
        eye.cone_cos = std::cos(cone);
    }
    auto* vr_input = reg.ctx().find<VRInput>();
    auto* rig = reg.ctx().find<VRRig>();
    if(vr_input && rig && vr_input->session_running && pipelines[index(RenderTarget::VR)]) {
        // Headsets show about 100 degrees vertically
        eyes[index(RenderTarget::VR)] = { true, glm::vec3(rig->transform() * glm::vec4(vr_input->head.position, 1.f)), 1.2f };
    }


    // One level of detail per plant and target

    bool unrequested = false;
    for(auto [entity, transform, plant] : reg.view<const Transform, const Plant>().each()) {

        stats.plants++;
        if(plant.species >= species.size()) continue;

        if(!species[plant.species]) {
            if(jobs.size() < max_jobs) request(plant.species);
            else unrequested = true;
            continue;
        }
        const Species& s = *species[plant.species];
        if(s.state != Species::State::Ready) continue;

        float height = s.height * transform.scale.y;
        glm::vec3 center = transform.position + glm::vec3(0.f, height * 0.5f, 0.f);
        float radius = s.radius * transform.scale.y;
        bool have_matrix = false;
        glm::mat4 matrix;

        for(uint32_t target = 0; target < NUM_RENDER_TARGETS; target++) {
            const Eye& eye = eyes[target];
            if(!eye.active) continue;

            if(eye.cull) {
                glm::vec3 to = center - eye.position;
                float along = glm::dot(to, eye.forward);
                float aside = std::sqrt(std::max(glm::dot(to, to) - along * along, 0.f));
                if(aside * eye.cone_cos - along * eye.cone_sin > radius) continue;
            }

            int lod = -1;
            bool held_volume = plant.lod == Plant::volume;
            if(held_volume) {
                lod = (int) s.def.lods.size() - 1;
            } else if(force_lod >= 0 || plant.lod >= 0) {
                lod = std::min(plant.lod >= 0 ? plant.lod : force_lod, (int) s.def.lods.size() - 1);
            } else {
                float distance = std::max(glm::distance(eye.position, center), 0.01f);
                float screen_height = height / (2.f * distance * eye.tan_half_fov) * lod_bias;
                for(size_t i = 0; i < s.def.lods.size(); i++) {
                    if(screen_height >= s.def.lods[i].screen_height) {
                        lod = (int) i;
                        break;
                    }
                }
            }
            if(lod < 0) continue;

            if(!have_matrix) {
                matrix = transform.matrix();
                have_matrix = true;
            }
            // Far away : the volume of the species where it is baked, in place of the billboard
            bool far_volume = s.def.lods[lod].billboard && plant.lod == -1 && force_lod < 0 && (!settings || settings->far_plants_on_screen);
            if((far_volume || held_volume) && volumes && plant.species < volumes->species.size() && volumes->species[plant.species].index >= 0
                && volume_pipelines[target]) {
                volume_instances[target].push_back({ plant.species, matrix });
                continue;
            }
            instances[target].push_back({ plant.species, (uint32_t) lod, matrix });
        }
    }
    if(unrequested) stats.species_loading++;

    for(auto& list : instances) {
        std::sort(list.begin(), list.end(), [](const Instance& a, const Instance& b) {
            return a.species != b.species ? a.species < b.species : a.lod < b.lod;
        });
    }

    for(auto& instance : instances[index(RenderTarget::Desktop)]) {
        stats.drawn++;
        for(auto& draw : species[instance.species]->def.lods[instance.lod].draws) stats.triangles += draw.index_count / 3;
    }
}

void VegetationPass::record(vk::CommandBuffer command, const RenderView& view) {

    const entt::registry& creg = reg;

    const auto& list = instances[index(view.target)];
    const auto& boxes = volume_instances[index(view.target)];
    if(list.empty() && boxes.empty()) return;

    auto* settings = creg.ctx().find<RenderSettings>();
    bool normal_maps = !settings || settings->vegetation_params.normal_maps;
    bool subsurface = !settings || settings->vegetation_params.subsurface;

    const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::Pipeline bound_pipeline;
    vk::DescriptorSet bound_set;
    uint32_t bound_species = ~0u;

    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set}, {});

    for(const Instance& instance : list) {

        const Species& s = *species[instance.species];
        const VegetationLod& lod = s.def.lods[instance.lod];

        if(instance.species != bound_species) {
            command.bindVertexBuffers(0, {s.vertices->buffer}, {0});
            command.bindIndexBuffer(s.indices->buffer, 0, vk::IndexType::eUint32);
            bound_species = instance.species;
        }

        for(auto& draw : lod.draws) {
            const Material& material = s.materials[draw.material];

            vk::Pipeline pipeline = (material.two_sided ? two_sided_pipelines : pipelines)[index(view.target)];
            if(pipeline != bound_pipeline) {
                command.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                bound_pipeline = pipeline;
            }
            if(material.set != bound_set) {
                command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, {material.set}, {});
                bound_set = material.set;
            }

            PlantPC pc;
            pc.model = instance.matrix;
            pc.tint = glm::vec4(glm::vec3(material.tint), s.height * glm::length(glm::vec3(instance.matrix[1])));
            pc.params = glm::vec4(s.def.alpha_cutoff, material.billboard ? 1.f : 0.f,
                normal_maps && material.has_normal ? 1.f : 0.f, subsurface && material.has_subsurface ? 1.f : 0.f);
            command.pushConstants(layout, stages, 0, sizeof(PlantPC), &pc);
            command.drawIndexed(draw.index_count, 1, draw.first_index, (int32_t) lod.first_vertex, 0);
        }
    }

    // The plants far away : the box of each, its fragment shader finds the plant in the volume of the species
    auto* volumes = creg.ctx().find<PlantVolumeSet>();
    if(!boxes.empty() && volumes) {
        command.bindPipeline(vk::PipelineBindPoint::eGraphics, volume_pipelines[index(view.target)]);
        command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, volume_layout, 0, {view.view_set}, {});
        for(const VolumeInstance& instance : boxes) {
            const auto& volume = volumes->species[instance.species];
            VolumePC pc;
            pc.model = instance.matrix;
            pc.low = glm::vec4(volume.min, (float) volume.index);
            pc.size = glm::vec4(volume.size, volume.voxel);
            command.pushConstants(volume_layout, stages, 0, sizeof(VolumePC), &pc);
            command.draw(36, 1, 0, 0);
        }
    }

}

VegetationPass::~VegetationPass() {

    // Workers only touch their job, but do not leave them running past the executor's users
    for(auto& job : jobs) {
        while(!job->done.load()) {
            std::this_thread::yield();
        }
    }
    jobs.clear();

    for(auto pipeline : pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    for(auto pipeline : two_sided_pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    for(auto pipeline : volume_pipelines) {
        if(pipeline) ctx.device->destroy(pipeline);
    }
    ctx.device->destroy(volume_layout);
    for(auto& [path, texture] : textures) {
        if(texture.view) ctx.device->destroy(texture.view);
    }
    for(auto& [image, view] : default_views) {
        ctx.device->destroy(view);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(sampler);
    ctx.device->destroy(atlas_sampler);
    ctx.device->destroy(materialLayout);
    ctx.device->destroy(descPool);

}
