# Mountables

`MountablesSys` in [`src/logic/player/mount.cpp`](../../../src/logic/player/mount.cpp) · Scope: Host · Needs renderer: no
· Lanes: `host.pre`, after `AnimalAnimation` / `AnimalGait` (or `AnimalBotSteer`), before `PlayerLegs` and `RigidBody` · Parameters: yes

Makes animals rideable. Animals of the listed species (kinematic `Animal`s of the library, and the `AnimalBotProxy` of a physical animal) get a `Mountable` measured on the skin of the species: the top of the back between the hips of the fore and hind legs (the saddle), the joint that carries it, the box of the trunk, and the speeds a rider asks. Mountables within `range` of a `Rider` (or ridden) get a kinematic collider body on their trunk, moved with them every frame, which is what a player bumps into and gets on.

## Behaviour

- **Constructor**: `species` as an array or a single string (default `["horse"]`), `range`, `walk`, `run`.
- **preinit**: makes the pools of `Mountable`, `MountCollider`, `Rider`, `Animal`, `AnimalPose`, `AnimalBotProxy`,
  `AnimalSkin`, `Transform`; connects `on_destroy<Mountable>` (destroys its collider entity).
- **tick**:
  1. Whether a species is wanted is decided once per library entry (its id in the list).
  2. Every `Animal` without `Mountable` or `AnimalSkin` of a wanted species, and every `AnimalBotProxy` without
     `Mountable` of a wanted species, gets one from the species' **shape** (computed once, cached): the skin
     (`AnimalLibrary::loadGeometry`) and `analyseGait` of the walk clip give the hips (else +-0.2 of the length around
     the middle); the saddle is the highest skin vertex in a band 60 % of the way from hind to fore within 0.3 of the
     width (0.75 of the height when that is too low); the joint is the one with the most skin weight at the top of
     that band; the trunk box spans the hips +-0.1 length, 0.24 of the saddle height thick, as wide as the skin
     (0.08 m to 0.6 of the width). `walk_speed = walk x` the walk clip's speed (or 0.6 x length), `run_speed =
     max(run x` the run (else trot) clip's speed, `1.6 x walk_speed)`. A proxy takes its policy's `walk_speed` and
     `max(top_speed, walk_speed)`, and the Transform carries its saddle when it has no `AnimalPose`. A species whose
     shape fails is no longer wanted.
  3. Colliders: a Mountable near a `Rider` (distance of the Transforms < `range`) or ridden gets an entity with
     `Transform`, `MountCollider`, `Ungrabbable` and a kinematic box `RigidBody` (half extents x the animal's scale y,
     friction 0.4, restitution 0); one that is not loses it (entity destroyed). Every collider's `Transform` is put on
     the trunk box (`position + rotation x centre x scale`, the animal's rotation).
- **finish**: disconnects the signal.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `species` | string or [string] | `["horse"]` | library ids of the species that can be ridden |
| `range` | float | 40 | m from a Rider within which a Mountable has its collider body |
| `walk` | float | 2 | walk speed asked, in speeds of the walk clip |
| `run` | float | 2.4 | run speed asked, in speeds of the run (else trot) clip; at least 1.6 x the walk |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalLibrary](../components/AnimalLibrary.md) | context | read+create | tick (helper animalLibrary), shape | loaded from resources/animals on first use when missing; entries, species (loaded on first use) and their geometry |
| [Animal](../components/Animal.md) | component | read | preinit (storage), tick | species of the kinematic animals |
| [AnimalSkin](../components/AnimalSkin.md) | component | read | preinit (storage), tick | excluded: the drawn skin of a ragdoll is not ridden |
| [AnimalBotProxy](../components/AnimalBotProxy.md) | component | read | preinit (storage), tick | species and the walk / top speeds of a physical animal's policy |
| [AnimalPose](../components/AnimalPose.md) | component | read | preinit (storage), tick | a proxy without one has its saddle on the Transform |
| [Mountable](../components/Mountable.md) | component | readwrite+create | tick; signal on_destroy<Mountable> | emplaced from the species' shape; `rider` read, `collider` written; a destroyed Mountable destroys its collider entity |
| [MountCollider](../components/MountCollider.md) | component | write+create | preinit (storage), tick | on the collider entity: the animal it stands for |
| [Ungrabbable](../components/Ungrabbable.md) | tag | write+create | tick | on the collider entity |
| [Rider](../components/Rider.md) | component | read | preinit (storage), tick | the players whose distance makes colliders |
| [Transform](../components/Transform.md) | component | readwrite+create | preinit (storage), tick | riders' and animals' read; the collider's created and set to the trunk box every frame |
| [RigidBody](../components/RigidBody.md) | component | write+create | tick | on the collider entity: kinematic box of the trunk times the animal's scale, friction 0.4 |

## Side effects

- Connects `on_destroy<Mountable>` in preinit (disconnected in finish).
- Reads the species' skin geometry (`resources/animals/species/<id>.bin`) once per species, runs `analyseGait` on its walk clip.
- Creates a collider entity per Mountable within `range` of a Rider (or ridden) and destroys it when none is.
- Logs once per species: `Mountables : <id>, saddle <h> m up on <joint>, walks <v> m/s, runs <v> m/s` (info), or a warning when the geometry cannot be read.

## Order and dependencies

After the systems that make and move the animals (`Herds`, `MountSpawn`, `SteeredWalk`, `AnimalAnimation`, `AnimalGait`, `AnimalBotSteer`), so that the collider follows this frame's pose; before `RigidBody`, which makes and moves the kinematic bodies. `Mounting` and `MountSteering` read the `Mountable`s of the frame before.

## Used in scenes

<!-- generated -->

## Tests

`HERD.2` in [`tests/unit/animals.cpp`](../../../tests/unit/animals.cpp) (a saddle on the skin, the clips' speeds, no collider without a rider); [`tests/unit/mount.cpp`](../../../tests/unit/mount.cpp) `MOUNT.2` to `MOUNT.5` (with a hand-made `Mountable`: the colliders).
