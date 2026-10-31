#ifndef DY_IMGDIFF_H
#define DY_IMGDIFF_H

#include <cstdint>
#include <string>
#include <vector>

namespace dy::imgdiff {

// 8-bit RGB image
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

// Thresholds in 1/255 units (phases.md, capture facility)
struct Thresholds {
    double max_mae = 1.5;           // mean absolute error over all channels
    double max_fraction = 0.005;    // of pixels whose largest channel difference exceeds pixel_threshold
    int pixel_threshold = 24;
};

struct Result {
    bool same_size = false;
    double mae = 0.;                // 1/255 units
    double fraction = 0.;           // of pixels over the pixel threshold
    uint64_t pixels_over = 0;
    int max_difference = 0;
    bool pass = false;
};

Result compare(const Image& a, const Image& b, const Thresholds& thresholds);

// Golden dimmed to grey, differing pixels on top: dark red for small differences, yellow then white for large
Image heatmap(const Image& a, const Image& b);

bool load(const std::string& path, Image& image);
bool save(const std::string& path, const Image& image);

}

#endif
