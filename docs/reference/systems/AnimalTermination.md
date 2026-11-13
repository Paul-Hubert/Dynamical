# AnimalTermination

`AnimalTerminationSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `AnimalTask`, before the observe phase · Parameters: no

Ends the physical animal's episode: by a fall (killed, the world parked until the end of the window) or by time or a
lost route (truncated).

## Behaviour

- **tick**: returns when the bot is not built or is kinematic.
  - Unless `bot.immortal` (the standing test): `bot.fallen(&segment)`: 1 when the trunk's origin is under
    `fall_height` (0.55) of its standing height over the ground, 2 when a segment that may not touch (neither a foot
    nor a lower leg; nor, with orders, the neck and head) has its lowest point within `touch_margin` (0.004) heights
    of the ground. With a cause and `die_on_fall`: `killed = true`, `fall_cause`, `fall_segment` kept, the world
    **parked** (`Parked::value = true`: the `Every` and `Act` systems stop until the next observe phase), return.
  - `truncated = true` when `bot.tick >= max_frames_60hz x (1 / dt) / 60` (2000 frames at 60 Hz, 33.3 s; never in a
    view without `view_truncate`) or when the route is lost.
- The ground is the bot's ground field, or y = 0 on the flat ground.

## Parameters

None (`AnimalParams::die_on_fall`, `fall_height`, `touch_margin`, `max_frames_60hz`; env `dieOnFall`, `fallHeight`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick | the fall test; `killed`, `fall_cause`, `fall_segment`, `truncated` |
| [WorldClock](../components/WorldClock.md) | context | read | tick (episodeTicks) | `dt`: the episode's length in ticks |
| [Parked](../components/Parked.md) | context | write | tick | parks the world on a fall |

## Side effects

- None beyond the registry.

## Order and dependencies

- After `AnimalSync` and `AnimalTask`, before `AnimalRewards@observe` (death reward) and `AnimalEpisode@observe` (done).

## Used in scenes
<!-- generated -->

## Tests

- `AA.5` (the limp ragdoll is ended; the kinematic half checks the same `fallen()` through `AnimalChecks`), `AA.2` (through `fallen()`), `AA.8`, in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
