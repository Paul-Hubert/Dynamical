#ifndef SETTINGS_H
#define SETTINGS_H

#include "entt/entt.hpp"

#include <string>

#include "cereal/types/string.hpp"

#include <sstream>

class Settings {
public:
    Settings(entt::registry& reg, int argc, char** argv);
    
    void synchronize(entt::registry& reg);

    const static char magic_number = 2;
    int window_width = 0;
    int window_height = 0;
    bool fullscreen = true;
    int fps_max = 60;
    std::string username = "John Doe";

    bool server_side = false;
    bool client_side = false;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(
            CEREAL_NVP(window_width),
            CEREAL_NVP(window_height),
            CEREAL_NVP(fullscreen),
            CEREAL_NVP(fps_max),
            CEREAL_NVP(username),
            CEREAL_NVP(server_side),
            CEREAL_NVP(client_side)
        );
    }

    void argument_override(int argc, char** argv);

private:
    entt::registry& reg;
};

#endif
