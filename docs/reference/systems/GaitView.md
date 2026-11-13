# GaitView

`GaitViewSys` in [`src/view/gait_view.cpp`](../../../src/view/gait_view.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The view of the kinematic walk scenes (`ik_walk_slope`, `ik_walk_terrain`, `walker_demo_terrain_view`;
docs/physical-agents/ik-gait.md): a camera that follows one world's walker from where the ground does not hide its feet,
markers of what the gait does, and the "Gait" panel that commands it (speed, heading, the test plane's tilt, gait
parameters) and shows its checks.

## Behaviour

**tick**: marker entities are pooled (boxes and spheres, reused in placing order each frame; the unused are scaled to
0 at the end, or at once when there is nothing to show: no world, no entity with `GaitState` + `GaitCommand`, or the
gait not started).

1. Focus = the balance's centre of mass when valid, else the gait centre + 1 m; eases (5 / s); view heading eases
   (1.5 / s) to the gait heading.
2. `camera`: nine places as WalkerView (asked side, other side, higher, turned); a place is clear when the lines to
   both feet (+12 cm) and to the focus stay above the ground (`GroundField::below`); eye eases (2.5 / s), jumps when
   far; looks at the focus - 0.25 m.
3. `markers`:
   - on a `PlaneField`: a 24 m plate under the walker and lines every metre (darker every 5 m) fixed in the world;
   - the soles' prints coloured by contact (orange heel, green flat, blue toe, grey swing) and the next print (grey),
     the feet's `IKTarget` (yellow spheres);
   - with hands ready (scrambling): palm prints (green planted, grey reaching), enabled hand targets;
   - with a valid `BalanceState`: the centre of mass (red sphere, bar down, foot mark), the extrapolated centre of mass
     (magenta) and the support polygon (white bars).
4. `panel`, window "Gait": speed and heading sliders (`GaitCommand`), plane pitch / roll (`PlaneField::setTilt`),
   follow camera / markers checkboxes, camera side and distance; gait state, contacts, hands, balance, the
   `GaitReport` (now / worst), and `GaitParams` sliders (heel strike, toe-off, step width, slope slowdown, lean,
   yield_limits, scramble and its five parameters).

**finish**: destroys every marker entity.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `world` | int | 0 | world followed |
| `distance` | float | 3.4 | camera distance, m (panel slider) |
| `height` | float | 1.3 | camera height over the focus - 1, m |
| `azimuth` | float | 75 | degrees from behind the walker (panel slider) |
| `camera` | bool | true | drive the desktop camera (panel checkbox) |
| `markers` | bool | true | draw the markers (panel checkbox) |
| `panel` | bool | true | show the "Gait" window |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | the world of "world" |
| [GaitState](../components/GaitState.md) | component | read | tick (world registry) | started, heading, centre, feet / hands state and prints, speeds and slope shown |
| [GaitCommand](../components/GaitCommand.md) | component | readwrite | tick (world registry) | speed and heading sliders |
| [BalanceState](../components/BalanceState.md) | component | read | tick (world registry) | com, xcom, ground, support polygon, errors shown |
| [GaitReport](../components/GaitReport.md) | component | read | tick (world registry) | checks shown (now / worst) |
| [GaitParams](../components/GaitParams.md) | component | readwrite | tick (world registry) | heel strike, toe-off, step width, slope slowdown, lean, yield_limits, scramble sliders |
| [IKTarget](../components/IKTarget.md) | component | read | tick (world registry) | positions of the feet's and hands' targets (markers) |
| [GroundField](../components/GroundField.md) | context | readwrite+create | tick (world registry, groundField) | a PlaneField is emplaced when none; the plane's pitch / roll sliders call setTilt; std::shared_ptr<GroundField> |
| [Camera](../components/Camera.md) | context | write | tick | position, yaw, pitch (camera true) |
| [ModelManager](../components/ModelManager.md) | context | readwrite | tick (place) | primitive box and sphere |
| [ModelC](../components/ModelC.md) | component | write+create | tick (place, ModelManager::load) | primitives on their first load |
| [Transform](../components/Transform.md) | component | readwrite+create+destroy | tick (place, hide), finish | marker entities, pooled: made when the pool is short, posed each frame, unused ones scaled to 0, destroyed in finish |
| [ModelRenderable](../components/ModelRenderable.md) | component | readwrite+create+destroy | tick (place), finish | marker model and tint |

## Side effects

- ImGui window "Gait" (with "panel").
- Pooled host marker entities, destroyed in finish.

## Order and dependencies

After WorldStepper (this frame's ticks) and PlayerControl. Its panel writes the world's components between ticks: the
next tick reads them. `groundField(w)` emplaces a flat plane in a world that has none.

## Used in scenes

<!-- generated -->

## Tests

None directly (`IK.*`, `GAIT.*` test the gait headless); the captures `tests/capture/ik_walk.capture.json` and
`ik_scramble.capture.json` (scene `ik_walk_slope`) draw it.
