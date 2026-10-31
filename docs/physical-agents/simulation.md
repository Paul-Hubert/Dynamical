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
void fixed_add(std::unique_ptr<System>, Phase);
void fixed_tick(double h, bool act_tick, bool observe_tick);
```

`preinit`, `init` and `finish` include the fixed list. `pre_add<T>()` and `post_add<T>()` stay for
master's systems. The scene loader uses an untyped `add(list, unique_ptr<System>)`.

## Per-tick schedule

Unity runs Academy and agent updates in an undefined order (UA §3.1). Here the order is explicit, and the world template lists these systems in this order:

| # | System | Phase | Does |
|---|---|---|---|
| 1 | `TrainerActions` / `Policy` / `TrackActions` | Act | Applies the action received for this window: targets and gains ([agent-runtime.md](agent-runtime.md#actions-8--15)). `Policy` runs ONNX first. `TrackActions` is the kinematic player |
| 2 | `DriveTargets` | Every | Writes the held drive targets into the Jolt motors; the target is rewritten every tick |
| 3 | `Disturbances` | Every | Push impulses and pull constraints, randomisation events |
| 4 | `PhysicsStep` | Every | `PhysicsWorld::step(h)`. Collision sub-steps; constraint impulses are read per sub-step by the energy recorder |
| 5 | `BodySync` | Every | Segment poses and velocities, **finite-difference anchor velocities**, joint angles |
| 6 | `Energy` | Every | Exact motor work from lambdas, plus whole-body d(KE + PE) as a cross-check |
| 7 | `Contacts` | Every | Drains the contact recorder into per-segment contacts |
| 8 | `Stats` | Every | Accumulates per-episode stats |
| 9 | `Termination` | Every | Kill and truncation. A terminal agent **snapshots its terminal observation and reward now** and parks (below) |
| 10 | `Observation` | Observe | Builds the stacked observation for the window that just ended |
| 11 | `Episode` | Observe | Final rewards for the window; writes `AgentStep`s to the `Outbox`; resets done agents |

**Decided differences from Unity:**
- the decision phase is per episode;
- terminal-window rewards are counted;
- the order is fixed.

The undefined same-order `FixedUpdate`s of Unity are not reproduced.

### Windows, lockstep and early termination

- **Lockstep.** The trainer exchange is lockstep: every agent in a process decides on the same exchange.
  Windows are therefore global, `decision_period` ticks long, and every episode starts on a window
  boundary. Unity also uses one global decision phase (UA §3.1); here every episode additionally starts on a boundary.
- **A kill mid-window** (Unity kills on the first dying tick):
  1. `Termination` snapshots the terminal observation and reward at that tick.
  2. The agent's world **stops stepping** for the rest of the window. Training has one agent per
     world, so nothing else in that world needs to advance.
  3. At the window end, `Episode` sends the terminal step and resets the agent.

  The episode thus ends exactly at the kill tick, as in Unity, without breaking lockstep.
- **Several agents per world** (later lines, duels): park the dead agent's bodies kinematic, or end
  the group episode. This is decided per line in M9.
- **Truncation** (`episode_s`, 33.3 s = 2000 ticks at 60 Hz, a multiple of the decision period 2) is
  checked on window boundaries. The params loader rejects an episode length that is not a whole number
  of windows.

## Training host

`dynamical_train` has no frame loop. `TrainHost::run`:

```
handshake (seed, num_areas)                     -> create N worlds (N = num_worlds; num_areas is ignored, see bridge)
loop:
    cmd = exchange(output)                      // blocking gRPC; output = gathered Outboxes
    if cmd == QUIT: break
    if cmd == RESET: reset every agent, build observations without stepping, continue
    scatter actions into each agent's ActionBuffer
    taskflow for_each world: for k in 0..period-1: world.systems.fixed_tick(h, k == 0, k == period-1)
    gather Outboxes in world index order        // deterministic wire order
```

- Runs as fast as the CPU allows. The engine-configuration `time_scale` and frame rate are accepted
  and ignored.
- `mode=bench`: random actions, no bridge, prints ticks/s per world and in total.
- `mode=null`: no worlds; fixed observations for 25 agents (M0).
- `mode=eval` and `mode=record-demo`: [inference-and-replay.md](inference-and-replay.md).

## Real-time stepping (`dynamical`)

`WorldStepper` (host pre system) runs the same `fixed_tick` from a wall-clock accumulator:
- `accumulator += min(frame_dt, 0.25)`, at most 8 ticks per frame;
- a pause flag and single-step from the panel;
- `alpha = accumulator / h` stored for `WorldView` interpolation.

`base` keeps master's `PhysicsSys` accumulator unchanged; only agent worlds use `WorldStepper`.

## Determinism

- Within a world: single-threaded Jolt, fixed creation order of bodies and constraints, seeded
  `WorldRng`, no wall clock.
- Across worlds: independent, so thread scheduling does not matter.
- Test: 1000 ticks with seeded random actions, twice, hashing every segment state; the hashes must be
  equal. Across platforms only if Jolt is built cross-platform deterministic (see [physics.md](physics.md#jolt-build)).
