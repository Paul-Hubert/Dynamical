#include <doctest/doctest.h>

#include "logic/settings/graphics.h"

#include <filesystem>
#include <fstream>

using namespace dy;

TEST_SUITE("unit") {

TEST_CASE("GRAPHICS.1 the five levels of resources/graphics.json, from the cheapest") {
    auto presets = loadGraphicsPresets("resources/graphics.json");
    REQUIRE(presets.size() == 5);
    const char* const names[] = { "ultralow", "low", "medium", "high", "ultrahigh" };
    for(int i = 0; i < 5; i++) {
        CHECK(presets[i].name == names[i]);
        CHECK(findGraphicsPreset(presets, names[i]) == i);
    }
    CHECK(findGraphicsPreset(presets, "extreme") == -1);
}

TEST_CASE("GRAPHICS.2 high is the defaults of the code : a capture without a level is drawn as with it") {
    auto presets = loadGraphicsPresets("resources/graphics.json");
    int high = findGraphicsPreset(presets, "high");
    REQUIRE(high >= 0);

    GraphicsPreset defaults;
    defaults.name = "high";
    CHECK(presets[high] == defaults);

    RenderSettings settings;
    applyGraphicsPreset(presets[high], settings);
    const RenderSettings code;
    CHECK(settings.lighting[0] == code.lighting[0]);
    CHECK(settings.lighting[1] == code.lighting[1]);
    CHECK(settings.probe_rays_per_frame == code.probe_rays_per_frame);
    CHECK(settings.rays_per_probe == code.rays_per_probe);
    CHECK(settings.far_plants_in_rays == code.far_plants_in_rays);
    CHECK(settings.far_plants_on_screen == code.far_plants_on_screen);
    CHECK(settings.textures == code.textures);
    CHECK(settings.relief == code.relief);
    CHECK(settings.grass_params.layers == code.grass_params.layers);
    CHECK(settings.grass_params.shadow == code.grass_params.shadow);
    CHECK(settings.grass_params.silhouette == code.grass_params.silhouette);
    CHECK(settings.vegetation_params.lod_bias == code.vegetation_params.lod_bias);
    CHECK(settings.vegetation_params.normal_maps == code.vegetation_params.normal_maps);
    CHECK(settings.vegetation_params.subsurface == code.vegetation_params.subsurface);
    CHECK(settings.streaming == code.streaming);
    CHECK(presets[high].view_distance == Settings::default_view_distance);
}

TEST_CASE("GRAPHICS.3 no level asks for less than the one before") {
    auto presets = loadGraphicsPresets("resources/graphics.json");
    REQUIRE(presets.size() >= 2);
    CHECK(!presets.front().ray_tracing);
    for(size_t i = 1; i < presets.size(); i++) {
        const GraphicsPreset& less = presets[i - 1];
        const GraphicsPreset& more = presets[i];
        CAPTURE(more.name);
        CHECK(more.view_distance >= less.view_distance);
        CHECK(more.ray_tracing >= less.ray_tracing);
        for(int target = 0; target < 2; target++) {
            CHECK((int) more.lighting[target].tier >= (int) less.lighting[target].tier);
            CHECK(more.lighting[target].traced_range >= less.lighting[target].traced_range);
            CHECK(more.lighting[target].sun_rays >= less.lighting[target].sun_rays);
            CHECK(more.lighting[target].plant_geometry_range >= less.lighting[target].plant_geometry_range);
            CHECK(more.lighting[target].shadowed_torches >= less.lighting[target].shadowed_torches);
            CHECK(more.lighting[target].animals >= less.lighting[target].animals);
        }
        CHECK(more.probe_rays_per_frame >= less.probe_rays_per_frame);
        CHECK(more.rays_per_probe >= less.rays_per_probe);
        CHECK(more.far_plants_in_rays >= less.far_plants_in_rays);
        CHECK(more.far_plants_on_screen >= less.far_plants_on_screen);
        CHECK(more.textures.grass_anisotropy >= less.textures.grass_anisotropy);
        CHECK(more.textures.ground_anisotropy >= less.textures.ground_anisotropy);
        CHECK(more.textures.vegetation_anisotropy >= less.textures.vegetation_anisotropy);
        CHECK(more.textures.animal_anisotropy >= less.textures.animal_anisotropy);
        CHECK(more.relief >= less.relief);
        CHECK(more.grass_layers >= less.grass_layers);
        CHECK(more.grass_shadow >= less.grass_shadow);
        CHECK(more.grass_see_through >= less.grass_see_through);
        CHECK(more.lod_bias >= less.lod_bias);
        CHECK(more.normal_maps >= less.normal_maps);
        CHECK(more.subsurface >= less.subsurface);
        CHECK(more.streaming.chunk_meshes >= less.streaming.chunk_meshes);
        CHECK(more.streaming.ray_chunks >= less.streaming.ray_chunks);
        CHECK(more.streaming.plant_volume_slices >= less.streaming.plant_volume_slices);
    }
    // The headset never gets more than the window
    for(auto& preset : presets) {
        CHECK(preset.lighting[1].traced_range <= preset.lighting[0].traced_range);
        CHECK(preset.lighting[1].sun_rays <= preset.lighting[0].sun_rays);
    }
}

TEST_CASE("GRAPHICS.4 a value left out is the default, comments are read over, a wrong file gives no level") {
    const auto file = std::filesystem::temp_directory_path() / "dynamical_graphics_test.json";
    {
        std::ofstream out(file);
        out << "// a comment\n{ \"presets\": [\n"
            << "{ \"name\": \"mine\", \"view_distance\": 320, \"lighting\": { \"desktop\": { \"traced_range\": 70 }, \"rays_per_probe\": 20 },\n"
            << "  \"textures\": { \"grass_anisotropy\": 64 }, \"grass\": { \"layers\": 2 } },\n"
            << "{ \"name\": \"mine\" }, { \"view_distance\": 100 } ] }\n";
    }
    auto presets = loadGraphicsPresets(file.string());
    REQUIRE(presets.size() == 1);   // neither the second of a name nor the one without

    GraphicsPreset expected;
    expected.name = "mine";
    expected.view_distance = 320;
    expected.lighting[0].traced_range = 70.f;
    expected.lighting[0].tier = RenderSettings::LightingTier::Medium;   // the highest tier that traces no further
    expected.rays_per_probe = 20;
    expected.textures.grass_anisotropy = 16;    // no sampler has more
    expected.grass_layers = 2;
    CHECK(presets[0] == expected);

    {
        std::ofstream out(file);
        out << "{ \"presets\": 3 ";
    }
    CHECK(loadGraphicsPresets(file.string()).empty());
    std::filesystem::remove(file);
    CHECK(loadGraphicsPresets(file.string()).empty());
}

}
