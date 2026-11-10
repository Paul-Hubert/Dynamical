#ifndef DY_TREE_SHADE_H
#define DY_TREE_SHADE_H

#include "renderer/util/vk.h"
#include "renderer/util/vmapp.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dy {

class Context;

// The shade of the plants on the ground, without shadow maps : each Plant taller than a walker draws the
// shadow of its crown as two soft discs on a map of the world seen from above, moved to where the sun
// (sun_direction of the shaders) puts them on the ground. Binding 1 of the view set : the terrain, the grass
// and the plants read it (treeShade in their fragment shaders, with flecks of sun from a noise).
//
// One texel a metre, the map wraps around every `size` metres : the shaders stop reading it before a plant
// that far could be seen. Plants are followed through the signals of the registry, they are not expected to move.
class TreeShade {
public:
    static constexpr int size = 2048;
    static constexpr float max_depth = 4.f;         // optical depth of a white texel
    static constexpr float reach = 700.f;           // metres from the camera where the shaders stop reading the map
    static constexpr glm::vec2 sun_slope = glm::vec2(0.5f, 0.5f);  // sun_direction.xz / sun_direction.y

    TreeShade(Context& ctx, entt::registry& reg);
    ~TreeShade();

    // Main thread, before the transfer commands of the frame are submitted
    void update();

private:
    struct Disc {
        glm::vec2 centre = glm::vec2(0.f);
        float radius = 0.f;
        float depth = 0.f;
    };

    void onPlant(entt::registry& reg, entt::entity entity);
    void onPlantDestroyed(entt::registry& reg, entt::entity entity);
    void draw(const Disc& disc, float sign);
    void upload();

    Context& ctx;
    entt::registry& reg;

    static constexpr int tile_shift = 6;
    static constexpr int tile = 1 << tile_shift;
    static constexpr int tiles = size / tile;

    std::unordered_map<entt::entity, std::array<Disc, 2>> plants;
    std::vector<float> depth;           // optical depth of the crowns over each texel
    std::vector<uint8_t> texels;
    std::vector<uint8_t> dirty;         // tiles of `tile` texels to convert again
    bool any_dirty = false;

    struct Image {
        std::shared_ptr<VmaImage> image;
        vk::ImageView view;
        int frames = 0;
    };
    Image current;
    std::vector<Image> retired;         // still read by the frames in flight
};

// ViewUBO::tree_shade, from the RenderSettings
glm::vec4 treeShadeView(const entt::registry& reg);

}

#endif
