#ifndef SETTINGS_H
#define SETTINGS_H

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

    // 0 : desktop only, 1 : VR alongside the desktop window when an OpenXR headset is available
    int vr_mode = 1;
    int spectator_mode = 2;

    // Marching cubes chunks are loaded within this distance of the cameras, in world units
    static constexpr int default_view_distance = 768;
    int view_distance = default_view_distance;

    // One kind of grass tile everywhere (GrassKind : meadow, clover, dandelion...) instead of the patches of the species. Not saved
    std::string grass_kind;

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
            CEREAL_NVP(view_distance)
        );
    }

    void argument_override(int argc, char** argv);

private:
    #ifndef DYNAMICAL_CONFIG_DIR
        #define DYNAMICAL_CONFIG_DIR "./"
    #endif
    #define DYNAMICAL_CONFIG_MAGIC "6"
    #define DYNAMICAL_CONFIG_FILE DYNAMICAL_CONFIG_DIR "config." DYNAMICAL_CONFIG_MAGIC ".json"
};

}

#endif
