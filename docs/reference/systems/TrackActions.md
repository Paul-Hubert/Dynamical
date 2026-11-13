# TrackActions

`TrackActionsSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `AgentFactory`, before `PhysicsStep` (`rig_world`) · Parameters: no

Plays a baked pose track (a retargeted Unity clip, docs/physical-agents/rig-animation.md) on the kinematic HumanBot:
every tick the segments are moved toward the next frame. This is the demonstration recorder of the track demos
(`rig_anim`, `rig_check`, `walker_demo`).

## Behaviour

- **init**: for every entity with [`PoseTrackPlayer`](../components/PoseTrackPlayer.md): `loadTrack(resourceFile(track))`
  (`.track.json` header and its `.track.bin` floats, mirrored to the engine frame unless `"frame": "engine"`).
  Throws `AssetError` when the track is for another body than the `HumanBot`'s, or when its `dt` differs from the
  world's by more than 1e-4 (relative). With a `HumanBot`, teleports every segment to frame 0
  (`SetPositionAndRotation`, activating).
- **tick** (every tick, not while parked). Returns when the bot is not built. For every playing player with data:
  `next = frame + 1`, wrapping to 0 with `loop`, else held on the last frame; each segment teleported when `teleport`
  or when the loop wraps, else `MoveKinematic` toward the frame over `h`; `frame = next`.

## Parameters

None. The component's `track` (required, under `resources/`), `loop` (false), `teleport` (false).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PoseTrackPlayer](../components/PoseTrackPlayer.md) | component | readwrite | init (`data`), tick (`frame`) | which track, where in it |
| [HumanBot](../components/HumanBot.md) | context | read | init (`find`), tick | body name, segment bodies |
| [WorldClock](../components/WorldClock.md) | context | read | init | the world's `dt` against the track's |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | init, tick | `SetPositionAndRotation`, `MoveKinematic` |

## Side effects

- Reads `resources/<track>.track.json` and `.track.bin`; throws `AssetError` on a mismatch.
- Moves the kinematic Jolt bodies of the walker.

## Order and dependencies

- After `AgentFactory` (needs the kinematic `HumanBot`: `motion: kinematic`, `recording: true` in `rig_world`), before
  `PhysicsStep`, then `BodySync` reads the poses back.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot_rig.cpp`: S5.1 (a track for another body is refused), S5.2 to S5.5 (the walk track on the
  kinematic rig), T9.2 / T9.4 (recorded observations).
