#include "forest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "vegetation.h"

#include "logic/components/transform.h"
#include "logic/terrain/chunk.h"
#include "logic/terrain/terrain_generator.h"
#include "scene/run_args.h"
#include "scene/system_registry.h"
#include "util/log.h"

using namespace dy;

namespace {

float smoothstep(float a, float b, float x) {
    if(b <= a) return x < a ? 0.f : 1.f;
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

uint32_t mix(uint32_t h, uint32_t value) {
    h ^= value + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return h;
}

// PCG, 24 bits a draw
struct Rng {
    uint32_t state;
    float unit() {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word ^= word >> 22u;
        return (float) (word >> 8) / 16777216.f;
    }
};

// Value noise in [0, 1], one value per cell of 1
float valueNoise(float x, float z, uint32_t seed) {
    int x0 = (int) std::floor(x), z0 = (int) std::floor(z);
    float fx = x - x0, fz = z - z0;
    fx = fx * fx * (3.f - 2.f * fx);
    fz = fz * fz * (3.f - 2.f * fz);
    auto at = [&](int i, int j) {
        return (float) (mix(mix(mix(seed, (uint32_t) i), (uint32_t) j), 0x51ED270Bu) >> 8) / 16777216.f;
    };
    float a = at(x0, z0), b = at(x0 + 1, z0), c = at(x0, z0 + 1), d = at(x0 + 1, z0 + 1);
    return (a * (1.f - fx) + b * fx) * (1.f - fz) + (c * (1.f - fx) + d * fx) * fz;
}

// A block of lattice nodes of the field, read between the nodes
struct FieldBlock {
    glm::ivec2 start, count;
    std::vector<float> cover, stand;

    FieldBlock(const ForestField& field, glm::vec2 min, glm::vec2 max) {
        start = glm::ivec2(glm::floor(min / ForestField::node));
        count = glm::ivec2(glm::floor(max / ForestField::node)) - start + 2;
        field.nodes(start, count, cover, stand);
    }

    float read(const std::vector<float>& values, float x, float z) const {
        float gx = x / ForestField::node - start.x, gz = z / ForestField::node - start.y;
        int i = std::clamp((int) std::floor(gx), 0, count.x - 2), j = std::clamp((int) std::floor(gz), 0, count.y - 2);
        float fx = gx - i, fz = gz - j;
        const float* row = &values[(size_t) j * count.x + i];
        return (row[0] * (1.f - fx) + row[1] * fx) * (1.f - fz) + (row[count.x] * (1.f - fx) + row[count.x + 1] * fx) * fz;
    }
};

// The highest surface of each column of samples of a chunk, in samples (fractional); a negative height without one
struct ChunkHeights {
    std::vector<float> heights;     // [x][z]

    ChunkHeights(const std::vector<float>& values) : heights((size_t) chunk::values * chunk::values, -1.f) {
        for(int x = 0; x < chunk::values; x++) {
            for(int z = 0; z < chunk::values; z++) {
                const float* column = &values[chunk::index(x, 0, z)];
                for(int y = chunk::values - 2; y >= 0; y--) {
                    if(column[y] > 0.f && column[y + 1] <= 0.f) {
                        heights[(size_t) x * chunk::values + z] = y + column[y] / (column[y] - column[y + 1]);
                        break;
                    }
                }
            }
        }
    }

    // False next to a column without a surface
    bool at(float x, float z, float& height) const {
        int x0 = (int) std::floor(x), z0 = (int) std::floor(z);
        if(x0 < 0 || z0 < 0 || x0 > chunk::values - 2 || z0 > chunk::values - 2) return false;
        float fx = x - x0, fz = z - z0;
        const float* h = &heights[(size_t) x0 * chunk::values + z0];
        float a = h[0], b = h[chunk::values], c = h[1], d = h[chunk::values + 1];
        if(a < 0.f || b < 0.f || c < 0.f || d < 0.f) return false;
        height = (a * (1.f - fx) + b * fx) * (1.f - fz) + (c * (1.f - fx) + d * fx) * fz;
        return true;
    }
};

ForestLayer::Where whereOf(const std::string& name) {
    if(name == "forest") return ForestLayer::Where::Forest;
    if(name == "edge") return ForestLayer::Where::Edge;
    if(name == "open") return ForestLayer::Where::Open;
    if(name == "all") return ForestLayer::Where::All;
    throw std::runtime_error("forest layer : where is forest, edge, open or all, not " + name);
}

ForestLayer parseLayer(const nlohmann::json& j, int stand, const VegetationLibrary& library) {

    ForestLayer layer;
    layer.stand = stand;
    layer.where = whereOf(j.value("where", std::string("forest")));
    layer.spacing = j.value("spacing", layer.spacing);
    layer.fill = j.value("fill", layer.fill);
    layer.clump = j.value("clump", layer.clump);
    layer.max_slope = j.value("max_slope", layer.max_slope);
    if(auto it = j.find("scale"); it != j.end()) {
        layer.scale_min = it->at(0).get<float>();
        layer.scale_max = it->at(1).get<float>();
    }
    if(layer.spacing < 0.25f) throw std::runtime_error("forest layer : spacing under 0.25 m");

    float total = 0.f;
    auto add = [&](const std::string& id, float weight) {
        int index = library.find(id);
        if(index < 0) {
            dy::log(dy::warning) << "Forest : no species " << id << " in the vegetation library\n";
            return;
        }
        if(weight <= 0.f) return;
        total += weight;
        layer.species.push_back((uint32_t) index);
        layer.weights.push_back(total);
    };
    const auto& species = j.at("species");
    if(species.is_object()) {
        for(auto& [id, weight] : species.items()) add(id, weight.get<float>());
    } else {
        for(auto& id : species) add(id.get<std::string>(), 1.f);
    }

    return layer;

}

}



ForestField::ForestField(std::shared_ptr<const TerrainGenerator> generator, ForestParams params) : params(params), generator(std::move(generator)) {

    auto simplex = FastNoise::New<FastNoise::Simplex>();

    auto patch_noise = FastNoise::New<FastNoise::FractalFBm>();
    patch_noise->SetSource(simplex);
    patch_noise->SetOctaveCount(4);
    patch_noise->SetGain(0.45f);
    patches = patch_noise;

    auto stand_noise = FastNoise::New<FastNoise::FractalFBm>();
    stand_noise->SetSource(simplex);
    stand_noise->SetOctaveCount(2);
    stands = stand_noise;

    // The noise has no known distribution : its quantiles are measured on a few hundred patches
    const int side = 160;
    std::vector<float> samples((size_t) side * side);
    patches->GenUniformGrid2D(samples.data(), 0, 0, side, side, 1.f / 12.f, (int) params.seed);
    std::sort(samples.begin(), samples.end());
    auto quantile = [&](float q) {
        return samples[(size_t) std::clamp(q * (float) samples.size(), 0.f, (float) samples.size() - 1.f)];
    };
    cover_low = quantile(1.f - params.coverage - params.edge * 0.5f);
    cover_high = quantile(1.f - params.coverage + params.edge * 0.5f);

    stand_sorted.resize((size_t) side * side);
    stands->GenUniformGrid2D(stand_sorted.data(), 0, 0, side, side, 1.f / 12.f, (int) params.seed + 1);
    std::sort(stand_sorted.begin(), stand_sorted.end());

}

void ForestField::nodes(glm::ivec2 start, glm::ivec2 count, std::vector<float>& cover, std::vector<float>& stand) const {

    const size_t total = (size_t) count.x * count.y;
    cover.resize(total);
    stand.resize(total);

    // A patch and the open land next to it make one period of the noise
    patches->GenUniformGrid2D(cover.data(), start.x, start.y, count.x, count.y, node / (2.f * params.patch_size), (int) params.seed);
    stands->GenUniformGrid2D(stand.data(), start.x, start.y, count.x, count.y, node / (2.f * params.stand_size), (int) params.seed + 1);

    const int margin = std::max((int) std::lround(params.slope_reach / node), 1);
    glm::ivec2 size = count + 2 * margin;
    std::vector<float> heights;
    generator->surfaceHeights(start - margin, size, node, heights);

    for(int j = 0; j < count.y; j++) {
        for(int i = 0; i < count.x; i++) {
            const float* h = &heights[(size_t) (j + margin) * size.x + i + margin];
            float dx = (h[margin] - h[-margin]) / (2.f * margin * node);
            float dz = (h[margin * size.x] - h[-margin * size.x]) / (2.f * margin * node);
            float slope = glm::degrees(std::atan(std::sqrt(dx * dx + dz * dz)));

            size_t index = (size_t) j * count.x + i;
            cover[index] = smoothstep(cover_low, cover_high, cover[index])
                * (1.f - smoothstep(params.max_slope - params.slope_fade, params.max_slope, slope));

            auto rank = std::lower_bound(stand_sorted.begin(), stand_sorted.end(), stand[index]) - stand_sorted.begin();
            stand[index] = (float) rank / (float) stand_sorted.size();
        }
    }

}

float ForestField::cover(float x, float z) const {
    FieldBlock block(*this, glm::vec2(x, z), glm::vec2(x, z));
    return block.read(block.cover, x, z);
}

float ForestField::stand(float x, float z) const {
    FieldBlock block(*this, glm::vec2(x, z), glm::vec2(x, z));
    return block.read(block.stand, x, z);
}

float ForestField::landSlope(float x, float z) const {
    const float reach = node * (float) std::max((int) std::lround(params.slope_reach / node), 1);
    float dx = (generator->surfaceHeight(x + reach, z) - generator->surfaceHeight(x - reach, z)) / (2.f * reach);
    float dz = (generator->surfaceHeight(x, z + reach) - generator->surfaceHeight(x, z - reach)) / (2.f * reach);
    return glm::degrees(std::atan(std::sqrt(dx * dx + dz * dz)));
}



ForestDefinition ForestDefinition::parse(const nlohmann::json& j, const VegetationLibrary& library) {

    ForestDefinition definition;
    auto& p = definition.params;
    p.seed = j.value("seed", p.seed);
    p.patch_size = j.value("patch_size", p.patch_size);
    p.coverage = j.value("coverage", p.coverage);
    p.edge = j.value("edge", p.edge);
    p.max_slope = j.value("max_slope", p.max_slope);
    p.slope_fade = j.value("slope_fade", p.slope_fade);
    p.slope_reach = j.value("slope_reach", p.slope_reach);
    p.stand_size = j.value("stand_size", p.stand_size);

    try {
        if(auto stands = j.find("stands"); stands != j.end()) {
            for(auto& stand : *stands) {
                int index = (int) definition.stands.size();
                definition.stands.push_back(stand.value("name", std::string("stand ") + std::to_string(index)));
                for(auto& layer : stand.at("layers")) definition.layers.push_back(parseLayer(layer, index, library));
            }
        }
        if(auto everywhere = j.find("everywhere"); everywhere != j.end()) {
            for(auto& layer : *everywhere) definition.layers.push_back(parseLayer(layer, -1, library));
        }
    } catch(const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("forest definition : ") + e.what());
    }

    return definition;

}

std::vector<uint32_t> ForestDefinition::species() const {
    std::vector<uint32_t> all;
    for(auto& layer : layers) all.insert(all.end(), layer.species.begin(), layer.species.end());
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    return all;
}

void dy::forestCandidates(const ForestField& field, const ForestLayer& layer, uint32_t index, int stands,
    glm::ivec2 min, glm::ivec2 max, std::vector<ForestCandidate>& out) {

    if(layer.species.empty() || layer.fill <= 0.f || max.x <= min.x || max.y <= min.y) return;

    FieldBlock block(field, glm::vec2(min) * layer.spacing, glm::vec2(max) * layer.spacing);
    const uint32_t seed = mix(field.params.seed * 0x9E3779B9u, index);

    for(int cx = min.x; cx < max.x; cx++) {
        for(int cz = min.y; cz < max.y; cz++) {

            // Every draw is made whether the plant is kept or not : a parameter only changes what it is about
            Rng rng { mix(mix(seed, (uint32_t) cx), (uint32_t) cz) };
            ForestCandidate c;
            c.x = ((float) cx + 0.1f + 0.8f * rng.unit()) * layer.spacing;
            c.z = ((float) cz + 0.1f + 0.8f * rng.unit()) * layer.spacing;
            float roll = rng.unit();
            float pick = rng.unit() * layer.weights.back();
            c.yaw = rng.unit() * 6.2831853f;
            float size = rng.unit();
            float stand_jitter = rng.unit() - 0.5f;

            c.cover = block.read(block.cover, c.x, c.z);
            float density = layer.fill;
            switch(layer.where) {
                case ForestLayer::Where::Forest: density *= c.cover * c.cover; break;
                case ForestLayer::Where::Edge: density *= 4.f * c.cover * (1.f - c.cover); break;
                case ForestLayer::Where::Open: density *= (1.f - c.cover) * (1.f - c.cover); break;
                case ForestLayer::Where::All: break;
            }
            if(roll >= density) continue;

            if(layer.clump > 0.f) {
                float clump = valueNoise(c.x / layer.clump, c.z / layer.clump, seed);
                if(roll >= density * smoothstep(0.4f, 0.7f, clump)) continue;
            }

            if(layer.stand >= 0 && stands > 1) {
                // Stands mix where they meet
                float stand = block.read(block.stand, c.x, c.z) + stand_jitter * 0.2f;
                if(std::clamp((int) std::floor(stand * (float) stands), 0, stands - 1) != layer.stand) continue;
            }

            size_t s = std::lower_bound(layer.weights.begin(), layer.weights.end(), pick) - layer.weights.begin();
            c.species = layer.species[std::min(s, layer.species.size() - 1)];
            c.scale = layer.scale_min + size * (layer.scale_max - layer.scale_min);
            // Trees are smaller where the forest thins out
            if(layer.where == ForestLayer::Where::Forest) c.scale *= 0.75f + 0.25f * c.cover;
            out.push_back(c);
        }
    }

}



ForestSys::ForestSys(entt::registry& reg, const nlohmann::json& params) : System(reg), params(params) {

}

void ForestSys::preinit() {
    reg.storage<Plant>();
    reg.storage<ChunkForest>();
    reg.on_destroy<ChunkForest>().connect<&ForestSys::onChunkForestDestroyed>(*this);
}

void ForestSys::init() {

    auto& library = vegetationLibrary(reg);
    auto* generator = reg.ctx().find<std::shared_ptr<TerrainGenerator>>();
    if(!generator) {
        dy::log(dy::error) << "Forest : no terrain generator, the scene needs the ChunkGenerator\n";
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::object();
        if(auto it = params.find("definition"); it != params.end() && it->is_object()) {
            j = *it;
        } else {
            std::string path = "./resources/" + params.value("definition", std::string("forest/tautavel.forest.json"));
            std::ifstream file(path);
            if(!file) throw std::runtime_error("cannot open " + path);
            j = nlohmann::json::parse(file);
        }
        for(const char* key : { "seed", "patch_size", "coverage", "max_slope" }) {
            if(auto it = params.find(key); it != params.end()) j[key] = *it;
        }
        definition = ForestDefinition::parse(j, library);
    } catch(const std::exception& e) {
        dy::log(dy::error) << "Forest : " << e.what() << "\n";
        definition = ForestDefinition();
        return;
    }

    field = std::make_shared<ForestField>(*generator, definition.params);
    reg.ctx().insert_or_assign<std::shared_ptr<ForestField>>(std::shared_ptr<ForestField>(field));
    reg.ctx().insert_or_assign<VegetationPreload>(VegetationPreload { definition.species() });

    dy::log(dy::info) << "Forest : " << definition.stands.size() << " stands, " << definition.layers.size() << " layers, "
        << definition.species().size() << " species, patches of " << definition.params.patch_size << " m on slopes under "
        << definition.params.max_slope << " degrees\n";

}

void ForestSys::scatter(entt::entity entity) {

    const ChunkC& chunk_c = reg.get<ChunkC>(entity);
    const std::vector<float>& values = reg.get<ChunkVolume>(entity).values;
    auto& plants = reg.emplace<ChunkForest>(entity).plants;
    if(values.size() != chunk::num_values) return;

    const auto& library = reg.ctx().get<VegetationLibrary>();
    const ChunkHeights heights(values);
    const glm::vec3 origin = chunk_c.origin();

    std::vector<ForestCandidate> candidates;
    for(size_t l = 0; l < definition.layers.size(); l++) {
        const ForestLayer& layer = definition.layers[l];
        const float max_gradient = std::tan(glm::radians(layer.max_slope));

        // Cells overlap the neighbours : a plant is on the chunk it stands in
        glm::ivec2 min = glm::ivec2(glm::floor(glm::vec2(origin.x, origin.z) / layer.spacing));
        glm::ivec2 max = glm::ivec2(glm::floor((glm::vec2(origin.x, origin.z) + (float) chunk::length) / layer.spacing)) + 1;
        candidates.clear();
        forestCandidates(*field, layer, (uint32_t) l, (int) definition.stands.size(), min, max, candidates);

        for(const ForestCandidate& c : candidates) {
            float x = (c.x - origin.x) / chunk::cube_size, z = (c.z - origin.z) / chunk::cube_size;
            if(x < 0.f || z < 0.f || x >= (float) chunk::cubes || z >= (float) chunk::cubes) continue;
            x += chunk::border;
            z += chunk::border;

            // On the cubes the chunk owns : the chunk above or below has the others
            float y, xa, xb, za, zb;
            if(!heights.at(x, z, y) || y < (float) chunk::border || y >= (float) (chunk::border + chunk::cubes)) continue;
            if(!heights.at(x - 1.f, z, xa) || !heights.at(x + 1.f, z, xb) || !heights.at(x, z - 1.f, za) || !heights.at(x, z + 1.f, zb)) continue;
            float gx = (xb - xa) * 0.5f, gz = (zb - za) * 0.5f;
            float gradient = std::sqrt(gx * gx + gz * gz);
            if(gradient > max_gradient) continue;

            const auto& species = library.species[c.species];
            glm::vec3 position(c.x, origin.y + (y - chunk::border) * chunk::cube_size, c.z);
            // Sunk a little, so the foot of the trunk is not in the air on the downhill side
            position.y -= std::min(0.02f * species.height, 0.15f) * c.scale * (1.f + 4.f * gradient);

            auto plant = reg.create();
            reg.emplace<Transform>(plant, position, glm::angleAxis(c.yaw, glm::vec3(0.f, 1.f, 0.f)), glm::vec3(c.scale));
            reg.emplace<Plant>(plant, c.species);
            plants.push_back(plant);
        }
    }

}

void ForestSys::tick(double dt) {

    if(!field || definition.layers.empty()) return;

    // Fixed-clock runs plant every chunk the frame it is generated, so a frame never depends on timing
    auto* args = reg.ctx().find<RunArgs>();
    const size_t budget = args && args->deterministic() ? ~(size_t) 0 : 2;

    std::vector<entt::entity> pending;
    for(auto entity : reg.view<ChunkC, ChunkVolume>(entt::exclude<ChunkForest, ChunkDestroying>)) {
        pending.push_back(entity);
        if(pending.size() >= budget) break;
    }
    for(auto entity : pending) {
        scatter(entity);
    }

}

void ForestSys::onChunkForestDestroyed(entt::registry& registry, entt::entity chunk) {
    for(auto plant : registry.get<ChunkForest>(chunk).plants) {
        if(registry.valid(plant)) registry.destroy(plant);
    }
}

void ForestSys::finish() {
    reg.on_destroy<ChunkForest>().disconnect<&ForestSys::onChunkForestDestroyed>(*this);
}

void dy::registerForest() {
    registerSystem("Forest", { [](entt::registry& reg, const nlohmann::json& params) { return std::make_unique<ForestSys>(reg, params); },
        SystemScope::Host });
}
