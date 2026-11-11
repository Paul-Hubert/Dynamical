# `src/renderer/util/`

> [Source](../../../src/renderer/util/) · Parent: [`src/renderer/`](src-renderer.md)

## Summary
Low-level helpers shared by the renderer: the Vulkan-Hpp include configuration (dynamic dispatch), RAII wrappers over VMA buffers and images, a common graphics pipeline builder, texture loading, Vulkan layer/extension checks and debug callback, and OpenXR result/extension helpers. Nothing here touches the registry.

## Files
| File | Contents |
|---|---|
| `vk.h` | Includes `vulkan.hpp` with dynamic dispatch and no nodiscard warnings |
| `vmapp.h` / `vmapp.cpp` | `VmaBuffer`, `VmaImage`: move-only RAII wrappers (buffer/image, allocation, size, offset, memory, image format, `ready` flag set by `Transfer::check`); non-VMA device-local buffer constructor |
| `vma.cpp` | `VMA_IMPLEMENTATION` translation unit |
| `pipeline.h` / `pipeline.cpp` | `GraphicsPipelineDesc` (shaders, specialization, vertex input, layout, render pass, cull, front face, depth test/write, alpha blend), `loadShader` (throws on missing SPIR-V), `createGraphicsPipeline` (triangle list, dynamic viewport/scissor, 1 sample, depth `LESS_OR_EQUAL`) |
| `texture.h` / `texture.cpp` | `loadTextureArray` (stb_image, same-size layers, throws on error), `makeSolidTexture` (1x1 RGBA8) |
| `vk_util.h` / `vk_util.cpp` | `checkLayers`, `checkExtensions` (drop unavailable ones), debug messenger callback (debug builds), `vkCheckResult` |
| `xr.h` / `xr_util.cpp` | OpenXR includes with the Vulkan platform, `xrCheckResult` (logs, returns success), `checkXrLayers`, `checkXrExtensions`, `splitExtensionString` |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
