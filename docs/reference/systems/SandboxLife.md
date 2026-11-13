# SandboxLife

`SandboxLifeSys` in [`src/logic/sandbox/sandbox.cpp`](../../../src/logic/sandbox/sandbox.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, right after `PhysicsStep` · Parameters: no

The rules of life of the [sandbox](Sandbox.md) that follow from what the bodies did during the physics step:
falls, the energy that moving costs and feeding gives, wounds from the blows of heads in Attack mode, mating,
deaths, and the grass growing back. The parameters are the species' `SpeciesLife` and the `LifeParams` of the
[Sandbox](../components/Sandbox.md) context (resources/agents/params/life.params.json); every duration is scaled
by `life_scale`.

## Behaviour

**tick**, over the animals in entity order:

1. `sb.seconds += h`. Every animal's [AnimalBot](../components/AnimalBot.md) state is read back from its Jolt
   bodies (`syncAnimalBot`: positions, rotations, velocities, the origin velocities by difference, `tick` and
   `window_ticks` counted). A carcass only rots (`rot += dt`).
2. Living animals: walker ticks, window ticks and the ticks of the current mode counted; age, time lived, time
   since attacked and since fed advance, the stun runs down.
3. **Falls** (not in puppet worlds, not already fallen or waiting to stand): `AnimalBot::fallen` (trunk too low or
   a body that must not touch the ground does) marks `fell`, the bot `killed` with its cause and segment, and
   costs `fall_damage` of health (counted as damage taken, a fall in the window and in the species).
4. **Work**: the positive change of the bodies' mechanical energy (kinetic + potential, per segment mass) over the
   tick, smoothed over a quarter second, is the animal's power (a puppet: 0.1 m g v of its trunk speed). The
   species sums power over the reference (0.1 m g x walk speed) and distance moved. The energy spent is
   `energyDrain(life, life_scale, power, mass, walk speed, resting)` x dt (basal rate, plus `work` of it per
   reference of power, x `rest_drain` in Rest mode).
5. **Feeding** in Feed mode, slower than `feed_speed` walk speeds, not stunned, store not full: a grazer whose
   muzzle is in grass taller than the stubble (muzzle under the grass top + 5 cm) eats `graze` x dt of height from
   the cell (`GrassField::eat`) and gains the full feeding rate (`dt / feed`) times what it ate over what it
   wanted; a predator whose muzzle is within `carcass_reach` x (its height + the carcass's) of a body of a carcass
   eats up to the full rate from its meat. Feeding resets `since_fed`.
6. Health heals at `1 / heal` a second (x `rest_heal` in Rest mode), capped at 1; gestation advances; a growth
   is due once the age reaches the next stage's start.
7. **Wounds**. Puppet worlds: a muzzle in Attack mode within half a victim's length of its trunk takes `bite / 2`
   health a second. Physical worlds: the events the contact listener recorded during the step (two ragdoll groups,
   their segments, the estimated impulse) are drained; for each side, an attacker in Attack mode, not stunned,
   whose touching segment is its head or its weapon wounds the other by `woundOf(params, attacker, impulse,
   victim mass)` (impulse over mass above `wound_threshold`, a full health at `kill_impulse` over it times
   `bite`). `contacts` counts every event, `blows` those that wounded; the victim remembers its attacker.
8. **Mating**: an adult female, not pregnant, not stunned, in Mate mode with at least `mate_energy`, and an adult
   male of its species in the same state within `mate_reach` body lengths accumulate time together; at
   `mate_time` she is pregnant (father kept), a conception in both windows. Apart, her mating time runs down.
9. **Deaths**: health 0 = killed, energy 0 = starved, age `old` = old age. `die` marks the carcass (meat =
   `meat` x scale³), counts the death and the lifetime, gives -1 objective unless old age, and credits a kill to
   a living attacker who wounded it within 10 s.
10. `draw`: the shapes follow their segments (`moveShapes`).
11. Once a simulated second (the last tick of each second), the grass grows back by that second over the life
    scale (`GrassField::grow`, stubble to full in `regrow` seconds).

## Parameters

None (the rules are the [Sandbox](Sandbox.md)'s).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Sandbox](../components/Sandbox.md) | context | readwrite | tick | `seconds`, the grass eaten and grown back, `contacts` and `blows`, species counters (falls, deaths, work, distance) through die() |
| [WorldClock](../components/WorldClock.md) | context | read | tick | the grass grows back once a simulated second |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | read | tick | bodies' positions, rotations and velocities (syncAnimalBot); the contact events SandboxSys::Contacts collected during the step are drained |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | readwrite | tick | ages, timers, falls, power and energy, feeding, health, gestation, growth due, wounds, mating, death and its cause, a carcass's meat and rot |
| [AnimalBot](../components/AnimalBot.md) | component | readwrite | tick | state read back from the bodies (syncAnimalBot), `killed` / `fall_cause` / `fall_segment` on a fall; mode and muzzle read |
| [SandboxShape](../components/SandboxShape.md) | component | read | tick (moveShapes, draw) | the animal each drawn shape follows |
| [WorldShape](../components/WorldShape.md) | component | read | tick (moveShapes, draw) | segment and local offset of the shape |
| [Transform](../components/Transform.md) | component | write | tick (moveShapes, draw) | position and rotation of the shape from its segment |
| [PhysicsPose](../components/PhysicsPose.md) | component | readwrite+create | tick (moveShapes, draw) | emplaced on a drawn shape on its first tick, then previous = current, current = the segment's pose, for WorldView's interpolation |

## Side effects

- Drains the contact listener's recorded events (the listener is SandboxSys's).

## Order and dependencies

- After `PhysicsStep` (it reads the step's bodies and contacts) and after [Sandbox](Sandbox.md) (which made the
  context and installed the contact listener); before [SandboxObserve](SandboxObserve.md), which reports the falls
  and deaths it marked.
- A death is not removed here: the animal stays a carcass with its agents until SandboxObserve sent their last
  steps and Sandbox's next window drops them.

## Used in scenes

<!-- generated -->
- [`common/sandbox_world.json`](../../../resources/scenes/common/sandbox_world.json): `fixed`

## Tests

- `SANDBOX.2` (basal drain), `SANDBOX.3` (wounds in Attack mode only), `SANDBOX.4` (falls), `SANDBOX.5` (death and
  carcass), `SANDBOX.6` (feeding on grass and carcasses), `SANDBOX.7` (mating, gestation, growth), `SANDBOX.10`
  (the weapon strikes) in [`tests/unit/sandbox.cpp`](../../../tests/unit/sandbox.cpp); `LIFE.1` to `LIFE.3` for
  `energyDrain`, `woundOf` and `GrassField`.
