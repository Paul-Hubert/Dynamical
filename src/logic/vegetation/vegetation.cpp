#include "vegetation.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "util/log.h"

using namespace dy;

namespace {

nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    if(!file) throw std::runtime_error("cannot open " + path.string());
    return nlohmann::json::parse(file);
}

std::string optionalString(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

glm::vec3 readVec3(const nlohmann::json& j) {
    return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>());
}

}

std::filesystem::path dy::vegetationDirectory() {
    std::filesystem::path local = "./resources/vegetation";
    std::error_code error;
    if(std::filesystem::exists(local / "library.json", error)) return local;
#ifdef DY_SOURCE_RESOURCES
    return std::filesystem::path(DY_SOURCE_RESOURCES) / "vegetation";
#else
    return local;
#endif
}

VegetationLibrary& dy::vegetationLibrary(entt::registry& reg) {
    if(auto* library = reg.ctx().find<VegetationLibrary>()) return *library;
    auto& library = reg.ctx().emplace<VegetationLibrary>();
    auto directory = vegetationDirectory();
    if(library.load(directory)) {
        dy::log(dy::info) << "Vegetation library : " << library.species.size() << " species in " << directory.string() << "\n";
    } else {
        dy::log(dy::warning) << "No vegetation library in " << directory.string() << " (tools/speedtree_import.py builds it)\n";
    }
    return library;
}

bool VegetationLibrary::load(const std::filesystem::path& dir) {

    directory = dir;
    species.clear();

    try {
        auto j = readJson(dir / "library.json");
        for(auto& s : j.at("species")) {
            Species entry;
            entry.id = s.at("id").get<std::string>();
            entry.name = s.value("name", entry.id);
            entry.category = s.value("category", std::string("tree"));
            entry.height = s.value("height", 0.f);
            entry.radius = s.value("radius", 0.f);
            entry.triangles = s.value("triangles", 0u);
            entry.lods = s.value("lods", 1u);
            species.push_back(std::move(entry));
        }
    } catch(const std::exception&) {
        species.clear();
        return false;
    }

    return true;

}

int VegetationLibrary::find(const std::string& id) const {
    for(size_t i = 0; i < species.size(); i++) {
        if(species[i].id == id) return (int) i;
    }
    return -1;
}

VegetationSpeciesDef VegetationLibrary::loadSpecies(uint32_t index) const {

    const Species& entry = species.at(index);
    auto j = readJson(directory / "species" / (entry.id + ".json"));

    VegetationSpeciesDef def;
    def.id = entry.id;
    def.min = readVec3(j.at("bounds").at("min"));
    def.max = readVec3(j.at("bounds").at("max"));
    def.alpha_cutoff = j.value("alpha_cutoff", def.alpha_cutoff);
    def.vertex_count = j.at("vertex_count").get<uint32_t>();
    def.index_count = j.at("index_count").get<uint32_t>();

    for(auto& m : j.at("materials")) {
        VegetationMaterial material;
        material.name = m.value("name", std::string());
        material.two_sided = m.value("two_sided", false);
        material.billboard = m.value("billboard", false);
        if(auto it = m.find("tint"); it != m.end() && it->size() == 4) {
            material.tint = glm::vec4(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>(), it->at(3).get<float>());
        }
        material.color = optionalString(m, "color");
        material.normal = optionalString(m, "normal");
        material.extra = optionalString(m, "extra");
        material.subsurface = optionalString(m, "subsurface");
        def.materials.push_back(std::move(material));
    }

    for(auto& l : j.at("lods")) {
        VegetationLod lod;
        lod.first_vertex = l.at("first_vertex").get<uint32_t>();
        lod.vertex_count = l.at("vertex_count").get<uint32_t>();
        lod.screen_height = l.value("screen_height", 0.f);
        lod.billboard = l.value("billboard", false);
        for(auto& d : l.at("draws")) {
            VegetationLod::Draw draw;
            draw.material = d.at("material").get<uint32_t>();
            draw.first_index = d.at("first_index").get<uint32_t>();
            draw.index_count = d.at("index_count").get<uint32_t>();
            if(draw.material >= def.materials.size() || (uint64_t) draw.first_index + draw.index_count > def.index_count) {
                throw std::runtime_error("draw call out of range in " + entry.id);
            }
            lod.draws.push_back(draw);
        }
        if((uint64_t) lod.first_vertex + lod.vertex_count > def.vertex_count) {
            throw std::runtime_error("level of detail out of range in " + entry.id);
        }
        def.lods.push_back(std::move(lod));
    }

    if(def.lods.empty()) throw std::runtime_error("no level of detail in " + entry.id);

    return def;

}

std::vector<VegetationVertex> VegetationLibrary::loadGeometry(const VegetationSpeciesDef& def, std::vector<uint32_t>& indices) const {

    static_assert(sizeof(VegetationVertex) == 28, "species/<id>.bin stores 28 byte vertices");

    auto path = directory / "species" / (def.id + ".bin");
    std::ifstream file(path, std::ios::binary);
    if(!file) throw std::runtime_error("cannot open " + path.string());

    std::vector<VegetationVertex> vertices(def.vertex_count);
    indices.resize(def.index_count);
    file.read(reinterpret_cast<char*>(vertices.data()), (std::streamsize) (vertices.size() * sizeof(VegetationVertex)));
    file.read(reinterpret_cast<char*>(indices.data()), (std::streamsize) (indices.size() * sizeof(uint32_t)));
    if(!file) throw std::runtime_error(path.string() + " is shorter than its description");

    // Indices are relative to their level of detail
    for(auto& lod : def.lods) {
        for(auto& draw : lod.draws) {
            for(uint32_t i = 0; i < draw.index_count; i++) {
                if(indices[draw.first_index + i] >= lod.vertex_count) throw std::runtime_error("index out of range in " + path.string());
            }
        }
    }

    return vertices;

}
