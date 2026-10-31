# Inference, viewer, eval, demos, replay

## ONNX Runtime (`dynamical_onnx`)

```cpp
class PolicyRunner {
public:
    PolicyRunner(const std::filesystem::path& onnx, const BehaviorSpec& expected, int intra_threads = 1);
    void run(std::span<const float> obs, int batch, std::span<float> actions, bool deterministic = true);
    const ModelInfo& info() const;   // version, memory size, input/output names and shapes, file mtime
};
```

- One `Ort::Env` per process; CPU provider; `ORT_ENABLE_ALL`.
- **Validate at load** against the scene's layout: input sizes and continuous action count. Refuse on
  mismatch, because a policy is bound to its body and layout.
- The fork's model I/O names (`obs_0`…, `continuous_actions`, `deterministic_continuous_actions`,
  `version_number`, `memory_size`) are *verify*. Recurrent policies (`memory_size > 0`) are refused in
  v1.
- Apply the trainer's `clamp(−3, 3)/3` to the actions if the exported graph does not already include
  it (*verify*; Unity applies no clipping of its own, UA §2.4).
- **Ownership.** Worlds cannot see the host registry, so the host system `ModelWatcher` (view) or
  `TrainHost` (eval) loads the session and injects a `PolicyHandle` (`std::shared_ptr<PolicyRunner>`)
  into each world's ctx. A swap replaces the handle; the world adopts it at its next episode start.
- `Policy` (world, Act phase) batches every agent of the world into one `run`, through the handle.
  Registry name `Policy`; the class is `PolicySys`.

## `walker_view`

`dynamical scene=walker_view var.run=<run-id>` (or `var.policy=<file.onnx>`).

- **`latest:<dir>/<behavior>` model source** (`ModelWatcher`, host system):
  - polls the directory every second for the newest `<behavior>-<step>.onnx` by step number, falling
    back to mtime;
  - loads the session on a Taskflow task;
  - swaps it in at the next episode start of each world;
  - shows the checkpoint name and step in the panel.

  The swap avoids loading half-written files by waiting for two equal size and mtime reads.
- **Worlds.** Default 1, or `num_worlds` side by side. Same `humanbot_world` template as training, with
  `Policy` replacing `TrainerActions`. Randomisation is off by default: the scene variable `randomize` (bool, default false) feeds the params
  override `randomize`.
  The target is controllable from the panel (click to place, speed slider).
- **Panels** (`AgentPanel`, ImGui/ImPlot):
  - joints: angle against limits, target against actual, motor torque, saturation;
  - reward terms over the episode;
  - energy: power per joint, positive and negative work, cost of transport;
  - observation vector with names;
  - physics: ticks/s, step ms, contacts;
  - buttons: pause, step, reset, push.
- It runs next to a training process and shares only files, so viewing cannot slow or crash
  training.

## Eval mode

**Minimal eval (P11):** `dynamical_train scene=walker_eval mode=eval var.policy=<onnx> episodes=<n>`
replaces the bridge with `Policy` and
deterministic actions.

- It emits **the same CSV columns and `EVT|{json}` stdout lines** as Unity's `energy-eval` and
  `gait-cycle` builds, so `energy_eval_run.py` and `gait_run.py` run unchanged.
- It applies env params from `-envParams=`. Unity's `record-run.ps1` forgets this today.
- **Full eval (P14):** the same CSV columns and `EVT|` lines as Unity's builds, plus replays.

## Demo recorder

`dynamical_train scene=walker_demo mode=record-demo var.track=<track> var.decisions=<n>` (UA §6, §9.2). Defaults per line: flat walker 3000 decisions, DeVita 10000.

1. Builds the world with a **kinematic**, non-colliding body. `TrackActions` plays the pose track
   with `MoveKinematic`, the same playback as [`rig_anim`](rig-animation.md). Unity's pose lands one
   tick late; `MoveKinematic` targets the frame at the end of the step, a documented 1-tick difference.
2. **Limits are not enforced**: kinematic bodies follow the clip even past the stops, as in Unity.
   `rig_anim` reports by how much.
3. Runs the same `Observation` system as training with `Recording` set:
   - header x,z = 0 (`speedDiffZeroRecording`);
   - header y = head error;
   - joint velocities from the anchor finite differences.

   Episodes are 1000 ticks; randomisation is off; there is no death.
4. Writes a stock `.demo` (*verify* against the fork's `demo_loader.py`):
   - `DemonstrationMetaProto` in the first 32 bytes;
   - `BrainParametersProto` at offset 33;
   - then length-delimited `AgentInfoActionPairProto` records.

   The actions are zeros of the layout size.
5. **Cache.** The file is keyed on (track hash, body hash, layout version, and the demo-relevant params
   subset from [assets.md](assets.md#demo-cache-key)) under
   `Demonstrations/`. `start-run` reuses a cached demo instead of re-recording, which makes demo bytes
   a controlled variable.

## Replays

- **Writer.** The run argument `replay=<dir>` (eval and training) writes per episode:
  - a JSON header (body hash, layout, params, policy, seed);
  - per tick, float32 segment poses;
  - per decision, actions, reward terms, contacts and energy channels.
- **Players.**
  - `walker_view var.replay=<file>`: in-engine, with a scrubber and synced plots.
  - Unity's `ReplayPlayer` scene: the skinned HeidelBot, recorded with the existing FFmpeg recorder,
    for publication videos.

  The format is documented in `docs/physical-agents/replay-format.md`, written at M8, so both
  readers stay in sync.
