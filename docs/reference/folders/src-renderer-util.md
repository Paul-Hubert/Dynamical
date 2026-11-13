# src/renderer/util

Helpers of the renderer with no state of their own and no registry access: the Vulkan-Hpp include, VMA and its
RAII wrappers, graphics pipelines and shader loading, texture loading and mip filtering, Vulkan and OpenXR result
checks and extension lists.

## Files

| File | What |
|---|---|
| [`vk.h`](../../../src/renderer/util/vk.h) | includes `vulkan/vulkan.hpp` with the dynamic dispatcher (`VULKAN_HPP_DISPATCH_LOADER_DYNAMIC`) |
| [`vk_util.h`](../../../src/renderer/util/vk_util.h) / [`vk_util.cpp`](../../../src/renderer/util/vk_util.cpp) | `checkLayers`, `checkExtensions` (keep the available ones), the debug messenger callback (warnings and up, debug builds), `vkCheckResult` |
| [`vma.cpp`](../../../src/renderer/util/vma.cpp) | the VMA implementation (`VMA_IMPLEMENTATION`) |
| [`vmapp.h`](../../../src/renderer/util/vmapp.h) / [`vmapp.cpp`](../../../src/renderer/util/vmapp.cpp) | `VmaBuffer`, `VmaImage`: move-only owners of a buffer / image and its allocation (VMA, or device-local memory) |
| [`pipeline.h`](../../../src/renderer/util/pipeline.h) / [`pipeline.cpp`](../../../src/renderer/util/pipeline.cpp) | `GraphicsPipelineDesc` (the common state: one subpass, dynamic viewport and scissor, cull, depth, blend, colour attachments, stencil), `createGraphicsPipeline`, `loadShader` (SPIR-V file), `sceneFragmentShader(ctx, name)` (`<name>.frag.glsl.spv` or `.rt.spv` with ray tracing) |
| [`texture.h`](../../../src/renderer/util/texture.h) / [`texture.cpp`](../../../src/renderer/util/texture.cpp) | `loadTextureArray`, `makeSolidTexture`, `TexturePixels` + `decodeTextureMips` (stb decode and CPU mip chain: sRGB-correct, alpha weighted, coverage kept for alpha test; worker-thread safe), `uploadTexture`, `uploadTextureArray` |
| [`xr.h`](../../../src/renderer/util/xr.h) / [`xr_util.cpp`](../../../src/renderer/util/xr_util.cpp) | OpenXR includes, `xrCheckResult` (logs failures), `checkXrLayers`, `checkXrExtensions`, `splitExtensionString` |

## Systems

None. Nothing here touches the registry.

## Components and context variables

None.

## Dependencies

- `src/renderer/context` (`Device`, `Context`, `Transfer` for the uploads), `src/util` (log, `readFile`);
  Vulkan, VMA, stb_image, OpenXR.

## Design docs

[docs/lighting.md](../../lighting.md) (the `.rt` variants of the scene fragment shaders), [docs/vegetation.md](../../vegetation.md) (mip filtering of the leaves).
