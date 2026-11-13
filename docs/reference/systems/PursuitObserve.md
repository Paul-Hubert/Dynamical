# PursuitObserve

`PursuitObserveSys` in [`src/logic/agents/pursuit.cpp`](../../../src/logic/agents/pursuit.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, `PursuitObserve@observe` (last tick of a decision window) · Parameters: no

The observe phase of the pursuit fixture: for every dot that is due a decision, its reward since the last one, its end,
its observation (a vector and the `Others` buffer), and the request for a decision.

## Behaviour

- **tick** (last tick of each window):
  - When the trainer asked for a RESET ([`AgentsReset`](../components/AgentsReset.md)`::requested`): clears it and
    `populate`s the field again (every dot destroyed, `chasers` + `runners` new ones; see [Pursuit](Pursuit.md)).
  - Counts the window (`PursuitField::windows`).
  - For each dot (entity order) that has not ended: it is **due** when `fresh`, a chaser (every window), or a runner on
    a window where `(windows + phase) % runner_every == 0`; a caught runner is always due.
    - A `fresh` dot sends its first observation with reward 0, not done, and becomes not fresh: what happened before
      it is paid with the next decision.
    - Otherwise: counts the decision; reward = what it collected (catch +1 / caught -1), plus `alive_reward` for a
      runner not caught, plus for a chaser without a catch in this window `approach_reward` times how much closer the
      nearest live runner is than at its last decision. `done` = caught or old (`chaser_decisions` /
      `runner_decisions`), `truncated` = old and not caught. Stats: `Pursuit/Catches per decision` (chasers);
      `Pursuit/Runner decisions` and `Pursuit/Runner caught` (a runner's end). `ended` = done: the dot leaves or
      starts again at the next window.
    - Writes the observation: position over the field's half side, sin and cos of the yaw, speed over the sprint
      speed, stamina; and the `Others` buffer, the other dots nearest first (runners already caught left out), each
      row its offset (forward, left, over the field's side), relative velocity (over the sprint speed), and one-hot
      chaser / runner. Sets `AgentIO::requested`.

## Parameters

None for the system: it uses the parameters of `Pursuit`, from [`PursuitField`](../components/PursuitField.md).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PursuitField](../components/PursuitField.md) | context | readwrite | tick | parameters; `windows` counted |
| [AgentsReset](../components/AgentsReset.md) | context | readwrite | tick | the trainer's RESET, cleared |
| [PursuitDot](../components/PursuitDot.md) | component | readwrite+create+destroy | tick, populate on RESET | rewards collected, decisions, `fresh`, `ended`, `nearest`; all made again on a RESET |
| [AgentIO](../components/AgentIO.md) | component | readwrite+create+destroy | tick, populate on RESET | reward, done, truncated, stats, observation, buffer, `requested` |
| [WorldRng](../components/WorldRng.md) | context | readwrite | populate on RESET | places of the new dots |

## Side effects

- On a RESET every dot entity is destroyed and new ones are created (new agent ids).

## Order and dependencies

- Last of the world's fixed list as `@observe`, after `Pursuit` (moves and catches of the window). The host reads the
  agents whose `requested` is set right after it.

## Used in scenes
<!-- generated -->

## Tests

- `PURSUIT.1` to `PURSUIT.6` in [`tests/unit/pursuit.cpp`](../../../tests/unit/pursuit.cpp)
- `MA.1`, `MA.2` in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp)
