#include "grass_textures.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>
#include <thread>

namespace dy {

namespace {

// Texels of a block against two ends. Eight values : the ends and six between them. Six values : the
// ends, four between them, 0 and 255 (the ground next to a blade leaves both ends to the blade)
struct Fit {
    int a = 0, b = 0;
    uint8_t index[16] {};
    float error = 0.f;
};

// Steps from a to b of an index, -1 for the two fixed values of the six value blocks
int stepsOf(int index, int steps) {
    if(index == 0) return 0;
    if(index == 1) return steps;
    return index - 1 <= steps - 1 ? index - 1 : -1;
}

float valueOf(int a, int b, int index, bool eight) {
    int steps = eight ? 7 : 5;
    int k = stepsOf(index, steps);
    if(k < 0) return index == 6 ? 0.f : 255.f;
    return float((steps - k) * a + k * b) / float(steps);
}

void assign(const uint8_t texels[16], bool eight, Fit& fit) {
    const int steps = eight ? 7 : 5;
    const float a = float(fit.a), b = float(fit.b);
    fit.error = 0.f;
    for(int i = 0; i < 16; i++) {
        float t = float(texels[i]);
        int k = a == b ? 0 : std::clamp(int(std::lround((t - a) / (b - a) * steps)), 0, steps);
        float value = ((steps - k) * a + k * b) / steps;
        int index = k == 0 ? 0 : k == steps ? 1 : k + 1;
        float error = (value - t) * (value - t);
        if(!eight) {
            if(t * t < error) {
                error = t * t;
                index = 6;
            }
            if((255.f - t) * (255.f - t) < error) {
                error = (255.f - t) * (255.f - t);
                index = 7;
            }
        }
        fit.index[i] = uint8_t(index);
        fit.error += error;
    }
}

// The ends that suit the indices best (least squares), kept when they are better
void refine(const uint8_t texels[16], bool eight, Fit& fit) {
    const int steps = eight ? 7 : 5;
    double aa = 0., ab = 0., bb = 0., at = 0., bt = 0.;
    for(int i = 0; i < 16; i++) {
        int k = stepsOf(fit.index[i], steps);
        if(k < 0) continue;
        double w = double(k) / steps, t = texels[i];
        aa += (1. - w) * (1. - w);
        ab += w * (1. - w);
        bb += w * w;
        at += (1. - w) * t;
        bt += w * t;
    }
    double det = aa * bb - ab * ab;
    if(std::abs(det) < 1e-9) return;

    Fit other = fit;
    other.a = std::clamp(int(std::lround((at * bb - bt * ab) / det)), 0, 255);
    other.b = std::clamp(int(std::lround((bt * aa - at * ab) / det)), 0, 255);
    if(eight ? other.a <= other.b : other.a > other.b) return;
    assign(texels, eight, other);
    if(other.error < fit.error) fit = other;
}

Fit fitBlock(const uint8_t texels[16], int a, int b, bool eight) {
    Fit fit;
    fit.a = a;
    fit.b = b;
    assign(texels, eight, fit);
    for(int pass = 0; pass < 2 && fit.error > 0.f; pass++) refine(texels, eight, fit);
    return fit;
}

// fn(row) for every row, on every core when there is enough to do
template<typename F>
void forRows(int rows, size_t work, F fn) {
    unsigned threads = work < 4096 ? 1u : std::max(std::thread::hardware_concurrency(), 1u);
    std::atomic<int> next { 0 };
    auto run = [&]() {
        for(int row = next++; row < rows; row = next++) fn(row);
    };
    std::vector<std::thread> workers;
    for(unsigned i = 1; i < threads; i++) workers.emplace_back(run);
    run();
    for(auto& worker : workers) worker.join();
}

// Blocks of the channels of an RGBA8 level, [d][h][w], one after the other for each block
void appendBlocks(std::vector<uint8_t>& out, const uint8_t* rgba, int w, int h, int d, std::initializer_list<int> channels) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const size_t start = out.size(), row_size = size_t(bw) * 8 * channels.size();
    out.resize(start + row_size * bh * d);
    forRows(bh * d, size_t(bw) * bh * d, [&](int row) {
        const int z = row / bh, by = row % bh;
        size_t at = start + row_size * row;
        for(int bx = 0; bx < bw; bx++) {
            for(int channel : channels) {
                uint8_t texels[16];
                for(int j = 0; j < 4; j++) {
                    for(int i = 0; i < 4; i++) {
                        texels[j * 4 + i] = rgba[((size_t(z) * h + (by * 4 + j) % h) * w + (bx * 4 + i) % w) * 4 + channel];
                    }
                }
                encodeBC4(texels, &out[at]);
                at += 8;
            }
        }
    });
}

void appendTexels(std::vector<uint8_t>& out, const uint8_t* rgba, size_t texels, std::initializer_list<int> channels) {
    size_t at = out.size();
    out.resize(at + texels * channels.size());
    for(size_t i = 0; i < texels; i++) {
        for(int channel : channels) out[at++] = rgba[i * 4 + channel];
    }
}

// Every channel of the slices of an image through its blocks and back
void throughBlocks(std::vector<uint8_t>& rgba, int size, int depth) {
    const int blocks = (size + 3) / 4;
    for(int z = 0; z < depth; z++) {
        uint8_t* slice = &rgba[size_t(z) * size * size * 4];
        for(int by = 0; by < blocks; by++) {
            for(int bx = 0; bx < blocks; bx++) {
                for(int channel = 0; channel < 4; channel++) {
                    uint8_t texels[16], block[8];
                    for(int j = 0; j < 4; j++) {
                        for(int i = 0; i < 4; i++) {
                            texels[j * 4 + i] = slice[(size_t((by * 4 + j) % size) * size + (bx * 4 + i) % size) * 4 + channel];
                        }
                    }
                    encodeBC4(texels, block);
                    decodeBC4(block, texels);
                    for(int j = 0; j < 4 && by * 4 + j < size; j++) {
                        for(int i = 0; i < 4 && bx * 4 + i < size; i++) {
                            slice[(size_t(by * 4 + j) * size + bx * 4 + i) * 4 + channel] = texels[j * 4 + i];
                        }
                    }
                }
            }
        }
    }
}

}

void encodeBC4(const uint8_t texels[16], uint8_t block[8]) {

    int low = 255, high = 0, inner_low = 255, inner_high = 0;
    for(int i = 0; i < 16; i++) {
        int t = texels[i];
        low = std::min(low, t);
        high = std::max(high, t);
        if(t != 0 && t != 255) {
            inner_low = std::min(inner_low, t);
            inner_high = std::max(inner_high, t);
        }
    }

    Fit fit;
    bool eight = false;
    if(low == high) {
        fit.a = fit.b = low;
    } else {
        eight = true;
        fit = fitBlock(texels, high, low, true);
        // 0 and 255 for nothing : the ends are free to hold what is between them
        if(fit.error > 0.f && (low == 0 || high == 255)) {
            if(inner_low > inner_high) inner_low = inner_high = 0;
            Fit six = fitBlock(texels, inner_low, inner_high, false);
            if(six.error < fit.error) {
                fit = six;
                eight = false;
            }
        }
    }

    block[0] = uint8_t(fit.a);
    block[1] = uint8_t(fit.b);
    uint64_t bits = 0;
    for(int i = 0; i < 16; i++) bits |= uint64_t(fit.index[i]) << (3 * i);
    for(int i = 0; i < 6; i++) block[2 + i] = uint8_t(bits >> (8 * i));

}

void decodeBC4(const uint8_t block[8], uint8_t texels[16]) {

    const int a = block[0], b = block[1];
    uint64_t bits = 0;
    for(int i = 0; i < 6; i++) bits |= uint64_t(block[2 + i]) << (8 * i);
    for(int i = 0; i < 16; i++) {
        texels[i] = uint8_t(std::lround(valueOf(a, b, int((bits >> (3 * i)) & 7), a > b)));
    }

}

GrassTextures makeGrassTextures(const std::vector<uint8_t>& image, int size, int depth, int max_levels, bool compressed) {

    GrassTextures out;
    out.size = size;
    out.depth = depth;
    out.compressed = compressed;

    int levels = std::max(max_levels, 1);
    std::vector<uint8_t> chain = rgbaMipChain(image, size, size, depth, levels);

    int w = size, h = size, d = depth;
    size_t offset = 0;
    for(int level = 0; level < levels; level++) {
        const uint8_t* rgba = &chain[offset];
        const size_t texels = size_t(w) * h * d;
        size_t before[3] = {out.height.data.size(), out.normal.data.size(), out.id.data.size()};

        if(compressed) {
            appendBlocks(out.height.data, rgba, w, h, d, {0});
            appendBlocks(out.normal.data, rgba, w, h, d, {1, 2});
            appendBlocks(out.id.data, rgba, w, h, d, {3});
        } else {
            appendTexels(out.height.data, rgba, texels, {0});
            appendTexels(out.normal.data, rgba, texels, {1, 2});
            appendTexels(out.id.data, rgba, texels, {3});
        }

        out.height.level_sizes.push_back(out.height.data.size() - before[0]);
        out.normal.level_sizes.push_back(out.normal.data.size() - before[1]);
        out.id.level_sizes.push_back(out.id.data.size() - before[2]);

        offset += texels * 4;
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
        d = std::max(d / 2, 1);
    }

    return out;

}

GrassRaycast compressedGrassRaycast(const GrassRaycast& data) {

    GrassRaycast out = data;
    throughBlocks(out.oblique, out.size, out.angles);
    throughBlocks(out.top, out.size, 1);
    return out;

}

}
