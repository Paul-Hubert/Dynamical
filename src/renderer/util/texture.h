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

}

#endif
