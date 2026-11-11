#include "animal_pass.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <thread>

#include "renderer/context/context.h"
#include "renderer/context/num_frames.h"
#include "renderer/util/pipeline.h"

#include "logic/components/camera.h"
#include "logic/components/render_settings.h"
#include "logic/components/transform.h"
#include "logic/components/vr.h"
#include "util/log.h"

using namespace dy;

constexpr uint32_t max_materials = 512;
constexpr size_t max_jobs = 6;

namespace {

struct AnimalPC {
    glm::mat4 model;
    glm::vec4 tint;     // w : height of the animal
    glm::vec4 params;   // x : alpha cutoff, y : normal map, z : first joint in the buffer
};

}

AnimalPass::AnimalPass(Context& ctx, entt::registry& reg) : ctx(ctx), reg(reg) {

    reg.storage<Animal>();
    reg.storage<AnimalPose>();
    reg.storage<Transform>();
    reg.ctx().emplace<AnimalStats>();
    species.resize(animalLibrary(reg).entries.size());

    auto poolSizes = std::vector {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, max_materials * 2),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, NUM_FRAMES),
    };
    descPool = ctx.device->createDescriptorPool(vk::DescriptorPoolCreateInfo({}, max_materials + NUM_FRAMES, poolSizes));

    auto bindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment),
    };
    materialLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, bindings));

    auto jointBindings = std::vector {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex),
    };
    jointLayout = ctx.device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, jointBindings));

    vk::SamplerCreateInfo samplerInfo({}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat);
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if(ctx.device.sampler_anisotropy) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 8.f;
    }
    sampler = ctx.device->createSampler(samplerInfo);

    white = makeSolidTexture(ctx, 0xFFFFFFFF);
    flat_normal = makeSolidTexture(ctx, 0xFFFF8080);       // (0.5, 0.5, 1)

    // The joints of every animal drawn in a frame, one buffer per frame in flight
    frames.resize(NUM_FRAMES);
    for(auto& f : frames) {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        f.joints = VmaBuffer(ctx.device, &allocInfo, vk::BufferCreateInfo({}, (vk::DeviceSize) max_joints * sizeof(glm::mat4),
            vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
        VmaAllocationInfo inf;
        vmaGetAllocationInfo(ctx.device, f.joints.allocation, &inf);
        f.pointer = static_cast<glm::mat4*>(inf.pMappedData);

        f.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &jointLayout))[0];
        auto bufInfo = vk::DescriptorBufferInfo(f.joints, 0, VK_WHOLE_SIZE);
        ctx.device->updateDescriptorSets({ vk::WriteDescriptorSet(f.set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &bufInfo, nullptr) }, {});
    }

    auto layouts = std::vector { ctx.view_layout, materialLayout, jointLayout };
    auto pcRange = vk::PushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(AnimalPC));
    layout = ctx.device->createPipelineLayout(vk::PipelineLayoutCreateInfo({}, layouts, pcRange));

}

void AnimalPass::createPipeline(RenderTarget target, vk::RenderPass renderpass) {

    GraphicsPipelineDesc desc;
    desc.vertex_shader = "./resources/shaders/animal.vert.glsl.spv";
    desc.fragment_shader = "./resources/shaders/animal.frag.glsl.spv";
    desc.bindings = {
        vk::VertexInputBindingDescription(0, sizeof(AnimalVertex), vk::VertexInputRate::eVertex),
    };
    desc.attributes = {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(AnimalVertex, position)),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR8G8B8A8Snorm, offsetof(AnimalVertex, normal)),
        vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(AnimalVertex, uv)),
        vk::VertexInputAttributeDescription(3, 0, vk::Format::eR16G16B16A16Uint, offsetof(AnimalVertex, joints)),
        vk::VertexInputAttributeDescription(4, 0, vk::Format::eR8G8B8A8Unorm, offsetof(AnimalVertex, weights)),
    };
    desc.layout = layout;
    desc.renderpass = renderpass;

    pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

    desc.cull = vk::CullModeFlagBits::eNone;
    two_sided_pipelines[index(target)] = createGraphicsPipeline(ctx.device, desc);

}

void AnimalPass::request(uint32_t index) {

    species[index] = std::make_unique<Species>();

    auto job = std::make_shared<Job>();
    job->species = index;
    // The skeleton and the clips are the library's, already loaded by whoever made the animal
    job->def = reg.ctx().get<AnimalLibrary>().species(index);
    jobs.push_back(job);
    if(!job->def) {
        job->error = "its files could not be read";
        job->done.store(true);
        return;
    }

    auto directory = reg.ctx().get<AnimalLibrary>().directory;
    auto loaded = std::make_shared<std::set<std::string>>();
    for(auto& [path, texture] : textures) loaded->insert(path);

    reg.ctx().get<tf::Executor>().silent_async([job, directory, loaded]() {
        try {
            job->vertices = AnimalLibrary::loadGeometry(directory, *job->def, job->indices);

            for(auto& material : job->def->materials) {
                for(int map = 0; map < 2; map++) {
                    const std::string& path = map == 0 ? material.color : material.normal;
                    if(path.empty() || loaded->count(path)) continue;
                    loaded->insert(path);
                    Job::Texture texture;
                    texture.path = path;
                    texture.srgb = map == 0;
                    texture.alpha_cutoff = map == 0 ? material.alpha_cutoff : 0.f;
                    job->textures.push_back(std::move(texture));
                }
            }
            for(auto& texture : job->textures) {
                try {
                    texture.pixels = decodeTextureMips((directory / texture.path).string(), texture.srgb, texture.alpha_cutoff);
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

vk::ImageView AnimalPass::viewOf(const std::shared_ptr<VmaImage>& image) {
    auto it = default_views.find(image.get());
    if(it == default_views.end()) {
        vk::ImageView view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, image->image, vk::ImageViewType::e2D, image->format,
            vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
        it = default_views.emplace(image.get(), view).first;
    }
    return it->second;
}

void AnimalPass::finishJob(Job& job) {

    Species& s = *species[job.species];
    const auto& entry = reg.ctx().get<AnimalLibrary>().entries[job.species];

    if(!job.error.empty()) {
        dy::log(dy::error) << "Animals : cannot load " << entry.id << " : " << job.error << "\n";
        s.state = Species::State::Failed;
        return;
    }

    for(auto& texture : job.textures) {
        if(textures.count(texture.path)) continue;
        Texture t;
        if(texture.failed) {
            dy::log(dy::warning) << "Animals : cannot load texture " << texture.path << " of " << entry.id << "\n";
        } else {
            vk::Format format = texture.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
            t.image = uploadTexture(ctx, texture.pixels, format);
            t.view = ctx.device->createImageView(vk::ImageViewCreateInfo({}, t.image->image, vk::ImageViewType::e2D, format,
                vk::ComponentMapping(), vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, (uint32_t) texture.pixels.level_sizes.size(), 0, 1)));
        }
        // A texture that failed stays in the map without an image, so it is not decoded again
        textures.emplace(texture.path, t);
    }

    s.def = job.def;
    s.height = std::max(s.def->max.y - std::min(s.def->min.y, 0.f), 0.01f);
    // A sphere around the animal as it stands, with room for its other poses
    s.center = 0.5f * (s.def->min + s.def->max);
    s.radius = 0.75f * glm::length(s.def->max - s.def->min) + 0.1f;

    s.vertices = ctx.transfer.createBuffer(job.vertices.data(), vk::BufferCreateInfo({}, job.vertices.size() * sizeof(AnimalVertex),
        vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
    s.indices = ctx.transfer.createBuffer(job.indices.data(), vk::BufferCreateInfo({}, job.indices.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eIndexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));

    for(auto& def : s.def->materials) {
        Material material;
        material.tint = def.tint;
        material.alpha_cutoff = def.alpha_cutoff;
        material.two_sided = def.two_sided || def.alpha_cutoff > 0.f;

        auto view = [&](const std::string& path, const std::shared_ptr<VmaImage>& fallback, bool& present) {
            auto it = textures.find(path);
            present = !path.empty() && it != textures.end() && it->second.image;
            return present ? it->second.view : viewOf(fallback);
        };
        bool has_color = false;
        vk::DescriptorImageInfo images[2] = {
            vk::DescriptorImageInfo(sampler, view(def.color, white, has_color), vk::ImageLayout::eShaderReadOnlyOptimal),
            vk::DescriptorImageInfo(sampler, view(def.normal, flat_normal, material.has_normal), vk::ImageLayout::eShaderReadOnlyOptimal),
        };
        if(!has_color) material.alpha_cutoff = 0.f;

        material.set = ctx.device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo(descPool, 1, &materialLayout))[0];
        ctx.device->updateDescriptorSets({
            vk::WriteDescriptorSet(material.set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[0], nullptr, nullptr),
            vk::WriteDescriptorSet(material.set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &images[1], nullptr, nullptr),
        }, {});

        s.materials.push_back(material);
    }

    s.state = Species::State::Ready;
    dy::log(dy::info) << "Animals : loaded " << entry.id << " (" << s.def->lods.size() << " levels, " << s.def->lods[0].triangles << " triangles, "
        << s.def->parents.size() << " joints, " << s.def->clips.size() << " clips, " << job.textures.size() << " textures)\n";

}

void AnimalPass::update(uint32_t frame_index) {

    frame = frame_index;
    for(auto& list : instances) list.clear();

    for(auto it = jobs.begin(); it != jobs.end();) {
        if((*it)->done.load()) {
            finishJob(**it);
            it = jobs.erase(it);
        } else {
            ++it;
        }
    }

    auto& stats = reg.ctx().get<AnimalStats>();
    stats = AnimalStats();
    for(auto& s : species) {
        if(!s) continue;
        if(s->state == Species::State::Ready) stats.species_loaded++;
        if(s->state == Species::State::Loading) stats.species_loading++;
    }

    auto* settings = reg.ctx().find<RenderSettings>();
    bool draw = !settings || settings->animals;
    float lod_bias = settings ? settings->vegetation_params.lod_bias : 2.f;
    int force_lod = settings ? settings->vegetation_params.force_lod : -1;


    // Where each target looks from

    struct Eye {
        bool active = false;
        glm::vec3 position = glm::vec3(0.f);
        float tan_half_fov = 1.f;
        // Animals outside this cone around `forward` are not drawn
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


    // One level of detail per animal and target, its joints once

    glm::mat4* joints = frames[frame_index].pointer;
    uint32_t joint_count = 0;
    bool unrequested = false, full = false;

    for(auto [entity, transform, animal, pose] : reg.view<const Transform, const Animal, const AnimalPose>().each()) {

        stats.animals++;
        if(animal.species >= species.size()) continue;

        if(!species[animal.species]) {
            if(jobs.size() < max_jobs) request(animal.species);
            else unrequested = true;
            continue;
        }
        const Species& s = *species[animal.species];
        if(s.state != Species::State::Ready || !draw) continue;
        if(pose.skin.size() != s.def->parents.size()) continue;

        const float scale = transform.scale.y;
        const float height = s.height * scale;
        const glm::vec3 center = transform.position + transform.rotation * (s.center * scale);
        const float radius = s.radius * scale;
        uint32_t first_joint = ~0u;
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

            int lod = (int) s.def->lods.size() - 1;
            if(force_lod >= 0) {
                lod = std::min(force_lod, lod);
            } else {
                float distance = std::max(glm::distance(eye.position, center), 0.01f);
                float screen_height = height / (2.f * distance * eye.tan_half_fov) * lod_bias;
                for(size_t i = 0; i < s.def->lods.size(); i++) {
                    if(screen_height >= s.def->lods[i].screen_height) {
                        lod = (int) i;
                        break;
                    }
                }
            }

            if(first_joint == ~0u) {
                if(joint_count + pose.skin.size() > max_joints) {
                    full = true;
                    break;
                }
                first_joint = joint_count;
                std::memcpy(joints + joint_count, pose.skin.data(), pose.skin.size() * sizeof(glm::mat4));
                joint_count += (uint32_t) pose.skin.size();
                matrix = transform.matrix();
            }
            instances[target].push_back({ animal.species, (uint32_t) lod, first_joint, matrix });
        }
    }
    if(unrequested) stats.species_loading++;
    if(full) dy::log(dy::warning) << "Animals : more than " << max_joints << " joints in view, some animals are not drawn\n";
    stats.joints = joint_count;

    for(auto& list : instances) {
        std::sort(list.begin(), list.end(), [](const Instance& a, const Instance& b) {
            return a.species != b.species ? a.species < b.species : a.lod < b.lod;
        });
    }

    for(auto& instance : instances[index(RenderTarget::Desktop)]) {
        stats.drawn++;
        stats.triangles += species[instance.species]->def->lods[instance.lod].triangles;
    }

}

void AnimalPass::record(vk::CommandBuffer command, const RenderView& view) {

    const entt::registry& creg = reg;

    const auto& list = instances[index(view.target)];
    if(list.empty()) return;

    auto* settings = creg.ctx().find<RenderSettings>();
    bool normal_maps = !settings || settings->vegetation_params.normal_maps;

    const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::Pipeline bound_pipeline;
    vk::DescriptorSet bound_set;
    uint32_t bound_species = ~0u;

    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, {view.view_set}, {});
    command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 2, {frames[frame].set}, {});

    for(const Instance& instance : list) {

        const Species& s = *species[instance.species];
        const AnimalLod& lod = s.def->lods[instance.lod];

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

            AnimalPC pc;
            pc.model = instance.matrix;
            pc.tint = glm::vec4(glm::vec3(material.tint), s.height * glm::length(glm::vec3(instance.matrix[1])));
            pc.params = glm::vec4(material.alpha_cutoff, normal_maps && material.has_normal ? 1.f : 0.f, (float) instance.first_joint, 0.f);
            command.pushConstants(layout, stages, 0, sizeof(AnimalPC), &pc);
            command.drawIndexed(draw.index_count, 1, draw.first_index, 0, 0);
        }
    }

}

AnimalPass::~AnimalPass() {

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
    for(auto& [path, texture] : textures) {
        if(texture.view) ctx.device->destroy(texture.view);
    }
    for(auto& [image, view] : default_views) {
        ctx.device->destroy(view);
    }
    ctx.device->destroy(layout);
    ctx.device->destroy(sampler);
    ctx.device->destroy(materialLayout);
    ctx.device->destroy(jointLayout);
    ctx.device->destroy(descPool);

}
