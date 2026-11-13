# HerdWatch

`HerdWatchSys` in [`src/logic/animals/herd.cpp`](../../../src/logic/animals/herd.cpp) (declared in [`herd.h`](../../../src/logic/animals/herd.h), registered from [`animal_lineup.cpp`](../../../src/logic/animals/animal_lineup.cpp)) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `Herds` and `PlayerControl` · Parameters: yes

Puts the desktop camera round a herd, at the height of someone standing, for videos of the herds (`tools/herd_video.py`, scene `forest_herds_watch`). It takes the herd nearest to the camera (a travelling one, one of the kind asked for, a big one, one with other herds around first), stays with it, follows it smoothly and turns round it, keeping out of the crowns of the trees between it and the herd. It overrides whatever moved the camera before it (`PlayerControl`).

## Behaviour

- **init**: the ground: context `std::shared_ptr<GroundField>`, else a `TerrainField` over the context `std::shared_ptr<TerrainGenerator>`, else a level `PlaneField`.
- **tick(dt)**: nothing without the `HerdStats` and `Camera` context variables.
  1. **Far kind** (once, when `kind` is given): when no herd of that kind is in `HerdStats`, asks `HerdSys::nearest(kind, camera, 3000 m)` through the `HerdSys*` context variable; if one is found, the camera jumps to 15 m south (+z) of its home, 3 m over the ground there, and waits up to 2 s for the herd to spawn (`Herds` follows the camera). The tick ends there.
  2. Nothing while there are no herds; while waiting, nothing until a herd of the kind shows up.
  3. **Choice** (when it has no herd, or its herd is gone): the herd of least `distance to the camera - 150 (travelling) - 5000 (of the kind) - 4 animals - 60 herds within 70 m`; logged. Focus = its centre, radius = `0.7 extent + distance`, angle = where the camera is from it.
  4. **Follow**: focus and radius eased toward the herd's (1.5/s), `dt` clamped to 0.1 s.
  5. **Trees** (every 0.5 s, with `clear`): every `Plant` with `Transform` within `radius + 30` m of the focus whose species (`VegetationLibrary`) is at least 1.5 m tall: crown radius `min(radius * scale, 0.6 height + 1)` and how much of it hangs in a sight line (more for low trees).
  6. **Orbit**: `orbit` degrees a second; with `clear` and trees, the turn is steered toward the angle within ±180° (20° lead) whose sight line is clearest (`clearance`: distance from the sight line to the crowns, and from the camera to them), up to 4 times faster or reversed; the turn rate is eased (2/s).
  7. **Height**: `height` over the ground at the camera, raised so that the ground between the camera and the herd does not hide its middle (0.8 m up), at most `height + 0.6 radius`; eased (2/s).
  8. Writes the camera's position, yaw and pitch, looking at the herd's centre 0.8 m up.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `distance` | float | 9 | metres from the edge of the herd (radius of the orbit = 0.7 extent + distance) |
| `height` | float | 2.6 | metres over the ground, more when the ground hides the herd |
| `orbit` | float | 5 | degrees a second around the herd |
| `kind` | string | `""` | name of a kind of herd to prefer; the camera goes to the nearest one within 3 km when none is near |
| `clear` | bool | true | the camera keeps out of crowns between it and the herd, stops turning rather than go through a tree |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HerdStats](../components/HerdStats.md) | context | read | tick | the herds of this frame: cell, kind, centre, extent, animals, travelling |
| [HerdSys](../components/HerdSys.md) | context | read | tick | `HerdSys*`: `nearest()` for a kind that is far |
| [Camera](../components/Camera.md) | context | readwrite | tick | position read to choose; position, yaw, pitch written |
| [Plant](../components/Plant.md) | component | read | tick | trees around the herd |
| [Transform](../components/Transform.md) | component | read | tick | where the trees are, their scale |
| [VegetationLibrary](../components/VegetationLibrary.md) | context | read | tick | height and crown radius of the species |
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | tick (`HerdSys::nearest` → `describe`) | herds of cells around the far kind |
| [GroundField](../components/GroundField.md) | context | read | init | the ground under the camera and on the sight line (`std::shared_ptr`) |
| [TerrainGenerator](../components/TerrainGenerator.md) | context | read | init | the ground without a field (`std::shared_ptr`) |

## Side effects

- Moves the desktop camera every tick once it has a herd.
- Log: the herd chosen (`HerdWatch : <kind>, <n> animals around ...`).
- Ground queries (up to ten a tick).

## Order and dependencies

- After `Herds` (reads its `HerdStats` of this frame) and after `PlayerControl` (it overrides the camera).
- Without `Herds` in the scene it does nothing.

## Used in scenes
<!-- generated -->

## Tests

None (scene `forest_herds_watch`, `tools/herd_video.py`).
