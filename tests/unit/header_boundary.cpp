#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The simulation library must stay free of graphics, window and headset code (architecture.md)
TEST_SUITE("unit") {

TEST_CASE("T1.1 header boundary: dynamical_sim sources name no vulkan, SDL, imgui, openxr or renderer/") {

    const char* dirs[] = { "src/logic", "src/physics", "src/scene", "src/util", "src/agent" };
    const char* words[] = { "vulkan", "SDL", "imgui", "openxr", "renderer/" };

    std::vector<std::string> hits;
    size_t files = 0;
    for(const char* dir : dirs) {
        if(!std::filesystem::exists(dir)) continue;
        for(auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            auto ext = entry.path().extension().string();
            if(!entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".inl")) continue;
            files++;
            std::ifstream in(entry.path());
            std::string line;
            for(int number = 1; std::getline(in, line); number++) {
                for(const char* word : words) {
                    if(line.find(word) != std::string::npos) {
                        hits.push_back(entry.path().generic_string() + ":" + std::to_string(number) + ": " + word);
                    }
                }
            }
        }
    }

    for(auto& hit : hits) MESSAGE(hit);
    CHECK(files > 20);
    CHECK(hits.empty());

}

}
