#ifndef SETTINGS_H
#define SETTINGS_H

#include "map_configuration.h"

#include "entt/entt.hpp"

#include <string>

#include <cereal/types/string.hpp>

#include <sstream>

namespace dy {

class Settings {
public:
    Settings(int argc, char** argv);
    
    void save();
    void load();

    int window_width = 0;
    int window_height = 0;
    bool fullscreen = true;
    int fps_max = 60;
    std::string username = "John Doe";

    bool server_side = false;
    bool client_side = false;

    int vr_mode = 1;
    int spectator_mode = 2;

    MapConfiguration map_configuration;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(
            CEREAL_NVP(window_width),
            CEREAL_NVP(window_height),
            CEREAL_NVP(fullscreen),
            CEREAL_NVP(fps_max),
            CEREAL_NVP(username),
            CEREAL_NVP(server_side),
            CEREAL_NVP(client_side),
            CEREAL_NVP(vr_mode),
            CEREAL_NVP(spectator_mode),
            CEREAL_NVP(map_configuration)
        );
    }

    void argument_override(int argc, char** argv);

private:
    #ifndef DYNAMICAL_CONFIG_DIR
        #define DYNAMICAL_CONFIG_DIR "./"
    #endif
    #define DYNAMICAL_CONFIG_MAGIC "4"
    #define DYNAMICAL_CONFIG_FILE DYNAMICAL_CONFIG_DIR "config." DYNAMICAL_CONFIG_MAGIC ".json"
};

}

#endif
