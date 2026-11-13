# WalkerView

`WalkerViewSys` in [`src/view/walker_view.cpp`](../../../src/view/walker_view.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The view of the human walker scenes (`walker_view`, `walker_terrain_view`, `walker_demo*`): a camera following one
world's physical walker ([HumanBot](../components/HumanBot.md)) and the "Walker" panel. On flat ground the camera is to
the side; in a world with a ground field (the terrain walker) it follows as GaitView does, from behind the walker,
swung `azimuth` degrees, from the first of nine places where the ground hides neither its feet nor its body.
The header's "target marker" is not drawn.

## Behaviour

**tick** (nothing without the world or a built `HumanBot`):

1. Pelvis = the spine segment's position; heading toward `bot->target`. The focus jumps there when unset or more than
   6 m away (a new episode elsewhere), then eases (6 / s).
2. With `camera` and a ground field (`bot->ground`): the view heading eases (1 / s) to the heading; places: the asked
   side, the other side, then higher, turned 60° and 100°; a place is clear when 15 samples on the lines to both feet
   and to the focus stay above the ground; the first clear one is kept while it stays clear (back to the first when it
   clears). The eye eases (2.5 / s) to it, jumps when far; looks at the focus - 0.25 m.
3. With `camera` and no ground field: eye = focus + (distance x side, height, 0.35 distance), looking at focus + 0.1.
4. "Walker" window: tick, decisions, target speed, speed, head height and the kill height, reward (`AgentIO`, required)
   and episode reward, the slope along and across under the pelvis (with a ground field).

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `world` | int | 0 | world followed |
| `distance` | float | 4.0 | camera distance, m |
| `height` | float | 1.2 | camera height, m (on terrain: over the focus - 1) |
| `side` | float | 1.0 | side factor (flat: x offset in distances; terrain: sign of the swing) |
| `azimuth` | float | 75 | degrees from behind, on terrain |
| `camera` | bool | true | drive the desktop camera |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | the world of "world" |
| [HumanBot](../components/HumanBot.md) | context | read | tick (world registry) | built, state (pelvis, feet, velocity), target, ground field, tick, decisions, heights, rewards |
| [AgentIO](../components/AgentIO.md) | context | read | tick (world registry) | reward shown; required once the bot is built |
| [Camera](../components/Camera.md) | context | write | tick | position, yaw, pitch (camera true) |

## Side effects

- ImGui window "Walker" (tick, decisions, target speed, speed, head height, reward, slope).

## Order and dependencies

After WorldStepper and PlayerControl.

## Used in scenes

<!-- generated -->

## Tests

None.
