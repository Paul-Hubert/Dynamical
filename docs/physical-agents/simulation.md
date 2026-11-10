# Simulation

## Worlds

```cpp
// src/scene/world.h
struct World {
    uint32_t index;                  // 0..N-1, stable, determines RNG stream and wire order
    std::string template_name;
    entt::registry reg;
    SystemSet systems;               // bound to reg; fixed lane only for agent worlds
};
// world ctx: PhysicsWorld, WorldClock, WorldRng, Outbox, const SceneVars*, shared_ptr<const BodyDef>, shared_ptr<const AgentParams>
struct WorldClock { double dt; uint32_t decision_period; uint64_t tick = 0; };
struct WorldRng  { std::mt19937_64 engine; };   // seeded hash(base_seed, index)
```

**Why one registry per world.**
- Episode resets create and destroy entities, and EnTT registries are not thread-safe for that.
- With separate registries, worlds step in parallel with no locks, and the master pattern
  `System(entt::registry&)` works unchanged.
- Each world also has its own Jolt `PhysicsSystem`. Worlds never interact, and stay deterministic on
  their own.

This replaces Unity's single PhysX scene holding 25 arenas 350 m apart under collision filtering
(UA §9.1).

## Fixed lane

`SystemSet` gains a third list and phase masks:

```cpp
enum class Phase : uint8_t { Every, Act, Observe };    // Act: first tick of a window; Observe: last tick
void add(List, std::unique_ptr<System>, Phase = Phase::Every);   // untyped, used by the scene loader
void fixed_tick(double h, bool act_tick, bool observe_tick);      // runs Every + the matching phase systems
void fixed_observe_only();                                         // runs only Observe systems (RESET path)
```

- `preinit`, `init` and `finish` include the fixed list.
- `pre_add<T>()` and `post_add<T>()` stay for master's systems.
- **Parking.** When the world ctx flag `Parked` is set, `fixed_tick` skips `Every` and `Act` systems.
  It still runs `Observe` systems on the window's last tick, and those see the snapshot.

## Per-tick schedule

Unity runs Academy and agent updates in an undefined order (UA §3.1). Here the order is explicit, and
the world template lists these systems in this order (names: [scenes.md](scenes.md#canonical-system-names)):

| # | System | Phase | Does |
|---|---|---|---|
| 1 | `TrainerActions` / `Policy` / `HeuristicActions` | Act | Applies the window's action: targets and gains ([agent-runtime.md](agent-runtime.md#actions-8--15)). `Policy` runs ONNX first |
| 1′ | `TrackActions` | Every | Kinematic pose-track playback (`rig_anim`, demos), instead of 1–3 |
| 2 | `DriveTargets` | Every | Writes the held drive targets into the Jolt motors |
| 3 | `Disturbances` | Every | Push impulses, pull constraints |
| 4 | `PhysicsStep` | Every | **n sub-steps of `Update(h/n, 1)`**. After each sub-step it calls the contact recorder (patch P-C) and the energy recorder (joint lambdas). The lambdas hold only the last step, so reading them later would lose sub-steps |
| 5 | `BodySync` | Every | Segment states, finite-difference anchor velocities, joint angles |
| 6 | `Contacts` | Every | Aggregates the tick's contact records per segment and foot; centre of pressure |
| 7 | `Energy` | Every | Aggregates the tick's work per joint; whole-body E, dE, closure residual |
| 8 | `Task` | Every | Target re-placement (`arc`), speed bookkeeping |
| 9 | `Stats` | Every | Accumulates per-episode stats |
| 10 | `Termination` | Every | Kill and truncation. On a kill it **snapshots the terminal observation and reward** and sets `Parked` |
| 11 | `Rewards` | Observe | Window reward terms, against the decision snapshot of the window that ended (Spine anchor, `tsv`, target speed) |
| 12 | `Observation` | Observe | Refreshes the snapshot to the live target, then pushes the observation, as Unity's `CalculateRewards` → `UpdateObjective` → observe. It does nothing while parked: the snapshot is used |
| 13 | `Episode` | Observe | Writes `AgentStep`s to the `Outbox`; resets done agents and clears `Parked` |

**Decided differences from Unity:**
- terminal-window rewards are counted;
- the order within a tick is fixed;
- every episode starts on a window boundary.

The decision phase is global, as in Unity.

### Windows, lockstep and early termination

- **Lockstep.** Every agent in a process decides on the same exchange, so windows are global and
  `decision_period` ticks long (`physics_hz / control_hz`: 2 at 60 Hz).
- **A kill mid-window** (Unity kills on the first dying tick):
  1. `Termination` snapshots and parks.
  2. The world skips physics for the rest of the window. Training has one agent per world.
  3. At the window end, `Episode` sends the terminal step and resets.

  The episode ends at the kill tick, as in Unity, without breaking lockstep.
- **Several agents per world** (M9 lines): park the dead agent's bodies kinematic, or end the group
  episode. Decided per line.
- **Truncation.** `max_frames_60hz` = 2000 (Unity's `maxFrames` at 60 Hz) gives
  `episode_ticks = 2000 · physics_hz / 60`. The loader requires it to be a whole number of windows, and
  truncation is checked on window boundaries.

## Training host

`dynamical_train` has no frame loop. `TrainHost::run`:

```
read -set / -envParams / params        -> physicsRate and every other pre-world value
handshake (seed, num_areas)            -> base_seed = seed (the trainer already adds worker_id); create N worlds
loop:
    cmd = exchange(output)             // blocking gRPC with a 1 s deadline, retried, so the CTRL_C flag is seen
    if ctrl_c flag or cmd == QUIT: break (exit 0)
    if cmd == RESET: reset every agent; for each world fixed_observe_only(); continue
    scatter actions into ActionBuffers (by AgentId)
    taskflow for_each world: for k in 0..period-1: fixed_tick(h, k == 0, k == period-1)
    gather Outboxes in world index order
```

- `num_areas` is ignored; `num_worlds` decides. Both are logged.
- **`physicsRate` is fixed before the worlds exist.** It is read from `-set:`, `-envParams=` or the
  params file. A live value differing from it is warned about and ignored (Unity also reads it once,
  at Awake).
- Runs as fast as the CPU allows. Engine-configuration `time_scale` and frame rate are accepted and
  ignored.
- **Modes:**
  - `mode=bench`: random actions, no bridge, ticks/s.
  - `mode=null`: the `null` scene, fixed observations for 25 agents (M0).
  - `mode=eval` and `mode=record-demo`: [inference-and-replay.md](inference-and-replay.md).

## Real-time stepping (`dynamical`)

`WorldStepper` (host pre) runs the same `fixed_tick` from the frame clock:
- `accumulator += min(frame_dt, 0.25)`, at most 8 ticks per frame;
- pause and single-step from the panel;
- `alpha = accumulator / h` for `WorldView`.

Under `capture` the frame clock is `fixed_frame_dt`, so captures are reproducible. `base` keeps
master's `PhysicsSys` accumulator.

## Determinism

- **Within a world:** single-threaded Jolt, fixed creation order, seeded `WorldRng`, no wall clock.
- **`WorldRng`** wraps `std::mt19937_64` with its own uniform, normal and unit-sphere functions.
  `std::*_distribution` output differs between MSVC and libstdc++.
- **Across worlds:** independent, so thread scheduling does not matter.
- **Test:** 1000 ticks with seeded random actions, twice, with 1 and with 8 threads, hashing every
  segment state; the hashes are equal. Across platforms only with a cross-platform-deterministic Jolt
  build ([physics.md](physics.md#jolt-build)).
