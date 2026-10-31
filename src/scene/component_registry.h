#ifndef DY_COMPONENT_REGISTRY_H
#define DY_COMPONENT_REGISTRY_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <set>
#include <stdexcept>
#include <string>

namespace dy {

// Components with a JSON form, keyed by registered name in scenes and prefabs
struct ComponentInfo {
    std::function<void(entt::registry&, entt::entity, const nlohmann::json&)> emplace;   // throws ComponentError
    std::function<nlohmann::json(const entt::registry&, entt::entity)> save;
};

void registerComponent(const std::string& name, ComponentInfo info);
const ComponentInfo* findComponent(const std::string& name);

// A bad field: path is relative to the component object (/half_extent/2)
struct ComponentError : std::runtime_error {
    ComponentError(std::string path, const std::string& message) : std::runtime_error(message), path(std::move(path)) {}
    std::string path;
};

// Reads optional fields of a component object, rejects unknown keys and wrong types
class JsonFields {
public:
    explicit JsonFields(const nlohmann::json& object);

    void read(const char* key, bool& out);
    void read(const char* key, int& out);
    void read(const char* key, float& out);
    void read(const char* key, std::string& out);
    void read(const char* key, glm::vec3& out);
    void read(const char* key, glm::vec4& out);
    void read(const char* key, glm::quat& out);    // [x, y, z, w]

    // One of the given names, stored as its index
    template<typename E, size_t N>
    void readEnum(const char* key, E& out, const char* const (&names)[N]) {
        std::string value;
        read(key, value);
        if(value.empty()) return;
        for(size_t i = 0; i < N; i++) {
            if(value == names[i]) { out = (E) i; return; }
        }
        throw ComponentError(std::string("/") + key, "unknown value '" + value + "'");
    }

    // Throws on keys nothing read
    void finish() const;

private:
    const nlohmann::json& object;
    std::set<std::string> used;
};

nlohmann::json toJson(glm::vec3 v);
nlohmann::json toJson(glm::vec4 v);
nlohmann::json toJson(glm::quat q);

}

#endif
