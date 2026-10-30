# Inference (ONNX Runtime)

Trained policies are exported by ML-Agents as ONNX (at `stop-run`). The engine runs them for:

- **Eval mode** (`dynamical_train --mode eval`): N episodes per arena, deterministic actions, stats to
  JSON — replaces Unity eval builds.
- **Viewer and game**: physical agents in `dynamical` driven by a policy (M8, M9).

## Library: `dynamical_onnx`

```cpp
// src/inference/policy_runner.h
class PolicyRunner {
public:
    PolicyRunner(const std::filesystem::path& onnx, const BehaviorSpec& expected, int intra_threads = 1);
    // obs[i] is a [batch, size_i] row-major block for observation i; returns [batch, num_continuous]
    void run(std::span<const std::span<const float>> obs, int batch, std::span<float> actions_out,
             bool deterministic = true);
    const ModelInfo& info() const;    // version_number, memory_size, input/output names and shapes
};
```

- One `Ort::Env` per process, one `Ort::Session` per model, CPU execution provider,
  `SetIntraOpNumThreads(intra_threads)`, graph optimisation `ORT_ENABLE_ALL`.
- Load-time validation against the scenario's `BehaviorSpec`: observation input count and sizes, number
  of continuous actions. Refuse to run on mismatch (a policy is bound to its body and observation layout).
- **Batch** every agent of a behaviour into one `run` call per decision tick; per-agent calls waste most
  of the time in overhead.

## Model I/O (*verify* against the fork's `model_serialization.py`)

| Name | Direction | Shape | Notes |
|---|---|---|---|
| `obs_0`, `obs_1`, … | input | `[batch, size]` | one per observation spec, same order as on the wire |
| `action_masks` | input | `[batch, sum(branches)]` | discrete only |
| `continuous_actions` | output | `[batch, n]` | stochastic sample, clipped/scaled to `[-1,1]` |
| `deterministic_continuous_actions` | output | `[batch, n]` | mean action; use for eval and in-game |
| `version_number`, `memory_size`, `continuous_action_output_shape` | output | constants | read once at load |
| `recurrent_in` / `recurrent_out` | in/out | `[batch, 1, memory_size]` | only for LSTM policies; v1 does not support them and refuses `memory_size > 0` |

If the fork exports observation normalisation inside the graph (it does when `normalize: true`), no
extra preprocessing is needed in C++.

## Systems

- `PolicySys` (fixed list, decision ticks only, runs before `ActionApplySys` of the next tick): for each
  behaviour with a loaded model, gather `ObservationBuffer`s of non-`Heuristic` agents, run, write
  `ActionBuffer` with `fresh = true`.
- In eval mode `PolicySys` replaces the bridge; the host loop is the training loop without gRPC.

## Cost

An MLP of a few hundred units costs on the order of tens of microseconds per agent per decision on one
CPU core when batched. At 30 Hz decisions this is negligible next to physics for dozens of agents; the
game's cap on physical agents ([game-integration.md](game-integration.md#cost-and-lod)) is set by physics,
not inference. Measure in M8 with Optick (`OPTICK_EVENT("PolicySys::run")`).
