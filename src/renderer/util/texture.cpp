#include "texture.h"

#include "renderer/context/context.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
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

namespace {

struct SrgbTables {
    float to_linear[256];
    uint8_t to_srgb[4096];
    SrgbTables() {
        for(int i = 0; i < 256; i++) {
            float c = i / 255.f;
            to_linear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for(int i = 0; i < 4096; i++) {
            float c = i / 4095.f;
            float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
            to_srgb[i] = (uint8_t) std::clamp(s * 255.f + 0.5f, 0.f, 255.f);
        }
    }
};

float coverage(const float* alpha, size_t count, float scale, float cutoff) {
    size_t covered = 0;
    for(size_t i = 0; i < count; i++) {
        if(alpha[i * 4 + 3] * scale > cutoff) covered++;
    }
    return (float) covered / (float) count;
}

}

TexturePixels dy::decodeTextureMips(const std::string& path, bool srgb, float alpha_cutoff) {

    static const SrgbTables tables;

    int w, h, channels;
    stbi_uc* decoded = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if(!decoded) {
        throw std::runtime_error("Could not load texture " + path);
    }

    TexturePixels out;
    out.width = (uint32_t) w;
    out.height = (uint32_t) h;

    size_t total = 0;
    for(int lw = w, lh = h;; lw = std::max(lw / 2, 1), lh = std::max(lh / 2, 1)) {
        out.level_sizes.push_back((size_t) lw * lh * 4);
        total += out.level_sizes.back();
        if(lw == 1 && lh == 1) break;
    }
    out.data.resize(total);
    std::memcpy(out.data.data(), decoded, out.level_sizes[0]);
    stbi_image_free(decoded);

    // Every level in floats : linear color premultiplied by alpha (so the color of a texel weighs what it
    // covers), alpha in [0, 1]. Images without transparency keep plain colors.
    bool transparent = false;
    for(size_t i = 0; i < (size_t) w * h; i++) transparent |= out.data[i * 4 + 3] != 255;
    const bool weighted = srgb && transparent;

    struct Level {
        int w, h;
        std::vector<float> texels;
    };
    std::vector<Level> levels(out.level_sizes.size());
    levels[0] = { w, h, std::vector<float>((size_t) w * h * 4) };
    for(size_t i = 0; i < (size_t) w * h; i++) {
        const uint8_t* p = out.data.data() + i * 4;
        float a = p[3] / 255.f;
        for(int c = 0; c < 3; c++) {
            levels[0].texels[i * 4 + c] = (srgb ? tables.to_linear[p[c]] : p[c] / 255.f) * (weighted ? a : 1.f);
        }
        levels[0].texels[i * 4 + 3] = a;
    }
    for(size_t l = 1; l < levels.size(); l++) {
        const Level& from = levels[l - 1];
        Level& to = levels[l];
        to.w = std::max(from.w / 2, 1);
        to.h = std::max(from.h / 2, 1);
        to.texels.resize((size_t) to.w * to.h * 4);
        for(int y = 0; y < to.h; y++) {
            int y0 = std::min(y * 2, from.h - 1), y1 = std::min(y * 2 + 1, from.h - 1);
            for(int x = 0; x < to.w; x++) {
                int x0 = std::min(x * 2, from.w - 1), x1 = std::min(x * 2 + 1, from.w - 1);
                const float* a = &from.texels[((size_t) y0 * from.w + x0) * 4];
                const float* b = &from.texels[((size_t) y0 * from.w + x1) * 4];
                const float* c = &from.texels[((size_t) y1 * from.w + x0) * 4];
                const float* d = &from.texels[((size_t) y1 * from.w + x1) * 4];
                float* o = &to.texels[((size_t) y * to.w + x) * 4];
                for(int k = 0; k < 4; k++) o[k] = (a[k] + b[k] + c[k] + d[k]) * 0.25f;
            }
        }
    }

    // Bark has an alpha channel too, all of it above the cutoff : nothing to keep there
    const float target = alpha_cutoff > 0.f && transparent ? coverage(levels[0].texels.data(), (size_t) w * h, 1.f, alpha_cutoff) : 1.f;
    const bool keep_coverage = target < 0.999f;
    out.cutout = keep_coverage;

    // From the smallest level up : colors back from premultiplied, and the texels with nothing in them take
    // the color of the level under theirs, that is of the leaves around. SpeedTree atlases are black where
    // they are transparent, the texture filter would darken the edge of every leaf.
    std::vector<size_t> offsets(levels.size(), 0);
    for(size_t l = 1; l < levels.size(); l++) offsets[l] = offsets[l - 1] + out.level_sizes[l - 1];

    for(size_t l = levels.size(); l-- > 0;) {
        Level& level = levels[l];
        const Level* parent = l + 1 < levels.size() ? &levels[l + 1] : nullptr;
        size_t count = (size_t) level.w * level.h;

        float scale = 1.f;
        if(keep_coverage && l > 0) {
            // Coverage grows with the scale : bisection
            float low = 1.f, high = 8.f;
            for(int i = 0; i < 12; i++) {
                scale = (low + high) * 0.5f;
                if(coverage(level.texels.data(), count, scale, alpha_cutoff) < target) low = scale;
                else high = scale;
            }
            scale = (low + high) * 0.5f;
        }

        uint8_t* dst = out.data.data() + offsets[l];
        for(int y = 0; y < level.h; y++) {
            for(int x = 0; x < level.w; x++) {
                size_t i = (size_t) y * level.w + x;
                float* p = &level.texels[i * 4];
                float alpha = p[3];

                if(weighted) {
                    // Already resolved to plain colors
                    const float* fill = parent ? &parent->texels[((size_t) std::min(y / 2, parent->h - 1) * parent->w + std::min(x / 2, parent->w - 1)) * 4] : nullptr;
                    float own = std::min(alpha * 64.f, 1.f);
                    for(int c = 0; c < 3; c++) {
                        float color = alpha > 1e-5f ? p[c] / alpha : 0.f;
                        p[c] = fill ? fill[c] * (1.f - own) + color * own : color;
                    }
                }

                // The first level keeps the file's texels, but for the ones that are fully transparent
                if(l == 0 && !(weighted && dst[i * 4 + 3] == 0)) continue;
                for(int c = 0; c < 3; c++) {
                    float v = std::clamp(p[c], 0.f, 1.f);
                    dst[i * 4 + c] = srgb ? tables.to_srgb[(int) (v * 4095.f + 0.5f)] : (uint8_t) (v * 255.f + 0.5f);
                }
                if(l > 0) dst[i * 4 + 3] = (uint8_t) (std::clamp(alpha * scale, 0.f, 1.f) * 255.f + 0.5f);
            }
        }
    }

    return out;

}

std::shared_ptr<VmaImage> dy::uploadTexture(Context& ctx, const TexturePixels& pixels, vk::Format format) {

    vk::ImageCreateInfo info({}, vk::ImageType::e2D, format, vk::Extent3D(pixels.width, pixels.height, 1), (uint32_t) pixels.level_sizes.size(), 1, vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);

    return ctx.transfer.createImageMips(pixels.data.data(), pixels.level_sizes, info, vk::ImageLayout::eShaderReadOnlyOptimal);

}

std::shared_ptr<VmaImage> dy::makeSolidTexture(Context& ctx, uint32_t rgba) {

    vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(1, 1, 1), 1, 1, vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);

    return ctx.transfer.createImage(&rgba, sizeof(rgba), info, vk::ImageLayout::eShaderReadOnlyOptimal);

}
