# Episode

`EpisodeSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `Episode@observe`, last of the observe phase · Parameters: no

Tells the training host whether the HumanBot episode ended with this observation, and parks the world until the
reset that `Rewards` carries out at the next observe phase.

## Behaviour

**tick** (observe phase, also while parked). Returns when the bot is not built.

- First window of an episode (`fresh`): clears `fresh`, `done = truncated = false`.
- Else `done = killed || truncated`, `AgentIO::truncated = truncated && !killed`. When done: `pending_reset = true`
  and `Parked = true` (the next window's physics and act systems are skipped; its observe phase resets the episode).

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | `fresh` cleared, `killed` / `truncated` read, `pending_reset` set |
| [AgentIO](../components/AgentIO.md) | context | write | tick | `done`, `truncated` |
| [Parked](../components/Parked.md) | context | write | tick | parks the world at the end of an episode |

## Side effects

- Parking stops `PhysicsStep` and the every-tick systems of the world for one decision window.

## Order and dependencies

- Last of `Rewards@observe`, `Observation@observe`, `Episode@observe`. The host reads `AgentIO` after it.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: S4.3 (kills, resets, ids stable).
- `tests/unit/terrain_walker.cpp`: TW.6, TW.11.
