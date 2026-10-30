# Rendering and tools

All of this lives in the `dynamical` executable (`src/logic/systems/agent_view/`); `dynamical_train`
never draws.

## What the renderer offers today

- One render pass (`ClassicRender`), triple-buffered (`NUM_FRAMES = 3`), transfer → compute → graphics
  semaphore chain.
- `MapUploadSys` fills persistently mapped per-frame buffers (`objectPointer`, `max_objects`) with
  `RenderObject { vec4 sphere; vec4 color; }`; `ObjectRenderSys` draws them as instanced camera-facing
  sphere impostors (`objectrender.vert/.frag`) with depth test.
- The camera is **orthographic** and tilted (`Camera::createProjection` uses `glm::ortho`; a
  perspective line is commented out), z-up, controlled by `CameraSys`.

## Body drawing, in three steps

### Step 1 (M1): sphere chains through the existing object path

`BodyRenderSys` (post list, before `MapUploadSys` finishes the frame) appends `RenderObject`s to the
same per-frame object buffer:

- For each capsule collider: `ceil(length / radius) + 1` spheres of the capsule's radius along its axis.
- Boxes: spheres at the corners and centre (debug only).
- Positions go through `simToMap` with the arena's draw origin
  ([conventions.md](conventions.md#sim--map-conversion-rendering-and-game-integration-only)).
- Colour by segment, or by a heat value (joint torque, contact, energy) chosen in the agent panel.

Needs only: a way for `BodyRenderSys` to append after `MapUploadSys` has written the map objects
(expose `num_objects` and the mapped pointer through `MapUploadData`), and a larger `max_objects`.
No new shaders, no new pipeline.

### Step 2 (M8): capsule impostor pipeline

A dedicated pipeline drawing capsules as ray-cast impostors (instance = two endpoints + radius + colour),
or instanced low-poly capsule meshes. Draw through the existing render pass and view descriptor set
(`ctx.classic_render.view_layout`). Add the commented-out perspective projection as a camera mode for
close-up agent viewing.

### Step 3 (later): skinned mesh

Requires the asset path from [body-and-assets.md](body-and-assets.md#later-gltf-instead-of-the-unity-bake):
one bone per segment, bone matrices = `simToMap(segment pose) × inverse bind`, uploaded per frame in a
storage buffer; vertex shader with 4 weights. Not needed for training or analysis.

### Interpolation

Physics runs at `physics_hz`, rendering at the display rate. `BodyRenderSys` draws
`lerp/slerp(prev, current, render_alpha)` from `SegmentState` ([simulation-loop.md](simulation-loop.md#game-loop-dynamical)).

## Agent viewer

`dynamical --viewer --scenario <s> [--model x.onnx] [--arenas N]` starts the game executable with a
flat empty map (or none), creates N arenas laid out on a grid with `arena_spacing`, and runs agents with
the policy, heuristics or a pose track. Arenas in the viewer are separate worlds exactly like training;
only drawing places them side by side.

## Replay

- **Recording:** `ReplayRecorder` (usable from `dynamical_train --record-replay` and the viewer) writes
  per decision tick: all `SegmentState`s, actions, reward terms, contacts and energy channels, into a
  chunked binary file (`.dyrep`, header JSON + float32 frames, same layout idea as pose tracks).
- **Playback:** `ReplaySys` (viewer) writes recorded `SegmentState`s instead of running physics;
  timeline scrubber, speed control, frame stepping in an ImGui window; ImPlot graphs stay synced to the
  playhead.
- Eval runs record replays of the best/worst/random episodes so videos never need a re-run.

## Video capture

The renderer only presents to the swapchain today. Capture needs:

1. An offscreen colour + depth target with the same format as the swapchain image, and a
   `ClassicRender` variant (or flag) that renders into it.
2. A copy to a host-visible buffer per frame (`vkCmdCopyImageToBuffer`), read after the frame fence.
3. Encoding: pipe raw RGBA frames to an `ffmpeg` child process
   (`ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 60 -i - -c:v libx264 out.mp4`); no codec linked into
   the engine.
4. Fixed-rate playback during capture: one replay frame per rendered frame, independent of wall time.

`dynamical --capture replay.dyrep --out video.mp4 --camera follow` renders a replay to a file. It still
needs a window/surface today; a true headless (no-surface) capture path is a follow-up and should not
block M8.

## ImGui / ImPlot panels (game exe, `AgentPanelSys`)

| Panel | Content |
|---|---|
| Agents | list of agent slots per arena: id, behaviour, episode step, reward so far, done reason |
| Joints | per joint: angle vs limits (bar), target vs actual, applied torque, saturation flag |
| Rewards | per-term reward, stacked ImPlot over the episode |
| Energy | power per joint, cumulative positive/negative work, cost of transport |
| Observations | the raw observation vector with names from the layout; diff against a loaded Unity golden file |
| Physics | ticks/s, Jolt step time, contacts per tick, bodies; buttons: pause, step one tick, reset agent, push agent |

These read components only, so they work identically in live simulation and replay.
`DevMenuSys` gets an entry to toggle the panels.
