# Game integration (M9)

The goal: the colony game's `human` entities can be physical walkers driven by a trained commanded
policy, while the existing AI (and later the LLM planner of `llm-ai-system.md`) keeps deciding *what* to
do. Planner → controller → physics:

| Layer | Today | With physical walkers |
|---|---|---|
| Planner | `AISys` picks a `Behavior` by score; actions chain (`WanderAction` emits a `Path`) | unchanged (later the LLM planner: "planner, not controller") |
| Locomotion | `PatherSys` moves `Position` along `Path` tiles at `terrain_speed` | `WalkerCommandSys` turns the `Path` into a `Command {direction, speed, facing}` |
| Controller | — | commanded walker policy (`PolicySys`, ONNX), the Unity `CommandedLowerAgent` line |
| Physics | — | Jolt `PhysicsWorld` in the game registry ctx |
| Map | `MapManager` owns `Position` | still owns it; the walker's root position is written back with `MapManager::move` |

## The time-scale problem

This has to be solved before any of the rest matters.

- `TimeSys` sets `time.dt = speed(60) × speed_modifier × frame_dt × 10`: at the slowest running speed one
  real second is **600 game seconds**; at the fastest (modifier 50) it is 30 000.
- `PatherSys` moves humans at `1/60` tile per game second, so about **10 tiles (10 m) per real second**
  at speed 1, and 500 m/s at the top speed.
- A physical walker covers about 1.3 m per **simulated** second. To keep pace with the game clock it
  would need 8–400× real-time physics per human. Not feasible beyond a handful of agents, and the
  policy was never trained for anything but 1× physics anyway.

Options, in recommended order:

1. **Real-time focus mode (recommended).** A new game speed "real time" where `Time::dt` equals the
   frame time (`speed × modifier × 10 = 1`). Physical walkers are active only in this mode and only
   inside a focus radius around the camera. At any faster speed every human is kinematic (today's
   `PatherSys`). Switching speed converts walkers: kinematic → physical spawns the ragdoll in a standing
   pose at the current `Position`; physical → kinematic snaps `Position` to the root and removes the
   ragdoll.
2. **Visual-only physics.** `PatherSys` stays authoritative at any speed; the ragdoll walks "on a
   treadmill" and is drawn at the kinematic position. Gives animation, not physical interaction, and
   breaks at speeds the walker cannot reach.
3. **Physical lines only where time is real.** Use ragdolls only for scenes that are real-time by
   nature (duels, strikes), entered as a mode.

The doc assumes option 1 below.

## Components and systems

```cpp
struct PhysicalWalker {            // on a human entity that currently has a ragdoll
    entt::entity root;             // the ArticulatedBody/Agent root (a separate entity, no Position)
};
struct WalkerCandidate {};         // human that may become physical when inside the focus radius
```

| System | List | Does |
|---|---|---|
| `WalkerLodSys` | pre | Each frame, in real-time mode: humans with `WalkerCandidate` inside the focus radius and under the cap get a ragdoll (`PhysicalWalker`); others lose it. Outside real-time mode: remove all |
| `WalkerCommandSys` | fixed (decision ticks) | For `Path` + `PhysicalWalker`: next waypoint from `path.tiles.back() + 0.5`, convert to sim frame, set `Command`; pop the waypoint within a radius of 0.3 m; remove `Path` when empty (same contract as `PatherSys`). Speed = target speed × `Tile::terrain_speed` |
| `PolicySys` | fixed | commanded walker policy, batched |
| physics + agent systems | fixed | as in training, minus `RewardSys`/`EpisodeSys` (no learning in the game). `TerminationSys` detects falls → `WalkerRecoverySys` |
| `WalkerRecoverySys` | fixed | On a fall: either a get-up policy (future line) or a fade to the rest pose at the current root |
| `WalkerMapSyncSys` | fixed | Root segment's horizontal position → `simToMap` → `MapManager::move(entity, xy)` when it changes tile or every N ticks; keeps tile occupancy, selection and pathfinding correct |

`PatherSys` changes its view to `reg.view<Path, Position>(entt::exclude<PhysicalWalker>)`, so
kinematic humans keep working unchanged.

The ragdoll root is its own entity (no `Position`, `MapManager` never sees it); the human entity keeps
`Position`, `AIC`, `BasicNeeds`, etc. `buildHuman` gains a `WalkerCandidate` tag; `destroyObject` also
destroys the ragdoll if present.

## Ground in the game

The map is the ground. Per chunk near physical walkers, build a static `HeightFieldShape` from tile
`level` (sim y) with per-tile-type friction, created on demand and destroyed when no walker is nearby.
Unwalkable tiles (`terrain_speed == 0`: stone, water) get no special collision in v1 — the pathfinder
already avoids them; walls and props come later. Map z-up → sim y-up through the same `simToMap`
reflection, inverted.

## Constraints

- **Same body, same physics.** A policy is bound to the body and physics settings it trained on:
  physical humans use the training body file and the same `physics_hz`, drive mode and solver settings.
  The game's `PhysicsWorld` reads them from the scenario file of the policy's training line.
- **Terrain must be in the training distribution.** A walker trained on flat boxes will fall on map
  slopes. Terrain training (heightfields, M9) precedes physical humans on generated maps; until then
  the focus mode should flatten or reject steep areas.
- **Commands must be in the training distribution.** Clamp `Command::speed` and turn rate to the
  ranges the commanded line was trained on.

## Cost and LOD

- Per physical human per simulated second: `physics_hz` Jolt steps of ~15–20 bodies and constraints,
  plus `physics_hz / decision_period` batched inference rows.
- Cap: `Settings::max_physical_walkers` (start at 16), nearest to the camera first. Measure in M9 with
  Optick and raise if the frame budget allows. Use `JobSystemThreadPool` in the game world.
- Hysteresis on the focus radius (enter at r, leave at 1.2 r) to avoid ragdoll churn.
