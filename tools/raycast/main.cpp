// Bakes the lookup textures of the grass shader (logic/grass/grass_bake.h) : a tile of generated grass
// blades, repeated to infinity, is hit by rays along the ground and at 22.5, 45 and 67.5 degrees under the
// horizon from every azimuth, and by rays from the top.
// dynamical bakes the same file by itself when it is missing or stale; this tool is there to bake it ahead,
// to export the grass model and to compare the lookup with real ray tracing.
//
//   raycast [output.bin] [--size 256] [--angles 96,64,64,32] [--low 1,0.5] [--seed 7] [--kind meadow] [--obj tile.obj] [--metres]
//           [--preview preview.png] [--blocks] [--gallery gallery.png]
//
// --kind : what grows on the tile (GrassKind : meadow, fescue, cocksfoot, ryegrass, timothy, oat, clover,
// clover_flower, plantain, dandelion); --metres : the OBJ in the proportions of the plants, not the 1 x 1 tile.
// --gallery : every kind ray traced, the whole tile and a slice of it from the side (docs/grass/13-plants.md).
// --angles : azimuths of each elevation, from the lowest rays up (one number : the same for all).
// --low : heights where the lowest rays start and end (GrassBakeParams::low_from, low_to).
// --preview : orthographic views of the grass from 5 to 90 degrees of elevation, ray traced (left) and
// rebuilt from the textures like the shader does (right), and the mean error of the hit height at each
// elevation over rays from every side.
// --blocks : the preview reads the rays back from the blocks the GPU gets (logic/grass/grass_textures.h),
// and what each channel loses in them is printed.
// (build with -DDYNAMICAL_BUILD_TOOLS=ON)

#include "logic/grass/grass_bake.h"
#include "logic/grass/grass_textures.h"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace dy;

namespace {

const float pi = 3.14159265359f;

glm::vec3 shade(glm::vec3 color, float depth_in_grass, glm::vec3 normal, glm::vec3 direction) {
    const glm::vec3 sun = glm::normalize(glm::vec3(0.5f, 1.f, 0.5f));
    if(glm::dot(normal, direction) > 0.f) normal = -normal;
    float light = std::abs(glm::dot(glm::normalize(normal), sun)) * 0.7f + 0.3f;
    float occlusion = 0.25f + 0.75f * depth_in_grass;
    return color * 1.6f * light * occlusion;
}

// What the texels lose in blocks of 4 x 4
void compareBlocks(const GrassRaycast& data, const GrassRaycast& blocks) {
    const char* names[4] = {"height", "normal x", "normal z", "blade id"};
    for(int c = 0; c < 4; c++) {
        double sum = 0.;
        size_t over = 0, texels = data.oblique.size() / 4;
        int largest = 0;
        for(size_t i = 0; i < texels; i++) {
            int error = std::abs(int(data.oblique[i * 4 + c]) - int(blocks.oblique[i * 4 + c]));
            sum += error;
            over += error > 8;
            largest = std::max(largest, error);
        }
        std::cout << "blocks, " << names[c] << " : mean error " << sum / texels << " / 255, " << 100. * over / texels
            << " % of the texels over 8 / 255, largest " << largest << "\n";
    }
    // The colour map, in BC1 : red, green and blue together
    double sum = 0.;
    size_t over = 0;
    for(size_t i = 0; i < data.oblique_colour.size(); i++) {
        if(i % 4 == 3) continue;
        int error = std::abs(int(data.oblique_colour[i]) - int(blocks.oblique_colour[i]));
        sum += error;
        over += error > 8;
    }
    size_t values = data.oblique_colour.size() / 4 * 3;
    std::cout << "blocks, colour : mean error " << sum / values << " / 255, " << 100. * over / values << " % of the values over 8 / 255\n";
}

bool preview(const GrassRaycast& data, const GrassModelParams& model, const std::string& path) {

    const std::vector<float> elevations = {5.f, 10.f, 15.f, 22.5f, 30.f, 45.f, 60.f, 67.5f, 75.f, 90.f};
    const int panel = 320;
    const float extent = 0.75f;     // half size of a view, in tiles
    const float azimuth = 0.6f;
    const int width = panel * 2, height = panel * int(elevations.size());

    GrassTracer tracer(generateGrassTile(model));
    std::vector<uint8_t> image(size_t(width) * height * 3);

    for(size_t row = 0; row < elevations.size(); row++) {

        float elevation = elevations[row] * pi / 180.f;
        glm::vec2 along(std::cos(azimuth), std::sin(azimuth));
        glm::vec3 direction(along.x * std::cos(elevation), -std::sin(elevation), along.y * std::cos(elevation));
        glm::vec3 right(-along.y, 0.f, along.x);
        glm::vec3 up = glm::cross(right, direction);
        float reach = std::cos(elevation) / std::sin(elevation);

        // Rays closer to the horizontal than grass_max_reach are looked up as if they went down in that
        // many tiles : the hit is where the blade is met on the ground, as a height on that ray
        auto seen = [&](float t) {
            return reach > grass_max_reach ? std::max(1.f - t * reach / grass_max_reach, 0.f) : 1.f - t;
        };

        for(int py = 0; py < panel; py++) {
            for(int px = 0; px < panel; px++) {

                glm::vec3 eye = glm::vec3(0.5f, 0.5f, 0.5f) - direction * 10.f
                    + right * ((px + 0.5f) / panel * 2.f - 1.f) * extent - up * ((py + 0.5f) / panel * 2.f - 1.f) * extent;
                glm::vec3 entry = eye + direction * ((1.f - eye.y) / direction.y);

                // Ray traced
                GrassTracer::Hit hit;
                tracer.trace(entry, direction, hit);
                float truth = 1.f + hit.t * direction.y;
                glm::vec3 traced = shade(hit.colour, truth, hit.normal, direction);

                // Looked up
                GrassLookup lookup = lookupGrass(data, glm::vec2(entry.x, entry.z), along, reach, 0.f);
                glm::vec3 looked = shade(lookup.colour, lookup.baked, lookup.normal, direction);

                size_t y = row * panel + py;
                for(int c = 0; c < 3; c++) {
                    image[(y * width + px) * 3 + c] = uint8_t(std::min(std::pow(traced[c], 1.f / 2.2f), 1.f) * 255.f);
                    image[(y * width + panel + px) * 3 + c] = uint8_t(std::min(std::pow(looked[c], 1.f / 2.2f), 1.f) * 255.f);
                }

            }
        }

        // Error of the hit height over rays from every side, against a constant height for scale
        const int count = 40000;
        uint32_t seed = 12345;
        auto random = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) * (1.f / 16777216.f);
        };
        std::vector<float> truths(count);
        double error = 0., mean = 0., constant = 0.;
        for(int i = 0; i < count; i++) {
            float a = random() * 2.f * pi;
            glm::vec2 heading(std::cos(a), std::sin(a));
            glm::vec2 entry(random(), random());
            GrassTracer::Hit hit;
            tracer.trace(glm::vec3(entry.x, 1.f, entry.y), glm::vec3(heading.x * reach, -1.f, heading.y * reach), hit);
            truths[i] = seen(hit.t);
            mean += truths[i];
            error += std::abs(lookupGrass(data, entry, heading, reach, 0.f).height - truths[i]);
        }
        mean /= count;
        for(float truth : truths) constant += std::abs(truth - mean);

        std::cout << "elevation " << elevations[row] << " : mean error of the hit height " << error / count
            << " of the grass height (" << constant / count << " for a constant height)\n";

    }

    return stbi_write_png(path.c_str(), width, height, 3, image.data(), width * 3) != 0;

}

// 5 x 7 letters of the labels of the gallery, rows from the top
const char* glyph(char c) {
    static const char* letters[26] = {
        "01110100011000111111100011000110001", "11110100011000111110100011000111110", "01110100011000010000100001000101110",
        "11110100011000110001100011000111110", "11111100001000011110100001000011111", "11111100001000011110100001000010000",
        "01110100011000010111100011000101111", "10001100011000111111100011000110001", "01110001000010000100001000010001110",
        "00111000100001000010000101001001100", "10001100101010011000101001001010001", "10000100001000010000100001000011111",
        "10001110111010110101100011000110001", "10001110011010110011100011000110001", "01110100011000110001100011000101110",
        "11110100011000111110100001000010000", "01110100011000110001101011001001101", "11110100011000111110101001001010001",
        "01111100001000001110000010000111110", "11111001000010000100001000010000100", "10001100011000110001100011000101110",
        "10001100011000110001100010101000100", "10001100011000110101101011101110001", "10001100010101000100010101000110001",
        "10001100010101000100001000010000100", "11111000010001000100010001000011111",
    };
    static const char* digits[10] = {
        "01110100011001110101110011000101110", "00100011000010000100001000010001110", "01110100010000100010001000100011111",
        "11110000010000101110000010000111110", "00010001100101010010111110001000010", "11111100001111000001000011000101110",
        "00110010001000011110100011000101110", "11111000010001000100010000100001000", "01110100011000101110100011000101110",
        "01110100011000101111000010001001100",
    };
    if(c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if(c >= '0' && c <= '9') return digits[c - '0'];
    if(c == '.') return "00000000000000000000000000000000100";
    return "00000000000000000000000000000000000";
}

void drawText(std::vector<uint8_t>& image, int width, int x, int y, const std::string& text, int scale, glm::vec3 colour) {
    for(char c : text) {
        const char* bits = glyph(c);
        for(int i = 0; i < 35; i++) {
            if(bits[i] != '1') continue;
            for(int sy = 0; sy < scale; sy++) {
                for(int sx = 0; sx < scale; sx++) {
                    size_t pixel = (size_t(y + (i / 5) * scale + sy) * width + x + (i % 5) * scale + sx) * 3;
                    for(int k = 0; k < 3; k++) image[pixel + k] = uint8_t(colour[k] * 255.f);
                }
            }
        }
        x += 6 * scale;
    }
}

// Orthographic view of what the box (in metres) holds of the tile repeated to infinity, ray traced, the
// parts of the plants in their colours
void view(const GrassTracer& tracer, const GrassKindInfo& info, glm::vec3 lo, glm::vec3 hi, float azimuth, float elevation,
        std::vector<uint8_t>& image, int image_width, int x0, int y0, int w, int h) {

    const GrassModel& model = tracer.model();
    // Metres to tile space; normals come back by the same scale (the inverse transposed of the way back)
    const glm::vec3 to_tile(1.f / model.tile_size, model.max_height / model.height, 1.f / model.tile_size);

    glm::vec2 along(std::cos(azimuth), std::sin(azimuth));
    glm::vec3 direction(along.x * std::cos(elevation), -std::sin(elevation), along.y * std::cos(elevation));
    glm::vec3 right(-along.y, 0.f, along.x);
    glm::vec3 up = glm::cross(right, direction);
    glm::vec3 sun = glm::normalize(glm::vec3(0.f, 1.f, 0.f) - right * 0.55f - glm::vec3(along.x, 0.f, along.y) * 0.35f);

    glm::vec3 middle = 0.5f * (lo + hi);
    float half_x = 0.f, half_y = 0.f;
    for(int corner = 0; corner < 8; corner++) {
        glm::vec3 d = glm::vec3(corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z) - middle;
        half_x = std::max(half_x, std::abs(glm::dot(d, right)));
        half_y = std::max(half_y, std::abs(glm::dot(d, up)));
    }
    float extent = std::max(half_x, half_y * w / h) * 1.05f;

    const int samples = 3;
    const glm::vec3 background(0.92f, 0.93f, 0.95f);

    auto ray = [&](float fx, float fy) {
        glm::vec3 eye = middle - direction * 10.f + right * (fx * extent) - up * (fy * extent * h / w);
        float t_in = 0.f, t_out = 1e9f;
        for(int axis = 0; axis < 3; axis++) {
            if(direction[axis] == 0.f) {
                if(eye[axis] < lo[axis] || eye[axis] > hi[axis]) return background;
                continue;
            }
            float a = (lo[axis] - eye[axis]) / direction[axis], b = (hi[axis] - eye[axis]) / direction[axis];
            t_in = std::max(t_in, std::min(a, b));
            t_out = std::min(t_out, std::max(a, b));
        }
        if(t_in >= t_out) return background;

        glm::vec3 origin = (eye + direction * t_in) * to_tile, d = direction * to_tile;
        GrassTracer::Hit hit;
        // A little further than the box : its floor is the ground
        if(!tracer.trace(origin, d, hit, t_out - t_in + 1e-3f)) return background;

        glm::vec3 point = origin + d * hit.t;
        float y = point.y / model.max_height;
        glm::vec3 normal = glm::normalize(hit.normal * to_tile);
        glm::vec3 colour(0.3f, 0.23f, 0.16f);
        float light = std::max(glm::dot(normal, sun), 0.f);
        if(hit.blade) {
            // The colours the rays bake, a little brighter
            colour = hit.colour * 1.5f * (hit.part == GrassPart::Blade ? 0.8f + 0.4f * hit.id : 1.f);
            light = std::abs(glm::dot(normal, sun));
            colour *= 0.7f + 0.3f * y;
        }
        GrassTracer::Hit shadow;
        glm::vec3 sun_tile = sun * to_tile;
        bool shadowed = tracer.trace(point + hit.normal * 2e-3f + sun_tile * 1e-3f, sun_tile, shadow) && shadow.blade;
        return colour * (0.4f + 0.6f * light) * (shadowed ? 0.6f : 1.f);
    };

    std::atomic<int> next { 0 };
    auto work = [&]() {
        for(int py = next++; py < h; py = next++) {
            for(int px = 0; px < w; px++) {
                glm::vec3 sum(0.f);
                for(int sy = 0; sy < samples; sy++) {
                    for(int sx = 0; sx < samples; sx++) {
                        sum += ray((px + (sx + 0.5f) / samples) / w * 2.f - 1.f, (py + (sy + 0.5f) / samples) / h * 2.f - 1.f);
                    }
                }
                sum /= float(samples * samples);
                size_t pixel = (size_t(y0 + py) * image_width + x0 + px) * 3;
                for(int c = 0; c < 3; c++) image[pixel + c] = uint8_t(std::min(std::pow(sum[c], 1.f / 2.2f), 1.f) * 255.f);
            }
        }
    };
    std::vector<std::thread> workers;
    for(unsigned i = 1; i < std::max(std::thread::hardware_concurrency(), 1u); i++) workers.emplace_back(work);
    work();
    for(auto& worker : workers) worker.join();

}

// Every kind of tile : the whole tile from above (left) and a slice through its middle from the side (right)
bool gallery(GrassModelParams params, const std::string& path) {

    const int view_width = 480, view_height = 340, label = 30, columns = 2;
    const int cell_width = view_width * 2, cell_height = view_height + label;
    const int width = cell_width * columns, height = cell_height * ((grass_kinds + columns - 1) / columns);
    std::vector<uint8_t> image(size_t(width) * height * 3, 255);

    for(int kind = 0; kind < grass_kinds; kind++) {
        params.kind = static_cast<GrassKind>(kind);
        const GrassKindInfo& info = grassKindInfo(params.kind);
        GrassTracer tracer(generateGrassTile(params));
        const GrassModel& model = tracer.model();
        int x = (kind % columns) * cell_width, y = (kind / columns) * cell_height;

        char sizes[64];
        std::snprintf(sizes, sizeof(sizes), "TILE %.2f M   HIGH %.2f M", model.tile_size, model.height);
        drawText(image, width, x + 10, y + 8, std::to_string(kind + 1) + "  " + info.label, 2, glm::vec3(0.1f));
        drawText(image, width, x + 10 + 12 * 24, y + 8, sizes, 2, glm::vec3(0.45f));

        float t = model.tile_size, top = model.height * 1.02f;
        view(tracer, info, glm::vec3(0.f), glm::vec3(t, top, t), 0.6f, 28.f * pi / 180.f, image, width, x, y + label, view_width, view_height);
        view(tracer, info, glm::vec3(0.3f * t, 0.f, 0.3f * t), glm::vec3(0.7f * t, top, 0.7f * t), 0.6f, 10.f * pi / 180.f, image, width,
            x + view_width, y + label, view_width, view_height);

        std::cout << info.name << " : " << model.blades << " parts, " << model.indices.size() / 3 << " triangles, tile of "
            << model.tile_size << " m, " << model.height << " m high\n";
    }

    return stbi_write_png(path.c_str(), width, height, 3, image.data(), width * 3) != 0;

}

int usage() {
    std::cout << "Usage : raycast [output.bin] [--size 256] [--angles 96,64,64,32] [--low 1,0.5] [--seed 7] [--kind meadow] [--obj tile.obj] [--metres]"
        " [--preview preview.png] [--blocks] [--gallery gallery.png]\nKinds :";
    for(int kind = 0; kind < grass_kinds; kind++) std::cout << " " << grassKindInfo(static_cast<GrassKind>(kind)).name;
    std::cout << "\n";
    return 1;
}

// "64" or "64,64,64,32" : azimuths of each elevation
bool parseAngles(const std::string& text, std::array<int, grass_elevations>& angles) {
    size_t start = 0;
    int count = 0;
    while(start <= text.size() && count < grass_elevations) {
        size_t end = std::min(text.find(',', start), text.size());
        angles[count++] = std::atoi(text.substr(start, end - start).c_str());
        start = end + 1;
    }
    if(start <= text.size()) return false;
    if(count == 1) angles.fill(angles[0]);
    else if(count != grass_elevations) return false;
    for(int value : angles) {
        if(value < 1) return false;
    }
    return true;
}

}

int main(int argc, char** argv) {

    GrassBakeParams params;
    std::string output, obj, preview_path, gallery_path;
    bool blocks = false, metres = false;

    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool has_value = i + 1 < argc;
        if(arg == "--size" && has_value) params.size = std::atoi(argv[++i]);
        else if(arg == "--angles" && has_value) {
            if(!parseAngles(argv[++i], params.angles)) return usage();
        }
        else if(arg == "--low" && has_value) {
            std::string low = argv[++i];
            size_t comma = low.find(',');
            if(comma == std::string::npos) return usage();
            params.low_from = float(std::atof(low.substr(0, comma).c_str()));
            params.low_to = float(std::atof(low.substr(comma + 1).c_str()));
        }
        else if(arg == "--seed" && has_value) params.model.seed = uint32_t(std::atoi(argv[++i]));
        else if(arg == "--kind" && has_value) {
            if(!grassKindFromName(argv[++i], params.model.kind)) return usage();
        }
        else if(arg == "--gallery" && has_value) gallery_path = argv[++i];
        else if(arg == "--metres") metres = true;
        else if(arg == "--obj" && has_value) obj = argv[++i];
        else if(arg == "--preview" && has_value) preview_path = argv[++i];
        else if(arg == "--blocks") blocks = true;
        else if(arg.rfind("--", 0) == 0 || !output.empty()) return usage();
        else output = arg;
    }
    if(output.empty() && obj.empty() && preview_path.empty() && gallery_path.empty()) return usage();
    if(params.size < 1) return usage();

    if(!gallery_path.empty() && !gallery(params.model, gallery_path)) {
        std::cerr << "Could not write " << gallery_path << "\n";
        return 1;
    }

    if(!obj.empty()) {
        GrassModel model = generateGrassTile(params.model);
        if(!writeGrassObj(model, obj, metres)) {
            std::cerr << "Could not write " << obj << "\n";
            return 1;
        }
        std::cout << obj << " : " << model.blades << " blades, " << model.indices.size() / 3 << " triangles\n";
    }

    if(output.empty() && preview_path.empty()) return 0;

    auto start = std::chrono::steady_clock::now();
    GrassRaycast data = bakeGrass(params);
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "Baked " << data.size << " x " << data.size << " x (" << data.angles[0] << " + " << data.angles[1] << " + "
        << data.angles[2] << " + " << data.angles[3] << ") rays at " << data.lowElevation() * 180.f / pi << ", 22.5, 45 and 67.5 degrees and "
        << data.size << " x " << data.size << " from the top in " << elapsed.count() << " s\n";

    if(!output.empty() && !saveGrassRaycast(data, output)) {
        std::cerr << "Could not write " << output << "\n";
        return 1;
    }

    if(blocks) {
        GrassRaycast read_back = compressedGrassRaycast(data);
        compareBlocks(data, read_back);
        data = std::move(read_back);
    }

    if(!preview_path.empty() && !preview(data, params.model, preview_path)) {
        std::cerr << "Could not write " << preview_path << "\n";
        return 1;
    }

    return 0;

}
