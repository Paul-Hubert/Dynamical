#include "scene_def.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include <entt/entt.hpp>

#include "component_registry.h"
#include "system_registry.h"

using namespace dy;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

const char* const command_line = "<command line>";

std::string pointer(const std::string& parent, const std::string& key) {
    std::string escaped;
    for(char c : key) {
        if(c == '~') escaped += "~0";
        else if(c == '/') escaped += "~1";
        else escaped += c;
    }
    return parent + "/" + escaped;
}

std::string pointer(const std::string& parent, size_t index) {
    return parent + "/" + std::to_string(index);
}

json readJson(const fs::path& file, const std::string& display) {
    std::ifstream in(file, std::ios::binary);
    if(!in) throw SceneError(display, "", "cannot open " + file.generic_string());
    try {
        return json::parse(in, nullptr, true, true);
    } catch(const json::parse_error& e) {
        throw SceneError(display, "", std::string("invalid JSON: ") + e.what());
    }
}

void checkKeys(const json& object, std::initializer_list<const char*> allowed, const std::string& display, const std::string& path) {
    if(!object.is_object()) throw SceneError(display, path, "expected an object");
    for(auto& [key, value] : object.items()) {
        if(std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return key == a; })) {
            throw SceneError(display, pointer(path, key), "unknown key '" + key + "'");
        }
    }
}

void deepMerge(json& target, const json& source) {
    if(target.is_object() && source.is_object()) {
        for(auto& [key, value] : source.items()) deepMerge(target[key], value);
    } else {
        target = source;
    }
}

bool hasType(const json& value, const std::string& type) {
    if(type == "int") return value.is_number_integer();
    if(type == "float") return value.is_number();
    if(type == "bool") return value.is_boolean();
    if(type == "string") return value.is_string();
    return false;
}

std::string text(const json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}

// Every component of an entity or prefab must build, so errors surface at load time
void validateComponents(const json& components, const std::string& display, const std::string& path) {
    entt::registry scratch;
    for(auto& [name, fields] : components.items()) {
        const ComponentInfo* info = findComponent(name);
        if(!info) throw SceneError(display, pointer(path, name), "unknown component " + name);
        try {
            info->emplace(scratch, scratch.create(), fields);
        } catch(const ComponentError& e) {
            throw SceneError(display, pointer(path, name) + e.path, std::string(name) + ": " + e.what());
        }
    }
}

json loadPrefabChain(const fs::path& resources, const std::string& name, std::vector<std::string>& chain) {

    std::string display = "prefabs/" + name + ".json";
    fs::path file = resources / "prefabs" / (name + ".json");
    if(std::find(chain.begin(), chain.end(), name) != chain.end()) throw SceneError(display, "/extends", "cyclic prefab " + name);
    if(!fs::exists(file)) throw SceneError(display, "", "prefab not found");

    json content = readJson(file, display);
    if(!content.is_object()) throw SceneError(display, "", "a prefab is a JSON object");

    chain.push_back(name);
    json result = json::object();
    if(auto it = content.find("extends"); it != content.end()) {
        if(!it->is_string()) throw SceneError(display, "/extends", "expected a prefab name");
        result = loadPrefabChain(resources, it->get<std::string>(), chain);
    }
    chain.pop_back();

    json own = json::object();
    for(auto& [key, value] : content.items()) {
        if(key == "extends") continue;
        if(!value.is_object()) throw SceneError(display, pointer("", key), "expected the fields of component " + key);
        own[key] = value;
    }
    validateComponents(own, display, "");
    deepMerge(result, own);
    return result;

}

struct SourceFile {
    std::string display;
    json content;
};

struct Variable {
    std::string type;
    json value;
};

struct Located {
    json value;
    std::string file, path;
};

class Loader {
public:
    explicit Loader(const SceneLoadOptions& options) : options(options), scenes(options.resources / "scenes") {}

    SceneDef load(const std::string& name, const std::vector<std::pair<std::string, std::string>>& vars) {

        fs::path file;
        std::string display;
        if(name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) {
            file = name;
            display = fs::path(name).generic_string();
        } else {
            file = scenes / (name + ".scene.json");
            display = name + ".scene.json";
        }
        if(!fs::exists(file)) throw SceneError(display, "", "scene not found (" + file.generic_string() + ")");

        std::vector<std::string> stack;
        collect(file, display, stack);

        for(auto& f : files) checkTopLevel(f);
        for(auto& f : files) readVariables(f);
        applyCommandLine(vars);
        for(auto& f : files) {
            substitute(f.content, f.display, "");
            validate(f);
        }

        return merge(display);

    }

private:

    void collect(const fs::path& file, const std::string& display, std::vector<std::string>& stack) {

        json content = readJson(file, display);
        if(!content.is_object()) throw SceneError(display, "", "a scene file is a JSON object");

        stack.push_back(display);
        if(auto it = content.find("include"); it != content.end()) {
            if(!it->is_array()) throw SceneError(display, "/include", "expected an array of file names");
            for(size_t i = 0; i < it->size(); i++) {
                const json& entry = (*it)[i];
                std::string path = pointer("/include", i);
                if(!entry.is_string()) throw SceneError(display, path, "expected a file name");
                std::string included = entry.get<std::string>();
                if(std::find(stack.begin(), stack.end(), included) != stack.end()) {
                    throw SceneError(display, path, "cyclic include of " + included);
                }
                includes.insert(included);
                if(loaded.count(included)) continue;
                fs::path included_file = scenes / included;
                if(!fs::exists(included_file)) throw SceneError(display, path, "include not found: " + included);
                collect(included_file, included, stack);
            }
        }
        stack.pop_back();

        loaded.insert(display);
        files.push_back({ display, std::move(content) });

    }

    void checkTopLevel(const SourceFile& f) {
        checkKeys(f.content, { "format_version", "name", "include", "variables", "renderer", "host", "templates", "worlds", "resolved_from" }, f.display, "");
        // Before any system check: a rendered scene in dynamical_train fails for that reason, not for its view systems
        if(!options.allow_renderer && f.content.contains("renderer")) {
            throw SceneError(f.display, "/renderer", "dynamical_train does not render: a renderer block is not allowed");
        }
        if(auto it = f.content.find("resolved_from"); it != f.content.end()) {
            checkKeys(*it, { "scene", "includes" }, f.display, "/resolved_from");
            for(auto& include : it->value("includes", json::array())) {
                if(include.is_string()) includes.insert(include.get<std::string>());
            }
            resolved_from = *it;
        }
    }

    void readVariables(const SourceFile& f) {

        auto it = f.content.find("variables");
        if(it == f.content.end()) return;
        if(!it->is_object()) throw SceneError(f.display, "/variables", "expected an object");

        for(auto& [name, spec] : it->items()) {
            std::string path = pointer("/variables", name);
            checkKeys(spec, { "type", "default", "description" }, f.display, path);
            auto type = spec.find("type");
            if(type == spec.end() || !type->is_string()) throw SceneError(f.display, path + "/type", "expected int, float, bool or string");
            std::string t = type->get<std::string>();
            if(t != "int" && t != "float" && t != "bool" && t != "string") {
                throw SceneError(f.display, path + "/type", "unknown type '" + t + "' (int, float, bool or string)");
            }
            auto value = spec.find("default");
            if(value == spec.end()) throw SceneError(f.display, path, "a variable needs a default");
            if(!hasType(*value, t)) throw SceneError(f.display, path + "/default", "default " + value->dump() + " is not a " + t);
            if(auto known = variables.find(name); known != variables.end() && known->second.type != t) {
                throw SceneError(f.display, path + "/type", "variable " + name + " is already a " + known->second.type);
            }
            variables[name] = { t, t == "float" ? json(value->get<double>()) : *value };
        }

    }

    void applyCommandLine(const std::vector<std::pair<std::string, std::string>>& vars) {

        for(auto& [name, text] : vars) {
            std::string path = "var." + name;
            auto it = variables.find(name);
            if(it == variables.end()) throw SceneError(command_line, path, "unknown variable " + name);
            Variable& v = it->second;
            const char* first = text.data();
            const char* last = text.data() + text.size();
            bool ok = true;
            if(v.type == "int") {
                int64_t value;
                auto [ptr, ec] = std::from_chars(first, last, value);
                ok = ec == std::errc() && ptr == last;
                if(ok) v.value = value;
            } else if(v.type == "float") {
                double value;
                auto [ptr, ec] = std::from_chars(first, last, value);
                ok = ec == std::errc() && ptr == last;
                if(ok) v.value = value;
            } else if(v.type == "bool") {
                ok = text == "true" || text == "false" || text == "1" || text == "0";
                if(ok) v.value = text == "true" || text == "1";
            } else {
                v.value = text;
            }
            if(!ok) throw SceneError(command_line, path, "'" + text + "' is not a " + v.type);
        }

    }

    const Variable& variable(const std::string& name, const std::string& display, const std::string& path) {
        auto it = variables.find(name);
        if(it == variables.end()) throw SceneError(display, path, "unknown variable ${" + name + "}");
        return it->second;
    }

    void substitute(json& j, const std::string& display, const std::string& path) {

        if(j.is_object()) {
            for(auto& [key, value] : j.items()) {
                if(path.empty() && (key == "variables" || key == "include" || key == "resolved_from")) continue;
                substitute(value, display, pointer(path, key));
            }
        } else if(j.is_array()) {
            for(size_t i = 0; i < j.size(); i++) substitute(j[i], display, pointer(path, i));
        } else if(j.is_string()) {
            static const std::regex whole(R"(^\$\{([A-Za-z_][A-Za-z0-9_]*)\}$)");
            static const std::regex reference(R"(^(.+\.json)#(.+)$)");
            std::string s = j.get<std::string>();
            std::smatch m;
            if(std::regex_match(s, m, whole)) {
                j = variable(m[1].str(), display, path).value;
            } else if(s.find("${") != std::string::npos) {
                std::string out;
                size_t at = 0;
                while(true) {
                    size_t open = s.find("${", at);
                    if(open == std::string::npos) break;
                    size_t close = s.find('}', open);
                    if(close == std::string::npos) throw SceneError(display, path, "unterminated ${ in '" + s + "'");
                    out += s.substr(at, open - at) + text(variable(s.substr(open + 2, close - open - 2), display, path).value);
                    at = close + 1;
                }
                j = out + s.substr(at);
            }
            if(j.is_string() && std::regex_match(s = j.get<std::string>(), m, reference)) {
                j = fileValue(m[1].str(), m[2].str(), display, path);
            }
        }

    }

    // "<file>#<key>": a value read from a JSON file under resources/ (key: top-level name or JSON pointer)
    json fileValue(const std::string& file, const std::string& key, const std::string& display, const std::string& path) {
        auto it = referenced.find(file);
        if(it == referenced.end()) {
            fs::path full = options.resources / file;
            if(!fs::exists(full)) throw SceneError(display, path, "referenced file not found: " + file);
            it = referenced.emplace(file, readJson(full, file)).first;
        }
        const json& content = it->second;
        try {
            if(key[0] == '/') return content.at(json::json_pointer(key));
            return content.at(key);
        } catch(const json::exception&) {
            throw SceneError(display, path, "no key " + key + " in " + file);
        }
    }

    SystemEntry system(const json& value, const std::string& display, const std::string& path, SystemScope where) {

        SystemEntry entry;
        entry.file = display;
        entry.path = path;

        std::string spec;
        if(value.is_string()) {
            spec = value.get<std::string>();
        } else if(value.is_object()) {
            checkKeys(value, { "name", "params" }, display, path);
            auto name = value.find("name");
            if(name == value.end() || !name->is_string()) throw SceneError(display, path + "/name", "expected a system name");
            spec = name->get<std::string>();
            if(auto params = value.find("params"); params != value.end()) {
                if(!params->is_object()) throw SceneError(display, path + "/params", "expected an object");
                entry.params = *params;
            }
        } else {
            throw SceneError(display, path, "a system is a name or { \"name\", \"params\" }");
        }

        auto at = spec.find('@');
        entry.name = spec.substr(0, at);
        if(at != std::string::npos) {
            std::string phase = spec.substr(at + 1);
            if(phase == "act") entry.phase = Phase::Act;
            else if(phase == "observe") entry.phase = Phase::Observe;
            else throw SceneError(display, path, "unknown phase '" + phase + "' (act or observe)");
            if(where == SystemScope::Host) throw SceneError(display, path, "phases only apply to the systems of a world");
        }

        const SystemInfo* info = findSystem(entry.name);
        if(!info) throw SceneError(display, path, "unknown system " + entry.name);
        if(where == SystemScope::Host && info->scope == SystemScope::World) {
            throw SceneError(display, path, entry.name + " is a world system, used at host scope");
        }
        if(where == SystemScope::World && info->scope == SystemScope::Host) {
            throw SceneError(display, path, entry.name + " is a host system, used in a world");
        }
        if(info->needs_renderer && !options.allow_renderer) throw SceneError(display, path, entry.name + " needs the renderer");

        return entry;

    }

    std::vector<SystemEntry> systems(const json& list, const std::string& display, const std::string& path, SystemScope where) {
        if(!list.is_array()) throw SceneError(display, path, "expected an array of systems");
        std::vector<SystemEntry> out;
        for(size_t i = 0; i < list.size(); i++) out.push_back(system(list[i], display, pointer(path, i), where));
        return out;
    }

    EntityDef entity(const json& value, const std::string& display, const std::string& path) {

        if(!value.is_object()) throw SceneError(display, path, "an entity is an object of components");
        EntityDef def;
        def.file = display;
        def.path = path;
        def.components = json::object();

        if(auto prefab = value.find("prefab"); prefab != value.end()) {
            if(!prefab->is_string()) throw SceneError(display, path + "/prefab", "expected a prefab name");
            std::string name = prefab->get<std::string>();
            if(!fs::exists(options.resources / "prefabs" / (name + ".json"))) {
                throw SceneError(display, path + "/prefab", "unknown prefab " + name);
            }
            def.components = loadPrefab(options.resources, name);
        }

        json own = json::object();
        for(auto& [key, fields] : value.items()) {
            if(key == "prefab") continue;
            if(!findComponent(key)) throw SceneError(display, pointer(path, key), "unknown component " + key);
            if(!fields.is_object()) throw SceneError(display, pointer(path, key), "expected the fields of component " + key);
            own[key] = fields;
        }
        deepMerge(def.components, own);
        validateComponents(def.components, display, path);
        return def;

    }

    void validate(const SourceFile& f) {

        const json& c = f.content;
        const std::string& d = f.display;

        if(auto it = c.find("format_version"); it != c.end() && !(it->is_number_integer() && it->get<int>() == 1)) {
            throw SceneError(d, "/format_version", "unsupported format_version " + it->dump() + " (1)");
        }
        if(auto it = c.find("name"); it != c.end() && !it->is_string()) throw SceneError(d, "/name", "expected a string");

        if(auto it = c.find("renderer"); it != c.end()) {
            if(!options.allow_renderer) throw SceneError(d, "/renderer", "dynamical_train does not render: a renderer block is not allowed");
            checkKeys(*it, { "passes", "vr" }, d, "/renderer");
            if(auto vr = it->find("vr"); vr != it->end() && !vr->is_boolean()) throw SceneError(d, pointer("/renderer", "vr"), "expected true or false");
            auto passes = it->find("passes");
            if(passes == it->end() || !passes->is_array()) throw SceneError(d, pointer("/renderer", "passes"), "expected an array of pass names");
            std::set<std::string> seen;
            for(size_t i = 0; i < passes->size(); i++) {
                const json& p = (*passes)[i];
                std::string path = pointer(pointer("/renderer", "passes"), i);
                if(!p.is_string() || std::none_of(std::begin(scene_passes), std::end(scene_passes), [&](const char* n) { return p == n; })) {
                    throw SceneError(d, path, "unknown pass " + p.dump() + " (terrain, grass, model, ui)");
                }
                if(!seen.insert(p.get<std::string>()).second) throw SceneError(d, path, "pass listed twice");
            }
        }

        if(auto it = c.find("host"); it != c.end()) {
            checkKeys(*it, { "pre", "post" }, d, "/host");
            if(it->contains("pre")) systems((*it)["pre"], d, "/host/pre", SystemScope::Host);
            if(it->contains("post")) systems((*it)["post"], d, "/host/post", SystemScope::Host);
        }

        if(auto it = c.find("templates"); it != c.end()) {
            if(!it->is_object()) throw SceneError(d, "/templates", "expected an object of world templates");
            for(auto& [name, t] : it->items()) {
                std::string path = pointer("/templates", name);
                checkKeys(t, { "physics", "clock", "fixed", "entities" }, d, path);
                if(t.contains("physics") && !t["physics"].is_object()) throw SceneError(d, path + "/physics", "expected an object");
                if(t.contains("clock") && !t["clock"].is_object()) throw SceneError(d, path + "/clock", "expected an object");
                if(t.contains("fixed")) systems(t["fixed"], d, path + "/fixed", SystemScope::World);
                if(t.contains("entities")) {
                    if(!t["entities"].is_array()) throw SceneError(d, path + "/entities", "expected an array of entities");
                    for(size_t i = 0; i < t["entities"].size(); i++) entity(t["entities"][i], d, pointer(path + "/entities", i));
                }
            }
        }

        if(auto it = c.find("worlds"); it != c.end()) {
            if(!it->is_array()) throw SceneError(d, "/worlds", "expected an array of worlds");
            for(size_t i = 0; i < it->size(); i++) {
                const json& w = (*it)[i];
                std::string path = pointer("/worlds", i);
                checkKeys(w, { "template", "count", "systems" }, d, path);
                if(!w.contains("template") || !w["template"].is_string()) throw SceneError(d, path + "/template", "expected a template name");
                if(w.contains("count") && !(w["count"].is_number_integer() && w["count"].get<int64_t>() >= 0)) {
                    throw SceneError(d, path + "/count", "expected a non-negative integer, got " + w["count"].dump());
                }
                if(w.contains("systems")) {
                    const json& s = w["systems"];
                    checkKeys(s, { "replace", "remove" }, d, path + "/systems");
                    if(s.contains("replace")) {
                        if(!s["replace"].is_object()) throw SceneError(d, path + "/systems/replace", "expected an object");
                        for(auto& [from, to] : s["replace"].items()) system(to, d, pointer(path + "/systems/replace", from), SystemScope::World);
                    }
                    if(s.contains("remove") && !s["remove"].is_array()) throw SceneError(d, path + "/systems/remove", "expected an array");
                }
            }
        }

    }

    static void dedupe(std::vector<Located>& list) {
        // A host system runs once: a later file listing it again moves it (desktop.json, then the scene's order)
        std::vector<Located> out;
        std::set<std::string> seen;
        for(auto it = list.rbegin(); it != list.rend(); ++it) {
            std::string name = it->value.is_string() ? it->value.get<std::string>() : it->value["name"].get<std::string>();
            if(seen.insert(name).second) out.push_back(*it);
        }
        std::reverse(out.begin(), out.end());
        list = std::move(out);
    }

    SceneDef merge(const std::string& top) {

        SceneDef def;
        def.file = top;

        std::vector<Located> pre, post, worlds;
        std::map<std::string, Located> templates;
        std::optional<Located> renderer;

        for(auto& f : files) {
            const json& c = f.content;
            if(c.contains("name")) def.name = c["name"].get<std::string>();
            if(c.contains("renderer")) renderer = Located { c["renderer"], f.display, "/renderer" };
            if(c.contains("host")) {
                const json& host = c["host"];
                for(size_t i = 0; host.contains("pre") && i < host["pre"].size(); i++) pre.push_back({ host["pre"][i], f.display, pointer("/host/pre", i) });
                for(size_t i = 0; host.contains("post") && i < host["post"].size(); i++) post.push_back({ host["post"][i], f.display, pointer("/host/post", i) });
            }
            if(c.contains("templates")) {
                for(auto& [name, t] : c["templates"].items()) templates[name] = { t, f.display, pointer("/templates", name) };
            }
            if(c.contains("worlds")) {
                for(size_t i = 0; i < c["worlds"].size(); i++) worlds.push_back({ c["worlds"][i], f.display, pointer("/worlds", i) });
            }
        }
        dedupe(pre);
        dedupe(post);

        if(renderer) {
            if(!includes.count("common/desktop.json")) {
                throw SceneError(renderer->file, renderer->path, "a rendered scene must include common/desktop.json (Input, UI, PlayerControl, DebugMenu)");
            }
            RendererDef r;
            for(auto& p : renderer->value["passes"]) r.passes.push_back(p.get<std::string>());
            r.vr = renderer->value.value("vr", true);
            def.renderer = r;
        }

        for(auto& e : pre) def.host_pre.push_back(system(e.value, e.file, e.path, SystemScope::Host));
        for(auto& e : post) def.host_post.push_back(system(e.value, e.file, e.path, SystemScope::Host));

        for(auto& w : worlds) {
            std::string name = w.value["template"].get<std::string>();
            auto t = templates.find(name);
            if(t == templates.end()) throw SceneError(w.file, w.path + "/template", "unknown world template " + name);
            const json& tv = t->second.value;

            WorldDef world;
            world.template_name = name;
            world.count = w.value.value("count", 1);
            world.physics = tv.value("physics", json::object());
            world.clock = tv.value("clock", json::object());
            if(tv.contains("fixed")) world.fixed = systems(tv["fixed"], t->second.file, t->second.path + "/fixed", SystemScope::World);
            for(size_t i = 0; tv.contains("entities") && i < tv["entities"].size(); i++) {
                world.entities.push_back(entity(tv["entities"][i], t->second.file, pointer(t->second.path + "/entities", i)));
            }

            if(w.value.contains("systems")) {
                const json& s = w.value["systems"];
                auto find = [&](const std::string& spec, const std::string& path) {
                    SystemEntry wanted = system(spec, w.file, path, SystemScope::World);
                    auto it = std::find_if(world.fixed.begin(), world.fixed.end(), [&](const SystemEntry& e) { return e.name == wanted.name && e.phase == wanted.phase; });
                    if(it == world.fixed.end()) throw SceneError(w.file, path, "template " + name + " has no system " + spec);
                    return it;
                };
                const json replaced = s.value("replace", json::object());
                for(auto& [from, to] : replaced.items()) {
                    std::string path = pointer(w.path + "/systems/replace", from);
                    *find(from, path) = system(to, w.file, path, SystemScope::World);
                }
                const json removed = s.value("remove", json::array());
                for(size_t i = 0; i < removed.size(); i++) {
                    std::string path = pointer(w.path + "/systems/remove", i);
                    if(!removed[i].is_string()) throw SceneError(w.file, path, "expected a system name");
                    world.fixed.erase(find(removed[i].get<std::string>(), path));
                }
            }
            def.worlds.push_back(std::move(world));
        }

        // The resolved scene: what ran, loadable as a scene itself
        json r = json::object();
        r["format_version"] = 1;
        r["name"] = def.name;
        json vars = json::object();
        for(auto& [name, v] : variables) vars[name] = { { "type", v.type }, { "default", v.value } };
        r["variables"] = vars;
        if(renderer) r["renderer"] = renderer->value;
        json host = { { "pre", json::array() }, { "post", json::array() } };
        for(auto& e : pre) host["pre"].push_back(e.value);
        for(auto& e : post) host["post"].push_back(e.value);
        r["host"] = host;
        json t = json::object();
        for(auto& [name, tl] : templates) t[name] = tl.value;
        r["templates"] = t;
        json w = json::array();
        for(auto& wl : worlds) w.push_back(wl.value);
        r["worlds"] = w;
        if(resolved_from) {
            r["resolved_from"] = *resolved_from;
        } else {
            r["resolved_from"] = { { "scene", top }, { "includes", json(std::vector<std::string>(includes.begin(), includes.end())) } };
        }
        def.resolved = r;

        return def;

    }

    SceneLoadOptions options;
    fs::path scenes;
    std::vector<SourceFile> files;          // includes first, the scene last
    std::set<std::string> loaded;
    std::set<std::string> includes;         // every file included, directly or not
    std::optional<json> resolved_from;
    std::map<std::string, Variable> variables;
    std::map<std::string, json> referenced;
};

}

SceneDef dy::loadScene(const std::string& name, const std::vector<std::pair<std::string, std::string>>& vars, const SceneLoadOptions& options) {
    Loader loader(options);
    return loader.load(name, vars);
}

nlohmann::json dy::loadPrefab(const std::filesystem::path& resources, const std::string& name) {
    std::vector<std::string> chain;
    return loadPrefabChain(resources, name, chain);
}

void dy::writeResolved(const SceneDef& scene, const std::filesystem::path& file) {
    std::error_code ec;
    if(file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << scene.resolved.dump(2) << "\n";
}
