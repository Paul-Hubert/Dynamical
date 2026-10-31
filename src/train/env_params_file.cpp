#include "env_params_file.h"

#include <charconv>
#include <fstream>
#include <random>

using namespace dy;
namespace fs = std::filesystem;

std::string dy::roundTrip(float value) {
    char buffer[64];
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, ptr);
}

bool dy::writeEnvParamsFile(const fs::path& path, const EnvParamsFile& file) {

    std::error_code ec;
    fs::path full = fs::absolute(path, ec);
    if(full.has_parent_path()) fs::create_directories(full.parent_path(), ec);

    // The run id is the directory holding the file (results/<run>/envparams.txt)
    std::string run = full.parent_path().filename().string();

    // Every worker writes the same values: a per-process temporary, then an atomic replace
    std::random_device rd;
    fs::path tmp = full;
    tmp += ".tmp" + std::to_string(rd());
    {
        std::ofstream out(tmp, std::ios::binary);
        if(!out) return false;
        out << "# run=" << run << " behavior=" << file.behavior << " written=" << file.written;
        for(auto& [key, value] : file.header) out << " " << key << "=" << value;
        out << "\n";
        for(auto& [name, value] : file.values) out << name << " " << roundTrip(value) << "\n";
        if(!out) return false;
    }
    fs::rename(tmp, full, ec);
    if(ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;

}
