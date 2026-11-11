#ifndef DY_GRASS_BAKE_H
#define DY_GRASS_BAKE_H

#include "grass_model.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dy {

// Rays against a grass tile repeated to infinity on the ground plane (y = 0), which stops every ray
// that comes down : a baked ray always hits something.
class GrassTracer {
public:
    struct Hit {
        float t = 0.f;
        glm::vec3 normal = glm::vec3(0.f, 1.f, 0.f);   // faces the ray
        bool blade = false;                             // else the ground
        float id = 0.5f;                                // of the blade (GrassModel::ids), 0.5 on the ground
        GrassPart part = GrassPart::Blade;              // of the blade (GrassModel::parts)
        glm::vec3 colour = grass_root_colour;           // of the blade there (GrassModel::colours), linear
    };

    explicit GrassTracer(GrassModel tile, int cells = 48);

    // direction does not need to be normalized, t is in units of its length
    bool trace(glm::vec3 origin, glm::vec3 direction, Hit& hit, float max_t = 1e4f) const;

    const GrassModel& model() const { return tile; }

private:
    struct Entry {
        uint32_t triangle;      // first index
        int16_t shift_x, shift_z;   // copy of the tile that reaches this cell
    };

    GrassModel tile;
    int cells;
    std::vector<std::vector<Entry>> grid;   // cells x cells over one tile, [z * cells + x]
    std::vector<float> tops;                // highest point of the triangles of each cell : rays over it pass
};

// Elevations of the oblique rays : 22.5, 45 and 67.5 degrees under the horizon, and before them the lowest
// rays, for the views along the ground (GrassBakeParams::low_from). The vertical is the top down rays
constexpr int grass_elevations = 4;

// Rays closer to the horizontal are looked up as if they crossed this many tiles, and the lowest rays
// are baked over that far
constexpr float grass_max_reach = 3.f;

// Of the rays that go down the whole grass (elevations 1 and over), in radians under the horizon
float grassElevation(int elevation);
// Tiles a baked ray crosses on its way : the cotangent of its elevation while it goes down the whole
// grass, grass_max_reach for the lowest rays
float grassReach(int elevation);

struct GrassBakeParams {
    int size = 192;         // samples of a tile along x and z
    // Azimuths of the rays of each elevation, the lowest first : the lower the rays, the more a blade
    // moves from an azimuth to the next
    // (204 in all : ten kinds of tiles are the 2040 layers of one image, GPUs take 2048)
    std::array<int, grass_elevations> angles = {72, 56, 48, 28};
    // The lowest rays go from the height low_from to low_to over grass_max_reach tiles : a ray along the
    // ground only meets the upper part of the blades. 1 to 0.5 is 9.5 degrees under the horizon. The
    // same height twice is horizontal rays in the grass (at its top they would meet nothing)
    float low_from = 1.f, low_to = 0.5f;
    GrassModelParams model;
};

// What the grass shader looks up instead of tracing rays (grass.frag.glsl).
// A texel is RGBA8 : r how much of its way the ray had left where it hit (1 at its start, 0.5 over the
// texel, 0 at its end), g b the normal of the hit (encodeGrassNormal), a the id of the blade (0.5 on the
// ground). For the rays that go down the whole grass r is the height of the hit : 0 ground, 1 top.
// The tile is (x, z) in [0, 1)², texel centers at (i + 0.5) / size.
struct GrassRaycast {
    int size = 0;
    std::array<int, grass_elevations> angles {};
    float low_from = 1.f, low_to = 0.5f;    // GrassBakeParams
    uint64_t key = 0;       // grassBakeKey of the parameters it was baked with

    // [elevation][angle][z][x], the elevations one after the other with `angles` slices each : ray towards
    // the azimuth (angle + 0.5) / angles * 2 pi (x to z) that is above the texel in the middle of its way.
    //  - elevations 1 and over : it starts at the top of the grass grassReach / 2 tiles before the texel,
    //    crosses the middle of the grass (y = 0.5) above it and ends on the ground
    //  - elevation 0, the lowest rays : it starts at low_from grass_max_reach / 2 tiles before the texel
    //    and is at low_to as far after it. Further on it goes on down for a blade, but r stays 0
    std::vector<uint8_t> oblique;
    // [z][x] : ray straight down on the texel
    std::vector<uint8_t> top;
    // The colour map : what each of those rays met, RGBA8 texels laid out like theirs. rgb is the colour of the
    // blade (GrassModel::colours) or of the ground, sRGB encoded (encodeGrassColour), a is 255
    std::vector<uint8_t> oblique_colour, top_colour;

    int slices() const;                     // of all the elevations
    int firstSlice(int elevation) const;

    // Of the lowest rays, in radians under the horizon : 0 when they are horizontal
    float lowElevation() const;
    // Height of a ray of that elevation where r of its way is left
    float height(int elevation, float r) const;
};

// Changes with the parameters and with the baker itself : a cached file with another key is stale
uint64_t grassBakeKey(const GrassBakeParams& params);

// threads = 0 : one per core
GrassRaycast bakeGrass(const GrassBakeParams& params = {}, unsigned threads = 0);
// The same rays on any tile of triangles (params.model is not read) : the relief of the ground textures
// (logic/terrain/ground_relief.h). cells : of the grid the triangles are sorted in, along x and z
GrassRaycast bakeGrassModel(GrassModel model, const GrassBakeParams& params, uint64_t key, unsigned threads = 0, int cells = 48);

bool saveGrassRaycast(const GrassRaycast& data, const std::string& path);
bool loadGrassRaycast(GrassRaycast& data, const std::string& path);

// Box filtered mip chain of RGBA8 texels, level after level starting with the image itself.
// levels : in, the most to make; out, how many were made (a dimension of 1 stays at 1).
// layers : the depth is layers of a 2D array, every level keeps them all
std::vector<uint8_t> rgbaMipChain(const std::vector<uint8_t>& image, int width, int height, int depth, int& levels, bool layers = false);

// --- CPU mirror of grass.frag.glsl, for the tests and the preview of tools/raycast ---

// Normal in two values in 0..1 : octahedral, +y in the middle. The upper half of the sphere is the inner
// diamond, the lower half the corners : no seam to wrap, texels filter and average like any other value
glm::vec2 encodeGrassNormal(glm::vec3 normal);
glm::vec3 decodeGrassNormal(glm::vec2 encoded);

// Linear colour in three sRGB bytes, as the GPU reads an sRGB texture
void encodeGrassColour(glm::vec3 colour, uint8_t* texel);
glm::vec3 decodeGrassColour(const uint8_t* texel);

// Filtered like the shader does (linear in the tile and between the two nearest azimuths, repeat) :
// the texel as it gets it, in 0..1
glm::vec4 sampleGrassOblique(const GrassRaycast& data, int elevation, glm::vec2 q, float azimuth);
glm::vec4 sampleGrassTop(const GrassRaycast& data, glm::vec2 q);
// The colour maps, filtered the same way (linear colours)
glm::vec3 sampleGrassObliqueColour(const GrassRaycast& data, int elevation, glm::vec2 q, float azimuth);
glm::vec3 sampleGrassTopColour(const GrassRaycast& data, glm::vec2 q);

struct GrassLookup {
    float height;       // of the hit on the view ray, 1 at the top of the grass
    float baked;        // height read in the textures (how deep the hit is in the grass)
    glm::vec3 normal;
    float id;           // of the blade
    glm::vec3 colour;   // of the blade, linear
};

// The view ray enters the top of the grass at q_top and goes down along the horizontal direction
// `along` (normalized); it crosses `reach` tiles before the ground (cotangent of its elevation, as large
// as it gets : the ray is looked up as if it crossed grass_max_reach at most, `height` is on that ray).
// The two baked elevations around the ray's are blended (along their reach), the last one and the top down
// rays over 67.5 degrees.
// top_start : part of the way from 67.5 degrees to the vertical where the top down rays start to blend in
GrassLookup lookupGrass(const GrassRaycast& data, glm::vec2 q_top, glm::vec2 along, float reach, float top_start);

// Over ground that falls away (a crest, the edge of a step) a view ray comes out of the grass again : what
// the lookup met deeper under the top of the grass than this is not drawn, the air behind shows.
// sink : height the ray loses on the ground per metre of its way, falling : how much the ground bends down
// along the view, per metre (minus the second derivative of its height, groundCurve), flat_length :
// horizontal part of the unit view vector. Infinite on flat ground and in hollows.
// The ray is sink l - falling (flat_length l)² / 2 deep after l : sink² / (2 falling flat_length²) at most,
// which is the depth returned
float grassSeeThrough(float sink, float falling, float flat_length);

}

#endif
