# Capture

`Capture` in [`src/view/capture.cpp`](../../../src/view/capture.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by `Game::start` with `capture=<script.json>`) · Parameters: the capture script

Not a system: the object `Game::start` makes when the run has `capture=<script.json>`. It runs the script: scripted
input at given frames, the desktop camera of each shot, a wait until the land is loaded, then the frame read back by the
renderer written as a PNG. The visual tests (`tools/visual_test.py`) and the video tools are built on it. The same file
holds the four "ready" predicates that CameraWalk uses too.

## Behaviour

- **readScene** (static, before the scene loads): the script's `scene` (default `base`) and `vars`; `Game::start`
  refuses a `scene=` that contradicts it.
- **Constructor**: parses the script (`hide_ui`, `shots` with increasing frames, `sequence`, `input`); errors (unknown
  key name, mouse button, decreasing frames, no shots) make `failed()` true and the run return 1. Creates `capture_dir`.
- **beginFrame(frame)**, before the systems tick:
  1. Pushes the input events of this frame into SDL's queue (key down / up by scancode name, mouse button with
     position, relative motion); `mouse` also sets ImGui's `MousePos` (the hidden window never has the OS cursor).
  2. `hide_ui`: ImGui's style alpha 0.
  3. Remembers the spawn camera on the first frame.
  4. When the next shot has a camera: position = `pos` (else the spawn) + `offset`; `above` puts it that high over
     the ground under it (a `TerrainField` on the `TerrainGenerator`, made once); yaw / pitch (else the spawn's), or
     toward `look_at`. A shot without a camera leaves it (scripted input or a scene system may move it).
- **due(frame)**, after the systems tick: the next shot's frame is reached, the plants and animals are loaded
  (`vegetationReady`, `animalsReady`; the wait for the terrain restarts after them), and the terrain and lighting are
  ready (`terrainReady`, `lightingReady`). A late shot logs a warning; a late sequence frame delays the ones after it.
  More than 1200 frames of waiting fails the run.
- **save(rgb, w, h)**: writes `<capture_dir>/<name>.png`, logs it, moves to the next shot.
- **Predicates** (free functions): `terrainReady` (no `ChunkC` without `ChunkGenerated` and no `ChunkMeshPending`, both
  ignoring `ChunkDestroying`), `vegetationReady` / `animalsReady` (no species loading; true without the statistics),
  `lightingReady` (`LightingStats::settled()`; true without ray tracing).

## Parameters

The script, `{ "scene", "vars", "hide_ui", "shots": [{ "frame", "name", "camera": { "pos", "look_at", "offset", "yaw",
"pitch", "above" } }], "sequence": { "start", "count", "every" (1), "name" ("frame") }, "input": [{ "frame", "key",
"button", "down" (true), "mouse", "motion" }] }`, and the run arguments `capture`, `capture_dir`, `exit_after_capture`.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Camera](../components/Camera.md) | context | readwrite | beginFrame | spawn camera remembered on the first frame; the shot's pos / offset / above / yaw / pitch / look_at written |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | beginFrame | the TerrainField of "above" shots (a default generator without it) |
| [ChunkC](../components/ChunkC.md) | component | read | due (terrainReady) | chunks not yet generated delay the shot |
| [ChunkGenerated](../components/ChunkGenerated.md) | tag | read | due (terrainReady) | excluded from the wait |
| [ChunkMeshPending](../components/ChunkMeshPending.md) | tag | read | due (terrainReady) | chunks waiting for a mesh delay the shot |
| [ChunkDestroying](../components/ChunkDestroying.md) | tag | read | due (terrainReady) | excluded from the wait |
| [VegetationStats](../components/VegetationStats.md) | context | read | due (vegetationReady) | species_loading |
| [AnimalStats](../components/AnimalStats.md) | context | read | due (animalsReady) | species_loading |
| [LightingStats](../components/LightingStats.md) | context | read | due (lightingReady) | settled() |

## Side effects

- Reads the capture script (constructor; Game::start also reads its scene and vars with readScene).
- Pushes SDL key, mouse button and motion events (sendInput) that InputSys reads the same frame; sets ImGui's MousePos; with hide_ui sets ImGui's style alpha to 0.
- Creates capture_dir and writes <capture_dir>/<shot>.png (stb_image_write) from the renderer's readback; logs each shot and late shots.
- Fails the run (Game::start returns 1) when a shot waits more than 1200 frames for the terrain or a PNG cannot be written; a late sequence frame delays the ones after it.

## Order and dependencies

- `beginFrame` runs after `renderer->prepare()` and before `pre_tick`: its camera is overwritten by any system that
  places the camera (that is why video scenes use `sequence`, which does not move it). Its input events reach InputSys
  the same frame.
- `due` runs after `post_tick`; the renderer is asked for the frame (`requestCapture`) and `save` after `render()`.
- The statistics it waits on are published by the renderer a frame late.

## Used in scenes

<!-- generated -->

## Tests

GPU (CMake, label `gpu`): `V0.1`, `V0.2`, `V1.1`, `V2.2` (base captures), `V2.1` (test_boxes_view), `P1 check: scripted
input ...`, `P2 check: scripted clicks pause and single-step WorldStepper`; scripts in `tests/capture/`.
