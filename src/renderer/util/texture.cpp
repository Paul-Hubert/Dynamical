#include "texture.h"

#include "renderer/context/context.h"

#include <stb_image.h>

#include <cstring>
#include <stdexcept>

using namespace dy;

std::shared_ptr<VmaImage> dy::loadTextureArray(Context& ctx, const std::vector<std::string>& paths, vk::Format format, uint32_t& width, uint32_t& height) {

    std::vector<uint8_t> pixels;
    width = height = 0;

    for(auto& path : paths) {
        int w, h, channels;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if(!data) {
            throw std::runtime_error("Could not load texture " + path);
        }
        if(width == 0) {
            width = w;
            height = h;
        } else if((uint32_t) w != width || (uint32_t) h != height) {
            stbi_image_free(data);
            throw std::runtime_error("Texture array layers must have the same size : " + path);
        }
        size_t offset = pixels.size();
        pixels.resize(offset + (size_t) w * h * 4);
        std::memcpy(pixels.data() + offset, data, (size_t) w * h * 4);
        stbi_image_free(data);
    }

    vk::ImageCreateInfo info({}, vk::ImageType::e2D, format, vk::Extent3D(width, height, 1), 1, (uint32_t) paths.size(), vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);

    return ctx.transfer.createImage(pixels.data(), pixels.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);

}

std::shared_ptr<VmaImage> dy::makeSolidTexture(Context& ctx, uint32_t rgba) {

    vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(1, 1, 1), 1, 1, vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);

    return ctx.transfer.createImage(&rgba, sizeof(rgba), info, vk::ImageLayout::eShaderReadOnlyOptimal);

}
