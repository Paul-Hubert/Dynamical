# SandboxWatch

`SandboxWatchSys` in [`src/view/sandbox_watch.cpp`](../../../src/view/sandbox_watch.cpp) · Scope: Host · Needs renderer: yes
· Lanes: host.pre · Parameters: yes

The desktop camera of `sandbox_view` (docs/action-agents.md): it circles a living animal of world 0's sandbox and stays
with it until it dies, then takes another. With `tour` it goes from animal to animal instead: a close-up turning round
one for `stay` seconds, then a flight to the nearest animal of another species it has not just visited.

## Behaviour

**tick** (does nothing without `Worlds`, without a world, or when world 0 has no `Sandbox`):

1. The watched animal is lost when it is no longer valid, has no `SandboxAnimal` or is `dead`. In a tour, a close-up
   ends after `stay` seconds.
2. Leaving: `pick` a new animal among those with `SandboxAnimal` and `AnimalBot` that are alive, in entity order.
   - First pick (or not a tour): score 3 for the species asked for, 2 for a predator when none is asked, else 1;
     the best, the first on ties.
   - Tour (after the first): the nearest (horizontal distance from the current focus) that is not the current one,
     strongly preferring another species (-1e6) and one not among the last 6 visited (-1e4).
   - None found while lost: nothing this frame. A tour then starts a flight of clamp(distance / 10, 1.5, 4) s.
3. Framing: reach = `distance` x max(body length, height), rise = `height` x body height - stand height.
   - Flying: smoothstep from the old focus to the animal; the look point turns to the animal faster (1.8 x); reach and
     rise bump up by up to 6 m / 4 m mid-way; the orbit slows to 0.3.
   - Otherwise the focus eases toward the trunk (rate 4 / s, 8 in a tour) so falls and blows do not shake the view.
4. angle += `orbit` degrees a second; `Camera::position` = focus + (cos, rise, sin) x reach, at least 0.4 m up;
   yaw / pitch toward the look point.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `species` | string | "" | species id to prefer for the first animal; empty: a predator first |
| `distance` | float | 3.5 | distance from the animal, in its lengths (max of length and height) |
| `height` | float | 2.0 | camera height, in body heights (minus its standing height) |
| `orbit` | float | 6.0 | degrees a second round the animal |
| `tour` | bool | false | go round the animals instead of staying with one |
| `stay` | float | 5.0 | seconds of a close-up in a tour |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Worlds](../components/Worlds.md) | context | read | tick | world 0's registry |
| [Sandbox](../components/Sandbox.md) | context | read | tick, pick (world 0's registry) | present or the system does nothing; species[...].life id and predator for the first pick |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | read | tick, pick (world 0's registry) | dead, species: who can be watched |
| [AnimalBot](../components/AnimalBot.md) | component | read | tick, pick (world 0's registry) | position[0] (trunk), body length / height / stand_height for the framing |
| [Camera](../components/Camera.md) | context | write | tick | position on the orbit (at least 0.4 m up), yaw and pitch toward the animal |

## Side effects

- None besides the components and context above.

## Order and dependencies

After PlayerControl (overrides its camera) and WorldStepper (this frame's ticks). Reads world 0's registry while no
world is stepping (same thread, after the step).

## Used in scenes

<!-- generated -->

## Tests

None directly; `tools/sandbox_view_video.py` films it.
