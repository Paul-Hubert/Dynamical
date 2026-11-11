// The physical bodies of the animals (logic/animals/animal_body.h, docs/physical-agents/reports/animal-agents.md) :
// generated from the library and the gait analysis, written next to the library as bodies/<id>.body.json.
//
//   animal_body --species all|deer_red,wolf_2 [--overrides resources/agents/animal_overrides.json] [--out <dir>]
//               [--check] [--summary <file.json>]
//
// --species all : every animal whose walk clip the gait analysis reads.
// --check : every body written is then stood for 10 s on its drives and dropped limp (logic/animals/animal_agent.h).
// Run from the repository root or the build folder. (build with -DDYNAMICAL_BUILD_TOOLS=ON)

#include "logic/animals/animal_agent.h"
#include "logic/animals/animal_body.h"
#include "physics/jolt_runtime.h"
#include "scene/system_registry.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace dy;

int main(int argc, char** argv) {

    std::string species = "all", overrides_path, out, summary_path;
    bool check = false;
    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if(arg == "--species") species = next();
        else if(arg == "--overrides") overrides_path = next();
        else if(arg == "--out") out = next();
        else if(arg == "--summary") summary_path = next();
        else if(arg == "--check") check = true;
        else {
            std::cerr << "unknown argument " << arg << "\n";
            return 2;
        }
    }

    AnimalLibrary library;
    if(!library.load(animalsDirectory())) {
        std::cerr << "no animal library in " << animalsDirectory().string() << " (tools/animal_import.py builds it)\n";
        return 1;
    }
    std::filesystem::path directory = out.empty() ? animalsDirectory() / "bodies" : std::filesystem::path(out);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    nlohmann::json overrides = nlohmann::json::object();
    if(overrides_path.empty()) {
        for(const char* candidate : { "resources/agents/animal_overrides.json", "../resources/agents/animal_overrides.json" }) {
            if(std::filesystem::exists(candidate, ec)) overrides_path = candidate;
        }
    }
    if(!overrides_path.empty()) {
        std::ifstream file(overrides_path);
        if(!file) {
            std::cerr << "cannot open " << overrides_path << "\n";
            return 2;
        }
        overrides = nlohmann::json::parse(file);
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

    std::unique_ptr<JoltRuntime> jolt;
    if(check) {
        jolt = std::make_unique<JoltRuntime>();
        registerSimSystems();
    }

    nlohmann::json summary = nlohmann::json::array();
    int written = 0;
    for(auto& id : ids) {
        nlohmann::json row = { { "species", id } };
        int index = library.find(id);
        if(index < 0) {
            std::cout << id << " : not in the library\n";
            row["error"] = "not in the library";
            summary.push_back(row);
            continue;
        }
        const auto& entry = library.entries[index];
        row["height"] = entry.height;
        row["length"] = entry.length;
        std::string error;
        nlohmann::json body;
        try {
            auto loaded = AnimalLibrary::loadSpecies(library.directory, id);
            if(loaded->walk < 0) error = "no walk clip";
            else {
                std::vector<uint32_t> indices;
                auto vertices = AnimalLibrary::loadGeometry(library.directory, *loaded, indices);
                GaitModel walk = analyseGait(*loaded, loaded->walk);
                row["joints_skeleton"] = loaded->parents.size();
                AnimalBodyOptions options = AnimalBodyOptions::parse(overrides, id);
                body = generateAnimalBody(entry, *loaded, vertices, indices, walk, options, error);
                if(error.empty()) row["legs"] = walk.legs.size();
            }
        } catch(const std::exception& e) {
            error = e.what();
        }
        if(!error.empty()) {
            std::cout << id << " : no body (" << error << ")\n";
            row["error"] = error;
            summary.push_back(row);
            continue;
        }
        auto path = std::filesystem::absolute(directory / (id + ".body.json"));
        std::ofstream(path) << body.dump(1) << "\n";
        written++;
        row["bodies"] = body["segments"].size();
        row["mass"] = body["total_mass"];
        row["volume"] = body["volume"];
        row["com_height"] = body["com_height"];
        row["substeps"] = body["substeps"];
        row["stand_clip"] = body["stand_clip"];
        int hinges = 0;
        for(auto& j : body["joints"]) hinges += j["hinge"].get<bool>() ? 1 : 0;
        row["hinges"] = hinges;
        std::cout << id << " : " << body["segments"].size() << " bodies (" << hinges << " hinges), " << body["total_mass"].get<float>() << " kg (volume "
            << body["volume"]["used"].get<double>() * 1000. << " l, signed triangles " << body["volume"]["signed_triangles"].get<double>() * 1000. << " l, shapes " << body["volume"]["shapes"].get<double>() * 1000. << " l), centre of mass "
            << body["com_height"].get<float>() << " m, " << body["substeps"].get<int>() << " substeps";
        if(check) {
            try {
                AnimalStandResult stand = animalStandTest(path.string(), 10.f, false);
                AnimalStandResult limp = animalStandTest(path.string(), 10.f, true);
                row["stand"] = stand.json();
                row["limp"] = limp.json();
                std::cout << " | stands: " << (stand.stands ? "yes" : "NO") << " (trunk " << stand.trunk_drop << " m lower, " << stand.drift << " m aside, "
                    << stand.max_speed << " m/s at most, tilt " << stand.tilt << " deg) | limp: " << (limp.fell && limp.rests ? "falls and rests" : limp.fell ? "falls, does NOT rest" : "does NOT fall")
                    << " (trunk at " << limp.trunk_height << " of its standing height, " << limp.end_speed << " m/s at the end)";
            } catch(const std::exception& e) {
                row["check_error"] = e.what();
                std::cout << " | check failed: " << e.what();
            }
        }
        std::cout << "\n";
        summary.push_back(row);
    }
    if(summary_path.empty()) summary_path = (directory / "summary.json").string();
    std::ofstream(summary_path) << summary.dump(1) << "\n";
    std::cout << written << " bodies in " << directory.string() << "\n";
    return written > 0 ? 0 : 1;

}
