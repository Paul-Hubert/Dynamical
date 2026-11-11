#ifndef DY_VEGETATION_H
#define DY_VEGETATION_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dy {

// A plant of the vegetation library, next to a Transform (uniform scale). Drawn by VegetationPass
struct Plant {
    uint32_t species = 0;   // index in VegetationLibrary::species
    int32_t lod = -1;       // >= 0 : always drawn with this level of detail (clamped to the species' last)
                            // `volume` : always from the baked volume of the species (ray traced lighting), its last level without it
    static constexpr int32_t volume = -2;
};

// One level of detail of a species : a range of the species' vertices and its draw calls
struct VegetationLod {
    struct Draw {
        uint32_t material = 0;
        uint32_t first_index = 0;
        uint32_t index_count = 0;
    };
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    float screen_height = 0.f;      // drawn while the plant is taller than this fraction of the screen
    bool billboard = false;         // the cards SpeedTree bakes for the far distance
    std::vector<Draw> draws;
};

struct VegetationMaterial {
    std::string name;
    bool two_sided = false;
    bool billboard = false;
    glm::vec4 tint = glm::vec4(1.f);
    // Paths relative to the library folder, empty when the material has no such map
    std::string color;              // rgb, a : opacity
    std::string normal;             // tangent space
    std::string extra;              // r : smoothness, g : metallic, b : ambient occlusion
    std::string subsurface;         // rgb : light going through the leaf
};

// species/<id>.json : what the renderer needs to draw a species. The geometry is in species/<id>.bin
struct VegetationSpeciesDef {
    std::string id;
    glm::vec3 min = glm::vec3(0.f);
    glm::vec3 max = glm::vec3(0.f);
    float alpha_cutoff = 0.33f;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    std::vector<VegetationMaterial> materials;
    std::vector<VegetationLod> lods;
};

// 28 bytes, the layout of species/<id>.bin
struct VegetationVertex {
    glm::vec3 position;
    int8_t normal[4];               // xyz, w : sign of the bitangent (cross(normal, tangent) * w)
    int8_t tangent[4];              // xyz, w : ambient occlusion
    glm::vec2 uv;
};

// Context variable : the index of resources/vegetation (tools/speedtree_import.py, docs/vegetation.md)
class VegetationLibrary {
public:
    struct Species {
        std::string id;
        std::string name;
        std::string category;       // ground (under 1 m), shrub (under 4 m), tree
        float height = 0.f;
        float radius = 0.f;
        uint32_t triangles = 0;     // of the most detailed level
        uint32_t lods = 1;          // levels of detail, the last one usually the billboard
    };

    // False when the folder has no library.json
    bool load(const std::filesystem::path& directory);

    int find(const std::string& id) const;

    // Throws std::runtime_error when the files are missing or malformed
    VegetationSpeciesDef loadSpecies(uint32_t species) const;
    std::vector<VegetationVertex> loadGeometry(const VegetationSpeciesDef& def, std::vector<uint32_t>& indices) const;

    std::filesystem::path directory;
    std::vector<Species> species;
};

// ./resources/vegetation next to the working directory, else the source tree's (the library is generated,
// too large to copy to every build folder)
std::filesystem::path vegetationDirectory();

// The context's library, loaded from vegetationDirectory() on first use (empty when there is none)
VegetationLibrary& vegetationLibrary(entt::registry& reg);

// Context variable : species the vegetation pass loads before any plant uses them
struct VegetationPreload {
    std::vector<uint32_t> species;
};

// Context variable written by VegetationPass, read by the debug menu
struct VegetationStats {
    uint32_t plants = 0;            // Plant entities
    uint32_t drawn = 0;             // instances drawn on the desktop target
    uint32_t triangles = 0;
    uint32_t species_loaded = 0;
    uint32_t species_loading = 0;   // asked for and not drawn yet : plants are missing
};

}

#endif
