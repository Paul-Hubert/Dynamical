# AnimalEpisode

`AnimalEpisodeSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `AnimalEpisode@observe`, last of the observe phase · Parameters: no

Tells the trainer whether the episode ended with this observation (`AgentIO::done`, `truncated`), closes the episode
record (kept for the eval), and schedules the reset for the next observe phase.

## Behaviour

- **tick** (last tick of each window): returns when the bot is not built.
  - A fresh episode (reset this tick or in init): `fresh = false`, done and truncated false, return.
  - A recording ends its walk after `recording_decisions` (300) recorded decisions: `truncated = true`.
  - `io.done = killed || truncated`, `io.truncated = truncated && !killed`.
  - When done: the episode record gets its ticks, whether it fell, the cause and the segment, the heading error
    averaged over the decisions; with `view_truncate` it is appended to `bot.episodes` (read and cleared by
    `TrainHost::evalAnimals`); `pending_reset = true` and the world is **parked** (no `Every` / `Act` system ticks
    until `AnimalRewards` resets it at the next observe phase).

## Parameters

None (`AnimalSpawn::recording_decisions`, `view_truncate`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick | `fresh`, `truncated`, `episode`, `episodes`, `pending_reset` |
| [AgentIO](../components/AgentIO.md) | context | write | tick | `done`, `truncated` |
| [Parked](../components/Parked.md) | context | write | tick | parks the world at the end of an episode |

## Side effects

- None beyond the registry.

## Order and dependencies

- Last of the observe phase, after `AnimalRewards` and `AnimalObservation`.

## Used in scenes

<!-- generated -->
- [`common/animal_demo_terrain_world.json`](../../../resources/scenes/common/animal_demo_terrain_world.json): `fixed@observe`
- [`common/animal_demo_world.json`](../../../resources/scenes/common/animal_demo_world.json): `fixed@observe`
- [`common/animal_orders_world.json`](../../../resources/scenes/common/animal_orders_world.json): `fixed@observe`
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed@observe`
- [`common/animal_world.json`](../../../resources/scenes/common/animal_world.json): `fixed@observe`

## Tests

- `AA.4`, `AA.5`, `AA.6` (recorded walks of `episode_decisions`), `AA.8`, in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
