#ifndef DY_RUN_ARGS_H
#define DY_RUN_ARGS_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dy {

// Process arguments as key=value pairs: scene, process, capture and probe keys.
// Window, VR and view distance stay in Settings (dynamical only).
struct RunArgs {
    // Scene
    std::string scene;
    std::vector<std::pair<std::string, std::string>> vars;   // var.<name>=<value>, command line order, last wins

    // Process
    std::string mode;
    int threads = 0;
    int64_t ticks = 0;          // dynamical: exit after this many frames when > 0
    int episodes = 0;
    int64_t seed = 0;           // dynamical: the worlds' base seed (dynamical_train takes the trainer's)

    // Capture
    std::string capture;                    // capture script (json)
    std::string capture_dir = "captures";   // where the PNGs go
    double fixed_frame_dt = 0.;             // > 0: the frame clock ignores the wall clock
    bool window_hidden = false;
    bool exit_after_capture = true;

    // dynamical: time each scene pass takes on the GPU (desktop target), logged at every capture and every
    // 300 frames. The window presents without waiting for the monitor
    bool gpu_times = false;

    // dynamical: false draws without the ray traced lighting on a GPU that has it (docs/lighting.md)
    bool ray_tracing = true;
    // dynamical: the tier of the ray traced lighting of both targets (off, low, medium, high), empty : the defaults
    std::string lighting;
    // dynamical: rays the probes may cast a frame, -1 : the default of the RenderSettings
    int probe_rays = -1;

    // Probes
    std::string probe;                      // csv file
    std::string probe_entities = "RigidBody";
    int probe_every = 60;

    // Fixed frame clock, synchronous terrain generation, no collider prefetch
    bool deterministic() const { return fixed_frame_dt > 0.; }
};

struct RunArgsResult {
    RunArgs args;
    std::vector<std::string> unknown;   // keys this parser does not know, in order
    std::vector<std::string> errors;    // malformed values
};

// Skips argv[0]. With capture or probe set, fixed_frame_dt defaults to 1/60 and window_hidden to true.
RunArgsResult parseRunArgs(int argc, char** argv);
RunArgsResult parseRunArgs(const std::vector<std::string>& args);

}

#endif
