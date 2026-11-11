#include "probes.h"

#include <algorithm>
#include <cmath>

using namespace dy;

namespace {

// Spreads the ones of a mask by `radius` probes along each axis in turn : a box
void dilate(std::vector<uint8_t>& mask, int radius) {
    const int n = probes::size;
    std::vector<uint8_t> line(n);
    for(int axis = 0; axis < 3; axis++) {
        const int stride = axis == 0 ? 1 : axis == 1 ? n : n * n;
        for(int a = 0; a < n; a++) {
            for(int b = 0; b < n; b++) {
                // The line of probes along the axis through (a, b) of the two others
                int start = axis == 0 ? (a * n + b) * n : axis == 1 ? a * n * n + b : a * n + b;
                for(int i = 0; i < n; i++) line[i] = mask[start + i * stride];
                for(int i = 0; i < n; i++) {
                    uint8_t any = 0;
                    for(int j = std::max(i - radius, 0); j <= std::min(i + radius, n - 1); j++) any |= line[j];
                    mask[start + i * stride] = any;
                }
            }
        }
    }
}

}

ProbeClasses dy::classifyProbes(const std::vector<float>& densities, glm::vec3 origin, const std::vector<PlantBounds>& plants) {

    const int n = probes::size;
    ProbeClasses out;
    out.classes.assign(probes::count, probes::Open);

    std::vector<uint8_t> rock(probes::count, 0), air(probes::count, 0);
    for(int z = 0; z < n; z++) {
        for(int y = 0; y < n; y++) {
            for(int x = 0; x < n; x++) {
                bool solid = densities[chunk::index(x + chunk::border, y + chunk::border, z + chunk::border)] > 0.f;
                rock[probes::index(x, y, z)] = solid;
                air[probes::index(x, y, z)] = !solid;
            }
        }
    }

    // The air within reach of the ground, and the ground next to the air
    const int reach = (int) std::ceil(probes::reach / probes::spacing);
    std::vector<uint8_t> near_rock = rock, near_air = air;
    dilate(near_rock, reach);
    dilate(near_air, 1);

    // The air within reach of a plant : a box around its bounds
    std::vector<uint8_t> near_plant(probes::count, 0);
    for(const PlantBounds& plant : plants) {
        glm::vec3 low = (plant.base - glm::vec3(plant.radius + probes::reach, probes::spacing, plant.radius + probes::reach) - origin) / probes::spacing;
        glm::vec3 high = (plant.base + glm::vec3(plant.radius + probes::reach, plant.height + probes::reach, plant.radius + probes::reach) - origin) / probes::spacing;
        glm::ivec3 from = glm::max(glm::ivec3(glm::ceil(low)), glm::ivec3(0));
        glm::ivec3 to = glm::min(glm::ivec3(glm::floor(high)), glm::ivec3(n - 1));
        for(int z = from.z; z <= to.z; z++) {
            for(int y = from.y; y <= to.y; y++) {
                for(int x = from.x; x <= to.x; x++) near_plant[probes::index(x, y, z)] = 1;
            }
        }
    }

    for(uint32_t i = 0; i < probes::count; i++) {
        if(rock[i]) {
            out.classes[i] = near_air[i] ? probes::Fill : probes::Rock;
            if(near_air[i]) out.fill.push_back(i);
        } else if(near_rock[i] || near_plant[i]) {
            out.classes[i] = probes::Active;
            out.active.push_back(i);
        }
    }

    return out;

}

glm::vec3 dy::fibonacciDirection(int i, int n) {
    const float golden = 2.39996323f;   // the golden angle
    float y = 1.f - (2.f * (float) i + 1.f) / (float) n;
    float radius = std::sqrt(std::max(1.f - y * y, 0.f));
    float angle = golden * (float) i;
    return glm::vec3(radius * std::cos(angle), y, radius * std::sin(angle));
}

int ProbeBricks::find(glm::ivec3 chunk) const {
    auto it = owners.find(chunk);
    return it != owners.end() ? it->second : -1;
}

int ProbeBricks::allocate(glm::ivec3 chunk) {

    if(int brick = find(chunk); brick >= 0) return brick;
    if(table[cell(chunk)] != 0) return -1;

    int brick = -1;
    for(size_t i = 0; i < bricks.size(); i++) {
        if(!bricks[i]) {
            brick = (int) i;
            break;
        }
    }
    if(brick < 0) {
        if((int) bricks.size() >= max_bricks) return -1;
        brick = (int) bricks.size();
        bricks.push_back(0);
    }

    bricks[brick] = 1;
    owners[chunk] = brick;
    table[cell(chunk)] = (uint16_t) (brick + 1);
    return brick;

}

void ProbeBricks::release(glm::ivec3 chunk) {
    auto it = owners.find(chunk);
    if(it == owners.end()) return;
    bricks[it->second] = 0;
    table[cell(chunk)] = 0;
    owners.erase(it);
}
