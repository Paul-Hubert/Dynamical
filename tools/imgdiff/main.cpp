// imgdiff <capture.png> <golden.png> [--mae 1.5] [--fraction 0.005] [--pixel 24] [--heatmap <out.png>] [--always-heatmap]
// Exit 0 within the thresholds, 1 outside them (a heatmap is written next to the capture), 2 on usage or I/O errors.

#include "imgdiff.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace dy::imgdiff;

int main(int argc, char** argv) {

    if(argc < 3) {
        std::fprintf(stderr, "usage: imgdiff <capture.png> <golden.png> [--mae 1.5] [--fraction 0.005] [--pixel 24] [--heatmap <out.png>] [--always-heatmap]\n");
        return 2;
    }

    std::string capture = argv[1], golden = argv[2];
    std::filesystem::path capture_path(capture);
    std::string heatmap_path = (capture_path.parent_path() / (capture_path.stem().string() + "_diff.png")).string();
    Thresholds thresholds;
    bool always_heatmap = false;

    for(int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        bool has_value = i + 1 < argc;
        if(arg == "--mae" && has_value) thresholds.max_mae = std::atof(argv[++i]);
        else if(arg == "--fraction" && has_value) thresholds.max_fraction = std::atof(argv[++i]);
        else if(arg == "--pixel" && has_value) thresholds.pixel_threshold = std::atoi(argv[++i]);
        else if(arg == "--heatmap" && has_value) heatmap_path = argv[++i];
        else if(arg == "--always-heatmap") always_heatmap = true;
        else {
            std::fprintf(stderr, "imgdiff: unknown argument %s\n", arg.c_str());
            return 2;
        }
    }

    Image a, b;
    if(!load(capture, a)) {
        std::fprintf(stderr, "imgdiff: cannot read %s\n", capture.c_str());
        return 2;
    }
    if(!load(golden, b)) {
        std::fprintf(stderr, "imgdiff: cannot read %s\n", golden.c_str());
        return 2;
    }

    Result r = compare(a, b, thresholds);
    if(!r.same_size) {
        std::printf("imgdiff %s: size %dx%d, golden %dx%d: FAIL\n", capture.c_str(), a.width, a.height, b.width, b.height);
        return 1;
    }

    std::printf("imgdiff %s: mae %.4f/255 (max %.2f), %.4f%% of pixels over %d/255 (max %.3f%%), largest %d: %s\n",
        capture.c_str(), r.mae, thresholds.max_mae, r.fraction * 100., thresholds.pixel_threshold, thresholds.max_fraction * 100.,
        r.max_difference, r.pass ? "PASS" : "FAIL");

    if(!r.pass || always_heatmap) {
        if(save(heatmap_path, heatmap(a, b))) std::printf("heatmap: %s\n", heatmap_path.c_str());
    }

    return r.pass ? 0 : 1;

}
