// Demonstrations of the procedural walk (logic/animals/gait_dataset.h, docs/animals.md) : animals of the library
// walk with styles, speeds and turns drawn from ranges, and what they did is written down, for imitation learning.
//
//   gait_dataset --species deer_red,horse,wolf_2 [--gait walk] [--samples 64] [--seconds 10] [--fps 30] [--seed 1]
//                [--flat] [--ranges ranges.json] [--out build/artifacts/gait_demos]
//
// --species all : every animal that has the gait and whose legs the walk can read.
// --flat : on a level plane (by default, on the marching cubes terrain, started where it is gentle).
// --ranges : a json file of what to draw from (GaitRanges), over the engine's own.
// Run from the build folder (resources/mclookup.bin). (build with -DDYNAMICAL_BUILD_TOOLS=ON)

#include "logic/animals/gait_dataset.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace dy;

int main(int argc, char** argv) {

    GaitDemoOptions options;
    std::string species = "deer_red", out = "artifacts/gait_demos", ranges;
    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if(arg == "--species") species = next();
        else if(arg == "--gait") options.gait = next();
        else if(arg == "--samples") options.samples = std::stoi(next());
        else if(arg == "--seconds") options.seconds = std::stof(next());
        else if(arg == "--fps") options.fps = std::stoi(next());
        else if(arg == "--seed") options.seed = (uint32_t) std::stoul(next());
        else if(arg == "--flat") options.terrain = false;
        else if(arg == "--ranges") ranges = next();
        else if(arg == "--out") out = next();
        else {
            std::cerr << "unknown argument " << arg << "\n";
            return 2;
        }
    }

    if(!ranges.empty()) {
        std::ifstream file(ranges);
        if(!file) {
            std::cerr << "cannot open " << ranges << "\n";
            return 2;
        }
        options.ranges = GaitRanges::parse(nlohmann::json::parse(file));
    }

    AnimalLibrary library;
    if(!library.load(animalsDirectory())) {
        std::cerr << "no animal library in " << animalsDirectory().string() << " (tools/animal_import.py builds it)\n";
        return 1;
    }

    std::vector<std::string> ids;
    if(species == "all") {
        for(auto& entry : library.entries) ids.push_back(entry.id);
    } else {
        for(size_t start = 0; start <= species.size();) {
            size_t end = std::min(species.find(',', start), species.size());
            if(end > start) ids.push_back(species.substr(start, end - start));
            start = end + 1;
        }
    }

    int written = 0;
    for(auto& id : ids) {
        options.species = id;
        GaitDemoResult result = writeGaitDemos(library, options, out);
        if(!result.ok) {
            std::cout << id << " : left out (" << result.error << ")\n";
            continue;
        }
        written++;
        std::cout << id << " : " << result.samples << " walks of " << result.frames << " frames, " << result.legs << " legs, " << result.joints << " joints, "
            << result.mean_speed << " m/s on average, feet down " << (int) (100.f * result.planted) << " % of the time, planted feet under "
            << result.slide << " m/s -> " << result.data.string() << "\n";
    }
    std::cout << written << " of " << ids.size() << " animals written\n";
    return written > 0 ? 0 : 1;

}
