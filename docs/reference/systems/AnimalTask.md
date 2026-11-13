# AnimalTask

`AnimalTaskSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `AnimalSync` · Parameters: no

The task of the physical animal: advances its `WalkCommand` (speed and turn drawn every few seconds from the
`GaitRanges`, eased in, the heading turned), the same process the recorded walks follow, so a policy cannot tell a
demonstration from its own task. With orders it also draws the mode and the head offsets.

## Behaviour

- **tick**: returns when the bot is not built or is kinematic (`AnimalGaitPlayer` advances that command).
  `advanceCommand(reg, bot, trunk position, body.walk_speed, h)`:
  - with a fixed spawn `speed`: only `command.time += h`;
  - else `stepCommand`: `time += h`; past `until` a new draw (`drawCommand`: the next draw in `ranges.segment`
    seconds; stand / walk / run by `ranges.idle` and `ranges.run` when the body runs; speed in the ranges' band times
    the walk clip's speed, or the run's; a turn of radius in `radius` x length, straight with probability
    `straight`); speed and turn eased toward the draw at `min(2h, 1)` per tick; `yaw += turn x speed x h`;
  - on a terrain (routes and a ground field): when a draw happened or there is no target, `drawDirection` gives a
    heading where the ground stays in the slope band; none found sets `route_lost` (the episode is truncated). The turn
    asked is toward that heading, at most the tightest turn of the ranges (`1 / (radius[0] x length)`).
- With orders: when `command.time >= order_until` a new order (`drawOrder`: Move 45 %, Feed 17 %, Attack 18 %,
  Mate 10 %, Rest 10 %; head offsets drawn per mode and clamped to 1 rad of yaw, 1.1 rad down, 0.5 up; next order in
  2 to 6 s); the command in force is rescaled to the new mode's speed factor (Feed 0.15, Mate and Rest 0, others 1),
  or redrawn at once coming out of a mode that stood. A command redrawn under an order gets that order's factor.

## Parameters

None (the ranges file of `AnimalSpawn::ranges`, and `AnimalSpawn::speed`, `orders`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick (advanceCommand, drawOrder) | `command`, `route_lost`, `mode`, `head`, `order_until` |
| [WorldRng](../components/WorldRng.md) | context | readwrite | tick (advanceCommand, drawOrder) | the draws |

## Side effects

- None beyond the registry. The ground field and routes are used through the bot's own copies.

## Order and dependencies

- After `AnimalSync` (the trunk position of this tick), before `AnimalTermination` (which truncates on `route_lost`)
  and the observe phase (`AnimalObservation` snapshots the command for the next window).

## Used in scenes

<!-- generated -->
- [`common/animal_orders_world.json`](../../../resources/scenes/common/animal_orders_world.json): `fixed`
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed`
- [`common/animal_world.json`](../../../resources/scenes/common/animal_world.json): `fixed`

## Tests

- `AA.10` (command draws: stand, walk, run, speed alone tells them apart; the free functions), `AA.8` (terrain headings), `AA.4`, in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
