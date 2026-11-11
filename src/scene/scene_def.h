#ifndef DY_SCENE_DEF_H
#define DY_SCENE_DEF_H

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "logic/systems/system.h"

namespace dy {

// A scene that does not load: the file (as written in the scene tree) and the JSON pointer of the culprit
struct SceneError : std::runtime_error {
    SceneError(std::string file, std::string path, const std::string& message)
        : std::runtime_error(file + ": " + (path.empty() ? "" : path + ": ") + message), file(std::move(file)), path(std::move(path)) {}
    std::string file;
    std::string path;
};

struct SystemEntry {
    std::string name;                       // registry name
    Phase phase = Phase::Every;
    nlohmann::json params = nlohmann::json::object();
    std::string file, path;                 // where it was declared
};

struct EntityDef {
    nlohmann::json components;              // name -> fields, prefab merged in
    std::string file, path;
};

struct WorldDef {
    std::string template_name;
    int count = 1;
    nlohmann::json physics = nlohmann::json::object();
    nlohmann::json clock = nlohmann::json::object();
    std::vector<SystemEntry> fixed;         // template list after the world's replace / remove
    std::vector<EntityDef> entities;
    std::string file, path;                 // the template, for errors
};

struct RendererDef {
    std::vector<std::string> passes;        // draw order
    bool vr = true;
};

struct SceneDef {
    std::string name;
    std::string file;
    std::optional<RendererDef> renderer;
    std::vector<SystemEntry> host_pre, host_post;
    std::vector<WorldDef> worlds;
    nlohmann::json resolved;                // merged and substituted: the provenance dump, loadable as a scene
};

struct SceneLoadOptions {
    std::filesystem::path resources = "resources";   // scenes/, prefabs/, and the root of "<file>#<key>" references
    bool allow_renderer = true;                       // false in dynamical_train
};

// Render passes a scene can list, in no particular order
inline constexpr const char* scene_passes[] = { "terrain", "grass", "vegetation", "animals", "model", "ui" };

// name: resources/scenes/<name>.scene.json, or a path to a .json file.
// vars: var.<name>=<value> from the command line, in order (last wins). Throws SceneError.
SceneDef loadScene(const std::string& name, const std::vector<std::pair<std::string, std::string>>& vars, const SceneLoadOptions& options);

// resources/prefabs/<name>.json, "extends" resolved: the component map. Throws SceneError
nlohmann::json loadPrefab(const std::filesystem::path& resources, const std::string& name);

void writeResolved(const SceneDef& scene, const std::filesystem::path& file);

}

#endif
