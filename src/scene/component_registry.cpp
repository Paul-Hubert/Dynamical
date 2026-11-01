#include "component_registry.h"

#include <map>

using namespace dy;

namespace {

std::map<std::string, ComponentInfo>& components() {
    static std::map<std::string, ComponentInfo> map;
    return map;
}

template<int N>
void readFloats(const nlohmann::json& j, const char* key, float* out) {
    if(!j.is_array() || j.size() != N) {
        throw ComponentError(std::string("/") + key, "expected an array of " + std::to_string(N) + " numbers");
    }
    for(int i = 0; i < N; i++) {
        if(!j[i].is_number()) throw ComponentError(std::string("/") + key + "/" + std::to_string(i), "expected a number");
        out[i] = j[i].get<float>();
    }
}

}

void dy::registerComponent(const std::string& name, ComponentInfo info) {
    components()[name] = std::move(info);
}

const ComponentInfo* dy::findComponent(const std::string& name) {
    auto it = components().find(name);
    return it != components().end() ? &it->second : nullptr;
}

JsonFields::JsonFields(const nlohmann::json& object) : object(object) {
    if(!object.is_object()) throw ComponentError("", "expected an object");
}

void JsonFields::read(const char* key, bool& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    if(!it->is_boolean()) throw ComponentError(std::string("/") + key, "expected a boolean");
    out = it->get<bool>();
}

void JsonFields::read(const char* key, int& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    if(!it->is_number_integer()) throw ComponentError(std::string("/") + key, "expected an integer");
    out = it->get<int>();
}

void JsonFields::read(const char* key, float& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    if(!it->is_number()) throw ComponentError(std::string("/") + key, "expected a number");
    out = it->get<float>();
}

void JsonFields::read(const char* key, std::string& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    if(!it->is_string()) throw ComponentError(std::string("/") + key, "expected a string");
    out = it->get<std::string>();
}

void JsonFields::read(const char* key, glm::vec2& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    readFloats<2>(*it, key, &out.x);
}

void JsonFields::read(const char* key, glm::vec3& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    readFloats<3>(*it, key, &out.x);
}

void JsonFields::read(const char* key, glm::vec4& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    readFloats<4>(*it, key, &out.x);
}

void JsonFields::read(const char* key, glm::quat& out) {
    auto it = object.find(key);
    if(it == object.end()) return;
    used.insert(key);
    float v[4];
    readFloats<4>(*it, key, v);
    out = glm::quat(v[3], v[0], v[1], v[2]);
}

void JsonFields::finish() const {
    for(auto& [key, value] : object.items()) {
        if(!used.count(key)) throw ComponentError("/" + key, "unknown field '" + key + "'");
    }
}

nlohmann::json dy::toJson(glm::vec3 v) { return { v.x, v.y, v.z }; }
nlohmann::json dy::toJson(glm::vec4 v) { return { v.x, v.y, v.z, v.w }; }
nlohmann::json dy::toJson(glm::quat q) { return { q.x, q.y, q.z, q.w }; }
