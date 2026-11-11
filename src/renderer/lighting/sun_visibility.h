#ifndef DY_SUN_VISIBILITY_H
#define DY_SUN_VISIBILITY_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"
#include "renderer/scene_pass.h"

#include <entt/entt.hpp>

#include <array>
#include <vector>

namespace dy {

class Context;

// With ray tracing, what a render target draws its scene in and how the light of the sun reaches the
// swapchain image (docs/lighting.md, "The sun's visibility"). One per target, a set of images per eye :
//  - the scene passes render `colour` (everything but the directional light; alpha : how far over its
//    surface the ray to the sun starts), `sun` (what that light adds where nothing hides it) and the depth
//  - trace() : how much of the sun each pixel sees. Rays to points of its disc within the traced range of
//    the target (`rays`), then blended with what the same point of the world saw the frames before
//    (`visibility`, in turn this frame's and the one before) : found again from the depth and the view of
//    the frame before, refused where it was something else, held to what the pixels around say now
//  - composite() : colour + sun x visibility, into the image the target shows. The UI comes after
class SunVisibility {
public:
    // extents : of each eye. final_pass : the render pass of the images the target shows
    SunVisibility(Context& ctx, entt::registry& reg, RenderTarget target, const std::vector<vk::Extent2D>& extents, vk::RenderPass final_pass);
    ~SunVisibility();

    vk::RenderPass scenePass() const { return scene_pass; }

    // Main thread, once a frame the target draws, before it is recorded
    void beginFrame();

    // Recording, on the thread of the target
    void beginScene(vk::CommandBuffer command, uint32_t eye);
    void endScene(vk::CommandBuffer command);
    void trace(vk::CommandBuffer command, uint32_t eye, vk::DescriptorSet view_set);
    void composite(vk::CommandBuffer command, uint32_t eye, vk::DescriptorSet view_set);

private:
    struct Target {
        VmaImage image;
        vk::ImageView view;
    };
    Target makeTarget(vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect);
    void fullscreen(vk::CommandBuffer command, vk::Extent2D extent, vk::Framebuffer framebuffer, vk::Pipeline pipeline,
        vk::DescriptorSet view_set, vk::DescriptorSet images);

    Context& ctx;
    entt::registry& reg;
    RenderTarget target;

    vk::Format depth_format;
    vk::RenderPass scene_pass, visibility_pass;
    vk::DescriptorSetLayout images_layout;
    vk::DescriptorPool pool;
    vk::PipelineLayout layout;
    vk::Pipeline trace_pipeline, resolve_pipeline, composite_pipeline;
    vk::Sampler nearest, linear;

    struct Eye {
        vk::Extent2D extent;
        Target colour, sun, depth;
        Target rays;                                    // what the rays of this frame found
        std::array<Target, 2> visibility;               // this frame's and the one before, in turn
        vk::Framebuffer scene, trace;
        std::array<vk::Framebuffer, 2> resolve;         // writes visibility[i]
        vk::DescriptorSet trace_set;
        std::array<vk::DescriptorSet, 2> resolve_set;   // reads visibility[1 - i]
        std::array<vk::DescriptorSet, 2> composite_set; // reads visibility[i]
    };
    std::vector<Eye> eyes;

    uint32_t current = 0;       // of the visibility images, the one this frame writes
    uint32_t history = 0;       // frames since the images were made
};

}

#endif
