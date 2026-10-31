#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "entt/entt.hpp"

namespace dy {

struct MeshData;

// Context variable : loads glTF files once and hands out the entity holding their ModelC
class ModelManager {
public:
    ModelManager(entt::registry& reg);

    // Returns entt::null if the file could not be loaded
    entt::entity load(const std::string& path);
    entt::entity get(const std::string& path) const;

    // A mesh built in code (WorldView's capsules), cached under key like a file. One white material
    entt::entity add(const std::string& key, const MeshData& mesh);

private:
    entt::registry& reg;
    std::unordered_map<std::string, entt::entity> models;
};

}

#endif
