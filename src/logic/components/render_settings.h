#ifndef DY_RENDER_SETTINGS_H
#define DY_RENDER_SETTINGS_H

#include <glm/glm.hpp>

namespace dy {

// Context variable : runtime toggles of the scene passes, edited from the debug menu
struct RenderSettings {
    bool terrain = true;
    bool grass = true;
    bool models = true;

    // Raymarched grass shells (2019 grass shader)
    struct Grass {
        glm::vec4 base_normal = glm::vec4(0.f, 1.f, 0.f, 0.f);
        float tile_size = 1.f;
        float height = 1.f;
        float noise_frequency = 0.0001f;
        float noise_amplitude = 0.1f;
    } grass_params;
};

}

#endif
