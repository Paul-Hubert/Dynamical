#include "imgdiff.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdlib>

using namespace dy::imgdiff;

Result dy::imgdiff::compare(const Image& a, const Image& b, const Thresholds& thresholds) {

    Result result;
    result.same_size = a.width == b.width && a.height == b.height && a.rgb.size() == b.rgb.size();
    if(!result.same_size || a.rgb.empty()) return result;

    const size_t pixels = (size_t) a.width * a.height;
    uint64_t sum = 0;
    for(size_t i = 0; i < pixels; i++) {
        int largest = 0;
        for(int c = 0; c < 3; c++) {
            int d = std::abs((int) a.rgb[i * 3 + c] - (int) b.rgb[i * 3 + c]);
            sum += d;
            largest = std::max(largest, d);
        }
        if(largest > thresholds.pixel_threshold) result.pixels_over++;
        result.max_difference = std::max(result.max_difference, largest);
    }

    result.mae = (double) sum / (double) (pixels * 3);
    result.fraction = (double) result.pixels_over / (double) pixels;
    result.pass = result.mae <= thresholds.max_mae && result.fraction <= thresholds.max_fraction;
    return result;

}

Image dy::imgdiff::heatmap(const Image& a, const Image& b) {

    Image out;
    out.width = std::min(a.width, b.width);
    out.height = std::min(a.height, b.height);
    out.rgb.resize((size_t) out.width * out.height * 3);

    for(int y = 0; y < out.height; y++) {
        for(int x = 0; x < out.width; x++) {
            const uint8_t* pa = &a.rgb[((size_t) y * a.width + x) * 3];
            const uint8_t* pb = &b.rgb[((size_t) y * b.width + x) * 3];
            int largest = 0;
            for(int c = 0; c < 3; c++) largest = std::max(largest, std::abs((int) pa[c] - (int) pb[c]));
            uint8_t* o = &out.rgb[((size_t) y * out.width + x) * 3];
            if(largest == 0) {
                uint8_t grey = (uint8_t) ((pb[0] * 77 + pb[1] * 150 + pb[2] * 29) >> 10);  // a quarter of the luminance
                o[0] = o[1] = o[2] = grey;
            } else {
                float t = std::min(1.f, largest / 64.f);
                o[0] = (uint8_t) (128 + 127 * std::min(1.f, t * 2.f));
                o[1] = (uint8_t) (255 * std::max(0.f, t * 2.f - 1.f));
                o[2] = (uint8_t) (largest > 128 ? 255 : 0);
            }
        }
    }
    return out;

}

bool dy::imgdiff::load(const std::string& path, Image& image) {
    int channels;
    uint8_t* data = stbi_load(path.c_str(), &image.width, &image.height, &channels, 3);
    if(!data) return false;
    image.rgb.assign(data, data + (size_t) image.width * image.height * 3);
    stbi_image_free(data);
    return true;
}

bool dy::imgdiff::save(const std::string& path, const Image& image) {
    return stbi_write_png(path.c_str(), image.width, image.height, 3, image.rgb.data(), image.width * 3) != 0;
}
