# Profiling

The engine is instrumented for [Tracy](https://github.com/wolfpld/tracy) (`lib/tracy`, a submodule at v0.15.0):
zones on the CPU, zones on the GPU, frames and a few plots, on one time line. It replaces Optick, which is gone.

## Running

```bash
# the profiler runs for the whole process ; open the Tracy profiler and connect, now or later
build/RelWithDebInfo/dynamical.exe profile=true scene=forest
build/RelWithDebInfo/TrainingEnv.exe profile=true scene=sandbox_heuristic mode=bench

# or as text : the run, its trace and what tracy-csvexport makes of it
python tools/profile_run.py --name walk -- scene=tautavel_walk ticks=600 window_hidden=true
```

- **Without `profile=true` there is no profiler**: no thread, no socket, nothing recorded. A zone is then one
  branch on a flag. The tests and the tools never start it. Tracy is built with `TRACY_MANUAL_LIFETIME` for that:
  its own start measures its clock for 200 ms, which every test process would pay.
- **With it, events are kept from the first instruction** until a profiler connects, so the start of the process
  (scene load, bakes, pipelines) is in the trace whenever the profiler comes. Nobody connecting means that memory
  only grows: it is a switch for a run that is looked at.
- The profiler connects **from this machine** (`TRACY_ONLY_LOCALHOST`: no firewall prompt). `-DTRACY_ONLY_LOCALHOST=OFF`
  to profile from another one. The first profiled process listens on port 8086, the next ones on the ports after it.
- `-DDYNAMICAL_PROFILE=OFF`: the macros are empty and Tracy is not compiled.
- The GUI (`lib/tracy/profiler`) and the tools (`capture`, `csvexport`) are CMake projects of the submodule; they must
  be the client's version. `tools/profile_run.py` builds the two tools in `build/tracy_tools` the first time.

`tools/profile_run.py` writes in `build/profiles/<name>/`: `trace.tracy`, `zones.csv` and `zones_self.csv` (every CPU
zone: calls, total, mean, min, max), `gpu.csv` (every GPU zone: calls, total, mean, median, 99th percentile, max,
per frame), `gpu_events.csv` (each one of each frame), `frames.csv`, and `summary.txt` with the largest of each.
These are what to read, or to hand to an LLM, when the question is where a frame goes.

### The walk over the land

```bash
python tools/profile_run.py --name walk --between "CameraWalk : started" "CameraWalk : finished" --frame-set Walk -- 
    scene=tautavel_walk var.exit=true gpu_times=true window_hidden=true fullscreen=false window_width=1920 window_height=1080 vr_mode=0
```

`tautavel_walk` has everything at once (forests, herds, soil and its relief, the species of the grass, the traced
lighting), and its camera walks 330 m through it: the profile of reference.

- `var.exit=true` (the `exit` parameter of `CameraWalk`): the process ends with the walk.
- `--between FIRST LAST` keeps the zones that begin between two messages of the trace. The `CameraWalk` says
  `CameraWalk : started` once the land is loaded and it sets off, and `CameraWalk : finished`: the loading is in
  the trace, not in the tables. `profile::message("...")` anywhere else for other cuts.
- `--frame-set Walk`: the frames of the walk (`DY_FRAME_NAMED("Walk")` in its tick) are what "per frame" divides by.
- `gpu_times=true` presents without waiting for the monitor, so that a frame lasts what its work takes.
- Not `capture=` or `fixed_frame_dt=`: a fixed clock generates the chunks of a frame before it goes on, which is
  not what a player gets.

## What is recorded

**Frames.** One per turn of the main loop of `dynamical` (`Game::start`); `Headset` is the second set, one per
`xrEndFrame`. Without a window (`dynamical_train`) a frame is one `ScenarioRunner::step`: a decision window of
every world.

**Systems.** `SystemSet` opens a zone around every call of every system, named as the scene names it (`Physics`,
`ChunkGenerator`, `Herds`...): `pre_tick`, `post_tick` and `fixed_tick` hold them, and `preinit <name>`,
`init <name>`, `finish <name>` are the same at the start and at the end. A system needs nothing for this, and
has no zone of its own at the top of its `tick`. Inside, the parts worth seeing apart have theirs:

| Zone | Where |
|---|---|
| `Physics step`, `Fixed steps before`, `Jolt update`, `Fixed steps after` | `PhysicsSys`, each 60 Hz step of a frame |
| `Build collider cell` | `TerrainColliders::run`, on the workers |
| `Generate chunk`, `Wait for the chunks` | `ChunkGeneratorSys`, on the workers |
| `Mesh chunks` | `TerrainSys`, recording the marching cubes |
| `Evaluate animal poses` | `AnimalAnimationSys`, on the workers |
| `Load plant species`, `Load animal`, `Decode ground texture` | the loads of the passes, on the workers |
| `World tick`, `Worlds::step`, `Step worlds` | the worlds of a scene |
| `Environment step`, `Gather agents`, `Write observations`, `Trainer exchange` | `dynamical_train` and its trainer |

**Renderer.** `Renderer::prepare` (`Wait for the frame's fence`: the CPU waiting for the GPU, or for the monitor when the window is synchronised with it), `Renderer::render`
with `Update` (`update lights`, `update <pass>` for each pass, `update ray scene`, `update probes`...), `Record`
(`ClassicRender::record` and `VRRender::record` on two workers, `record <pass> (desktop)` inside), `Submit`,
`Present`, and the OpenXR calls of the headset (`Wait for the headset's frame`).

**GPU.** Timestamps around what the command buffers hold, read back by Tracy when the GPU is done:

| Zone | Command buffer |
|---|---|
| `Marching cubes`, `Ray scene` (`Build bottom structures`, `Build top structure`), `Probes`, `Bake plant volume`, `Skin animals for the rays` | compute |
| `Desktop` : `<pass> (desktop)` for each pass, `Sun rays (desktop)`, `Composite (desktop)` | the window's |
| `Headset` : `Headset eye 0`, `Headset eye 1`, each with `<pass> (headset)`, `Sun rays (headset)`, `Composite (headset)` | the headset's |

**Plots.** `Physics steps`, `Active bodies`, `Chunk jobs`, `Chunks meshed`.

**Threads.** `Main`, and `Worker <n>` for the workers of the executors (`profile::namedWorkers()`).

## Adding zones

`util/profile.h`, included by every system through `logic/systems/system.h`:

```cpp
DY_ZONE();                      // until the end of the scope, named after the function
DY_ZONE_N("Jolt update");       // with a name of its own (a string literal)
DY_ZONE_DYNAMIC(name);          // with a name known when it runs (const char*, copied)
DY_PLOT("Chunk jobs", (int64_t) jobs.size());
DY_FRAME_NAMED("Headset");      // a frame of another kind than the main loop's
```

On the GPU, `renderer/context/gpu_profile.h` (through `context.h`):

```cpp
vk::CommandBuffer command = ctx.compute.getCommandBuffer();
DY_GPU_ZONE(ctx.gpu_profile, command, "Marching cubes");        // until the end of the scope
DY_GPU_ZONE_DYNAMIC(ctx.gpu_profile, command, name);
```

- A GPU zone writes a timestamp where it begins and another where it ends: **it ends before its command buffer
  does** (a block around the zones when the function ends the buffer itself, as the `record` of the targets do).
- **Every command buffer a zone was written in is submitted.** Tracy reads its queries in the order they were
  taken and waits for one that never comes; the zones after it would never show.
- In a render pass or out of one. Not inside a multiview pass (a timestamp takes one query per view there); the
  headset is drawn an eye at a time.
- Names are per target (`Terrain (desktop)`, `Terrain (headset)`, from `PassZones`): statistics are by name.

## How it is built

- `profile::start()` (main, before the registry and its workers) calls `tracy::StartupProfiler()`; `profile::stop()`
  at the end, once the executors are gone, sends what is left. `profile::active()` is the flag every macro tests.
- `GpuProfile` (`Context::gpu_profile`, after the device) makes the Tracy Vulkan context when the profiler runs
  and the graphics queue has timestamps. The device then asks for `VK_EXT_calibrated_timestamps` where the GPU has
  it: the clock of the GPU is tied to the CPU's again at every collect, instead of once at the start.
- One context for the three queues: they are the same queue (`Device`). Should they come apart, each needs its own.
- `GpuProfile::collect` runs once a frame at the start of a target's command buffer (the window's, the headset's
  when the window does not draw): it reads the queries the GPU has written and resets them in that buffer.
- `gpu_times=true` is still there (`ClassicRender`): the means of the passes of the window in the log, with
  nothing to build or connect.

## Not done

- No zones in the transfer command buffers (uploads), no memory or lock profiling, no call stacks
  (`TRACY_CALLSTACK`), and the log does not go to the profiler's messages (`profile::message` is there for it).
- Sampling and context switches need the profiled process to run as administrator on Windows.
- Not run with a headset: the headset's zones and its frame set are written as the window's, untested.
