# AnimalChecks

`AnimalChecksSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, after `AnimalSync`, before the observe phase (demonstration worlds) · Parameters: no

Checks a recording of the procedural walk on the body as it goes: joint limits, the lowest point of the bodies, the
fall test, stretched joints, sliding feet, jumps in the observation, heading flips, speed against the command. The
training host writes the result (`demo checks {...}` in the log, the scene variable `checks` as a file) and the tests
read it.

## Behaviour

- **preinit**: emplaces the [`AnimalDemoChecks`](../components/AnimalDemoChecks.md) context variable.
- **tick**: returns when the bot is not built, warming up (`warm_left > 0`) or between walks (`demo_restart`): it only
  measures with `AnimalGaitPlayer` (a dynamic body keeps `demo_restart` set). Per tick:
  - `ticks++`; per joint the excess over its limits (`jointAngles`, `limitExcess`): the largest and its joint kept,
    `limit_ticks` counts ticks over 0.5 degree;
  - per segment the lowest point over the hard ground (`lowest`, `lowest_segment`);
  - the fall test (`AnimalBot::fallen`): `falls`, `fall_cause`, `fall_segment`;
  - with the walk ready: `stretch` (the furthest a segment's origin is from where its parent's joint holds it); the
    horizontal speed of every foot planted for 0.05 s or more into `slides`, and every 256 ticks the 99th percentile
    as `foot_slide`.
  - On the last tick of a window (before the observe phase, the snapshot is still the last decision's): with recorded
    decisions, progress along and commanded distance added (total and per walk); between walks the walk's
    `|progress / commanded - 1|` goes into `speed_error` and the previous frame is forgotten. An observation frame
    (`observeAnimal`) against the previous one: the largest change of an axis and of a velocity and their segments,
    `axis_jumps` (an axis changed by more than 0.6), `heading_flips` (the command turned by more than 60 degrees in the
    trunk's frame). `decisions++`, decisions per kind of command, distance covered while the command stands.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalDemoChecks](../components/AnimalDemoChecks.md) | context | readwrite+create | preinit (create), tick | the measures |
| [AnimalBot](../components/AnimalBot.md) | context | read | tick | segment states, body, snapshot, command, `recorded`, `warm_left` |
| [AnimalWalkState](../components/AnimalWalkState.md) | context | read | tick | which gait plays, its planted feet |
| [WorldClock](../components/WorldClock.md) | context | read | tick | `tick`, `decision_period`, `dt` |

## Side effects

- None beyond the registry (the host writes the file and the log line from `AnimalDemoChecks::json()`).

## Order and dependencies

- After `AnimalSync` (this tick's state), before `AnimalRewards@observe` and `AnimalObservation@observe` (it reads the
  snapshot those replace).
- `TrainHost::writeAnimalChecks` (src/train/animal_host.cpp) finishes `foot_slide` and `speed_error` at the end of the
  run.

## Used in scenes
<!-- generated -->

## Tests

- `AA.5`, `AA.6`, `AA.7`, `AA.8`, `AA.11` read `AnimalDemoChecks`, in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp).
