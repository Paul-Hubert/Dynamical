# Capture

> **Kind:** service · **Registry name:** — · **Scope:** n/a · **Lane:** per-frame call from `Game::start` (`beginFrame` before the pre lane, `due` / `save` after the post lane) · **Needs renderer:** yes
> **Source:** [`src/view/capture.h`](../../../src/view/capture.h), [`src/view/capture.cpp`](../../../src/view/capture.cpp)

## Purpose
Runs a capture script (`capture=<script.json>`): injects scripted keyboard / mouse input through SDL's event queue, moves the desktop [`Camera`](../components/Camera.md) to each shot, waits until the terrain is generated and meshed, and writes the rendered frame to `<capture_dir>/<name>.png`. Used for reproducible screenshots and video frame sequences.

## Lifecycle
- **Construction / params:** `Capture(reg, args)` with `directory = RunArgs::capture_dir` (default `captures`); parses `RunArgs::capture`:
  - `shots` (array, optional): `{ "frame": uint (required), "name": string (required), "camera": { "pos": [3], "look_at": [3], "offset": [3], "yaw": float, "pitch": float } (all optional) }`; frames must strictly increase.
  - `sequence` (optional): `{ "start": uint, "count": uint (required), "every": uint (1), "name": string ("frame") }`: `count` shots `<name>_00000` ... at `start + i * every`, no camera; `every >= 1` and `start` after the last shot.
  - At least one shot or sequence frame.
  - `input` (array, optional): `{ "frame": uint (required), "key": SDL scancode name, "button": "left" | "right" | "middle", "down": bool (true), "mouse": [x, y], "motion": [dx, dy] }`; unknown key names or buttons are errors.
  - `scene` and `vars` are read separately by `Capture::readScene` (Game).
  - Any error sets `error` (reported by Game, exit 1). Creates `directory` (errors ignored).
- **readScene (static):** returns false if the file cannot be read or parsed; `scene` = script `scene` or `base`; `vars` = each `vars` entry, strings as is, other JSON values dumped.
- **beginFrame(frame)** (before the systems tick):
  1. `sendInput(frame)`: for every input event of this frame, sets `ImGui::GetIO().MousePos` if `mouse` is given; pushes `SDL_KEYDOWN/UP` with the scancode, `SDL_MOUSEBUTTONDOWN/UP` (at `mouse` if given), `SDL_MOUSEMOTION` with `xrel/yrel`.
  2. On the first call, records the camera position / yaw / pitch as the spawn camera.
  3. If shots remain and the next shot has any camera field: `position = (pos or spawn) + (offset or 0)`, `yaw = yaw or spawn yaw`, `pitch = pitch or spawn pitch`; with `look_at`, pitch / yaw point at it.
- **due(frame)** (after the post lane): false when done, failed or `frame < shot.frame`; true when `terrainReady()` (warns if later than planned); after `max_wait = 1200` frames past the shot frame without terrain, sets `error`.
- **save(rgb, w, h):** writes the RGB8 image to `<directory>/<name>.png` with stb_image_write; on failure sets `error`; otherwise logs and advances to the next shot.
- **finish:** None (destroyed with Game::start's scope).

## Expected behavior
- A shot's camera is applied every frame from the frame after the previous shot was saved until it is saved. Capture writes before the pre lane, so systems that write the camera during the tick (PlayerControl movement, SphereView follow, DesktopRoller) can still change it in that frame.
- A shot without camera fields leaves the camera alone (scripted input or a scene system may drive it); sequences never move the camera.
- `terrainReady()` is true when no `ChunkC` lacks `ChunkGenerated` and no `ChunkMeshPending` exists (chunks being destroyed excluded); scenes without terrain are always ready.
- The shot is only taken when `Renderer::requestCapture()` also succeeds (Game).
- Scripted input is seen by [InputSys](InputSys.md) in the same frame (pushed before the pre lane). The scripted `mouse` position survives [UISys](UISys.md) only while the window is unfocused (hidden capture window).

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| [`Camera`](../components/Camera.md) | host | readwrite | tick | `beginFrame`: spawn recorded, shot pose written |
| [`ChunkC`](../components/ChunkC.md) | host | read | tick | `terrainReady()` |
| [`ChunkGenerated`](../components/ChunkGenerated.md) | host | read | tick | `terrainReady()` (exclude) |
| [`ChunkDestroying`](../components/ChunkDestroying.md) | host | read | tick | `terrainReady()` (exclude) |
| [`ChunkMeshPending`](../components/ChunkMeshPending.md) | host | read | tick | `terrainReady()` |

## Side effects
- Reads the script file; creates the capture directory; writes PNG files.
- Pushes synthetic SDL events (`SDL_PushEvent`); writes `ImGuiIO::MousePos`.
- Logs a warning when the terrain delays a shot and an info line per saved shot.

## Ordering and dependencies
- `Camera` must exist (PlayerControlSys in the scene) before the first `beginFrame`.
- Deterministic captures rely on `RunArgs::fixed_frame_dt` (default 1/60 when `capture=` is set) and `window_hidden`.

## Related
[Game](Game.md), [InputSys](InputSys.md), [UISys](UISys.md), [`docs/physical-agents/testing.md`](../../physical-agents/testing.md), [folder `src/view/`](../folders/src-view.md).
