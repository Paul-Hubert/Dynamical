# Simulation loop

## Today

`Game::start` runs one variable-step loop: `renderer->prepare()` → `set->pre_tick(dt)` →
`set->post_tick(dt)` → `renderer->render()`, with `dt` from the wall clock. Every system gets the same
`dt`. That stays for the game's existing systems; physics gets its own fixed-step lane.

## `SystemSet` changes

Add a third list, stepped with a constant `h`:

```cpp
// src/logic/systems/system_set.h (additions)
template<typename T, typename... Args, typename std::enable_if<std::is_base_of<System, T>::value>::type* = nullptr>
T& fixed_add(Args&&... args) {
    auto sys = std::make_unique<T>(reg, std::forward<Args>(args)...);
    T& ref = *sys;
    fixed_systems.push_back(std::move(sys));
    return ref;
}
void fixed_tick(double h);     // ticks every fixed system once, in insertion order
std::vector<std::unique_ptr<System>> fixed_systems;
```

`preinit`, `init` and `finish` also iterate `fixed_systems`. `fixed_add` forwards extra constructor
arguments because agent systems need a `ScenarioDef&` or `BodyDef&`; plain `pre_add<T>()` stays as is.

## Fixed tick order (one physics tick, one arena)

| # | System | Runs | Does |
|---|---|---|---|
| 1 | `ActionApplySys` | every tick | On the first tick after a decision, copies the newly received action into each agent's `ActionBuffer` (applying the Unity plan's action mapping). Every tick: turns the buffer into joint targets, including any per-tick smoothing the Unity plan specifies |
| 2 | `JointDriveSys` | every tick | Computes drive torques (or sets Jolt motor targets) from targets and current joint state; records applied torque for energy |
| 3 | `PhysicsStepSys` | every tick | `PhysicsSystem::Update(h, collision_steps, temp_alloc, job_system)` |
| 4 | `BodySyncSys` | every tick | Copies Jolt pose/velocity of every segment into `SegmentState` (sim frame), computes joint angles/velocities into `JointState` |
| 5 | `ContactSys` | every tick | Drains the contact listener queue into `SegmentContacts` (foot contacts, illegal contacts) |
| 6 | `EnergySys` | every tick | Integrates the exact energy channels from torque × relative angular velocity |
| 7 | `RewardSys` | every tick | Adds per-tick reward terms into `RewardAccumulator` |
| 8 | `TerminationSys` | every tick | Sets `Episode::terminated` / `Episode::truncated` (falls, illegal contacts, max steps) |
| 9 | `ObservationSys` | decision ticks | Writes each agent's observation vector into `ObservationBuffer` |
| 10 | `EpisodeSys` | decision ticks | Publishes `AgentInfo` to the arena's outbox, then resets terminated agents (randomisation) |
| 11 | `StatsSys` | decision ticks | Pushes per-episode stats to the outbox for the Stats side channel |

A **decision tick** is the last tick of each `decision_period` window
(`(clock.tick + 1) % decision_period == 0`). Match the Unity plan's tick order exactly where it differs
(e.g. if Unity computes some reward terms only at decision time, put them in step 9–10, not 7); parity
tests (M3) will catch mismatches.

### Matching Unity's `DecisionRequester`

Unity's `DecisionRequester` with `TakeActionsBetweenDecisions = true` repeats the last action on every
academy step (FixedUpdate) between decisions; the decision is requested on steps where
`StepCount % DecisionPeriod == 0` (offset by `DecisionStep`). Our equivalent: `ActionApplySys` applies
the held action every tick; the bridge exchanges once per `decision_period` ticks. If the Unity scenario
uses a non-zero decision offset, add `ScenarioDef::decision_offset`.

## Headless host (`dynamical_train`)

```cpp
// src/train/train_host.cpp (sketch)
int TrainHost::run() {
    MlAgentsBridge& bridge = host.ctx<MlAgentsBridge>();
    ArenaSet& arenas       = host.ctx<ArenaSet>();
    tf::Executor& ex       = host.ctx<tf::Executor>();

    bridge.handshake();                              // init exchange, learns num_areas, seed
    arenas.create(bridge.num_areas(), scenario, body, bridge.seed());

    for (;;) {
        Command cmd = bridge.receive_command();      // actions + RESET / STEP / QUIT, plus side channels
        if (cmd.type == Command::Quit) break;
        if (cmd.type == Command::Reset) arenas.reset_all();
        else bridge.scatter_actions(arenas);

        arenas.step_parallel(ex, scenario.decision_period, scenario.h());   // decision_period ticks each

        bridge.gather_and_send(arenas);              // AgentInfos + stats + new brain params
    }
    return 0;
}
```

- No frame pacing, no sleep: the loop runs as fast as the CPU allows. The Engine Configuration
  channel's `time_scale` and `target_frame_rate` are accepted and ignored (logged once).
- `ArenaSet::step_parallel` is one `tf::Taskflow` with one task per arena; each task runs
  `decision_period` × `arena.fixed->fixed_tick(h)`. Arenas only synchronise at the decision boundary,
  which is exactly when the bridge needs them.
- `RESET` (sent by Python on `env.reset()`): reset every agent in every arena (new episode, new
  randomisation), then send observations without stepping physics — Python expects an observation
  right after reset.

Exact message ordering is in [mlagents-bridge.md](mlagents-bridge.md#message-sequence).

### Modes of `dynamical_train`

| Mode flag | Loop | Output |
|---|---|---|
| `--mode train` (default) | as above | gRPC to trainer |
| `--mode eval --model x.onnx` | `PolicyRunner` replaces the bridge, N episodes | JSON stats file |
| `--mode record-demo --tracks dir/` | kinematic playback, no policy | `.demo` files ([demo-recorder.md](demo-recorder.md)) |
| `--mode null-env` (M0) | no physics; random observations, zero reward | gRPC; measures trainer throughput |
| `--mode bench` | random actions, no bridge | ticks/s per arena, printed and to Optick |

## Game loop (`dynamical`)

Add an accumulator so fixed systems run at `h` regardless of frame rate:

```cpp
// src/logic/game.cpp, inside the loop, after set->pre_tick(dt)
accumulator += std::min(dt, 0.25);                  // clamp to avoid the spiral of death
int steps = 0;
while (accumulator >= h && steps < max_steps_per_frame) {
    set->fixed_tick(h);
    accumulator -= h;
    ++steps;
}
render_alpha = accumulator / h;                      // for interpolating body poses when drawing
set->post_tick(dt);
```

- `h` and `max_steps_per_frame` come from `Settings` (`physics_hz`, default 120; max 8 steps).
- Fixed systems in the game are the same physics/agent systems, bound to the game registry, plus a
  `PolicySys` that runs the ONNX policy on decision ticks ([inference.md](inference.md)).
- `BodyRenderSys` interpolates between the previous and current `SegmentState` with `render_alpha`.

## Pausing and replay

- Training: never pauses.
- Game and viewer: pausing stops `fixed_tick`; a "step one tick" DevMenu button calls it once.
- Replay ([rendering-and-tools.md](rendering-and-tools.md#replay)) bypasses physics entirely and writes
  recorded `SegmentState`s.
