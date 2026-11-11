#ifndef DY_TEXTURE_H
#define DY_TEXTURE_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <memory>
#include <string>
#include <vector>

namespace dy {

class Context;

// Loads images of the same size into the layers of a 2D array texture (RGBA8), uploaded through the transfer queue
std::shared_ptr<VmaImage> loadTextureArray(Context& ctx, const std::vector<std::string>& paths, vk::Format format, uint32_t& width, uint32_t& height);

// Single texel texture, used when a material has no texture
std::shared_ptr<VmaImage> makeSolidTexture(Context& ctx, uint32_t rgba);

// An RGBA8 image and its mip chain, the largest level first
struct TexturePixels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
    std::vector<size_t> level_sizes;
    bool cutout = false;    // some texels are under the alpha cutoff asked for (leaves, not bark)
};

// Decodes an image and filters its mip levels on the CPU. No Vulkan : can run on a worker thread.
// srgb : colors are averaged in linear space, weighted by alpha, and transparent texels take the color of the
// texels around them (the file's own fully transparent texels included) so the filter does not bleed black.
// alpha_cutoff > 0 : the alpha of the smaller levels is scaled to keep the coverage of the alpha test,
// else foliage thins out with the distance. Throws std::runtime_error when the file cannot be decoded.
TexturePixels decodeTextureMips(const std::string& path, bool srgb, float alpha_cutoff = 0.f);

// Uploads through the transfer queue (main thread)
std::shared_ptr<VmaImage> uploadTexture(Context& ctx, const TexturePixels& pixels, vk::Format format);

// The same for the layers of a 2D array : images of the same size, with their mip chains
std::shared_ptr<VmaImage> uploadTextureArray(Context& ctx, const std::vector<TexturePixels>& layers, vk::Format format);

}

#endif
