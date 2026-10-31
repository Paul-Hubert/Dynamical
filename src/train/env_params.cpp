#include "env_params.h"

#include <charconv>
#include <fstream>

using namespace dy;

namespace {

bool parseFloat(const std::string& text, float& out) {
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc() && ptr == text.data() + text.size();
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}

bool EnvParamStore::loadFile(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if(!in) {
        error = "cannot read -envParams file '" + path + "'";
        return false;
    }
    std::string raw;
    for(int number = 1; std::getline(in, raw); number++) {
        std::string line = trim(raw);
        if(line.empty() || line[0] == '#') continue;
        size_t space = line.find_first_of(" \t");
        float value;
        if(space == std::string::npos || space == 0 || !parseFloat(trim(line.substr(space + 1)), value)) {
            error = "malformed line " + std::to_string(number) + " in '" + path + "': " + raw;
            return false;
        }
        file[line.substr(0, space)] = value;
    }
    return true;
}

bool EnvParamStore::setOverride(const std::string& name, const std::string& value, std::string& error) {
    float v;
    if(!parseFloat(value, v)) {
        error = "-set:" + name + "=" + value + " is not a number";
        return false;
    }
    overrides[name] = v;
    return true;
}

void EnvParamStore::setLive(const std::string& name, float value) {
    live[name] = value;
}

std::map<std::string, float> EnvParamStore::resolved() const {
    std::map<std::string, float> out = file;
    for(auto& [k, v] : live) out[k] = v;
    for(auto& [k, v] : overrides) out[k] = v;
    return out;
}
