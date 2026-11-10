// Bakes the lookup textures of the grass shader (logic/grass/grass_bake.h) : a tile of generated grass
// blades, repeated to infinity, is hit by rays at 45 degrees from every azimuth and by rays from the top.
// dynamical bakes the same file by itself when it is missing or stale; this tool is there to bake it ahead,
// to export the grass model and to compare the lookup with real ray tracing.
//
//   raycast [output.bin] [--size 256] [--angles 64] [--seed 7] [--obj tile.obj] [--preview preview.png]
//
// --preview : orthographic views of the grass from 15 to 90 degrees of elevation, ray traced (left) and
// rebuilt from the textures like the shader does (right), and the mean error of the hit height at each
// elevation over rays from every side.
// (build with -DDYNAMICAL_BUILD_TOOLS=ON)

#include "logic/grass/grass_bake.h"

#include <stb_image_write.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace dy;

namespace {

const float pi = 3.14159265359f;

glm::vec3 shade(float height, float depth_in_grass, glm::vec3 normal, glm::vec3 direction) {
    const glm::vec3 sun = glm::normalize(glm::vec3(0.5f, 1.f, 0.5f));
    if(glm::dot(normal, direction) > 0.f) normal = -normal;
    float light = std::abs(glm::dot(glm::normalize(normal), sun)) * 0.7f + 0.3f;
    float occlusion = 0.25f + 0.75f * depth_in_grass;
    glm::vec3 color = glm::mix(glm::vec3(0.08f, 0.25f, 0.05f), glm::vec3(0.35f, 0.7f, 0.2f), height);
    return color * light * occlusion;
}

bool preview(const GrassRaycast& data, const GrassModelParams& model, const std::string& path) {

    const std::vector<float> elevations = {15.f, 30.f, 45.f, 60.f, 75.f, 90.f};
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

        for(int py = 0; py < panel; py++) {
            for(int px = 0; px < panel; px++) {

                glm::vec3 eye = glm::vec3(0.5f, 0.5f, 0.5f) - direction * 10.f
                    + right * ((px + 0.5f) / panel * 2.f - 1.f) * extent - up * ((py + 0.5f) / panel * 2.f - 1.f) * extent;
                glm::vec3 entry = eye + direction * ((1.f - eye.y) / direction.y);

                // Ray traced
                GrassTracer::Hit hit;
                tracer.trace(entry, direction, hit);
                float truth = 1.f + hit.t * direction.y;
                glm::vec3 traced = shade(truth, truth, hit.normal, direction);

                // Looked up
                GrassLookup lookup = lookupGrass(data, glm::vec2(entry.x, entry.z), along, reach, 0.f);
                glm::vec3 looked = shade(lookup.baked, lookup.baked, lookup.normal, direction);

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
            truths[i] = 1.f - hit.t;
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

int usage() {
    std::cout << "Usage : raycast [output.bin] [--size 256] [--angles 64] [--seed 7] [--obj tile.obj] [--preview preview.png]\n";
    return 1;
}

}

int main(int argc, char** argv) {

    GrassBakeParams params;
    std::string output, obj, preview_path;

    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool has_value = i + 1 < argc;
        if(arg == "--size" && has_value) params.size = std::atoi(argv[++i]);
        else if(arg == "--angles" && has_value) params.angles = std::atoi(argv[++i]);
        else if(arg == "--seed" && has_value) params.model.seed = uint32_t(std::atoi(argv[++i]));
        else if(arg == "--obj" && has_value) obj = argv[++i];
        else if(arg == "--preview" && has_value) preview_path = argv[++i];
        else if(arg.rfind("--", 0) == 0 || !output.empty()) return usage();
        else output = arg;
    }
    if(output.empty() && obj.empty() && preview_path.empty()) return usage();
    if(params.size < 1 || params.angles < 1) return usage();

    if(!obj.empty()) {
        GrassModel model = generateGrassTile(params.model);
        if(!writeGrassObj(model, obj)) {
            std::cerr << "Could not write " << obj << "\n";
            return 1;
        }
        std::cout << obj << " : " << model.blades << " blades, " << model.indices.size() / 3 << " triangles\n";
    }

    if(output.empty() && preview_path.empty()) return 0;

    auto start = std::chrono::steady_clock::now();
    GrassRaycast data = bakeGrass(params);
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "Baked " << data.size << " x " << data.size << " x " << data.angles << " rays at 45 degrees and "
        << data.size << " x " << data.size << " from the top in " << elapsed.count() << " s\n";

    if(!output.empty() && !saveGrassRaycast(data, output)) {
        std::cerr << "Could not write " << output << "\n";
        return 1;
    }

    if(!preview_path.empty() && !preview(data, params.model, preview_path)) {
        std::cerr << "Could not write " << preview_path << "\n";
        return 1;
    }

    return 0;

}
