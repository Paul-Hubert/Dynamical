#include "settings.h"

#include <filesystem>
#include <fstream>

#include "cereal/archives/json.hpp"

using namespace dy;

void Settings::load() {
    if(std::filesystem::exists(DYNAMICAL_CONFIG_FILE)) {
        std::ifstream is(DYNAMICAL_CONFIG_FILE);
        cereal::JSONInputArchive in(is);
        serialize(in);
    }

    save();

}

void Settings::save() {

    std::ofstream os(DYNAMICAL_CONFIG_FILE);

    cereal::JSONOutputArchive out(os);
    serialize(out);
}

Settings::Settings(int argc, char** argv) {
    load();
    argument_override(argc, argv);
}

void Settings::argument_override(int argc, char** argv) {

#define ARGUMENT(T, value) \
if(key == #T) T = value;

    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];
        std::stringstream ss(arg);
        std::string key;
        std::getline(ss, key, '=');
        std::string value;
        std::getline(ss, value);
        ARGUMENT(window_width, std::atoi(value.c_str()))
            ARGUMENT(window_height, std::atoi(value.c_str()))
            ARGUMENT(fullscreen, value == "true")
            ARGUMENT(fps_max, std::atoi(value.c_str()))
            ARGUMENT(username, value)
            ARGUMENT(server_side, value == "true")
            ARGUMENT(client_side, value == "true")
    }

#undef ARGUMENT

}
