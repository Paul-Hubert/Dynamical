# DeVitaStepStat

`DeVitaStepStatSys` in [`src/logic/agents/humanbot_energy.cpp`](../../../src/logic/agents/humanbot_energy.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick, after `Energy` and `Termination` · Parameters: no

DeVita's protocol (Unity's `DeVitaStepStat`): the sagittal work of the hip, knee and ankle of each leg over each of
its stances, split by sign, so that the ratio of negative to positive work per stance can be compared with human
gait data. It also counts the walker's steps.

## Behaviour

**tick** (every tick, not while parked). Returns when the bot is not built, kinematic, the energy books are not valid,
or when neither the energy reward is on (`energyBonusWeight > 0`) nor the run is an eval (`view_truncate`).

- Sagittal axis `ml = up x (horizontal direction to the target)`. For each side: the foot is loaded when the vertical
  contact impulse of [Energy](Energy.md) over `dt` is at least `0.05 x totalMass x 9.81` N. The limb's work this tick:
  per joint `(torque · ml) x (ω · ml) x dt`, split by sign.
- A `StanceWindow` per foot (debounce 3 ticks, at most 2 s): opens at the first loaded tick, closes at the last;
  a stance of 0.15 s or more counts a step (`DeVitaBooks::strikes`), and, when it started after
  `statsSettleSeconds` (2 s), adds its positive and negative work to the episode's sums and `stances`.
- The episode's sums leave as stats at the next reset (`DeVita/*`, see [AgentFactory](AgentFactory.md)), which also
  adds them to the run totals that the eval summary reads.

## Parameters

None; `energyBonusWeight`, `statsSettleSeconds`, `totalMass` from the params / env params.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick | `books`, `state`, `target` read; `devita` written |

## Side effects

- None.

## Order and dependencies

- After `Energy` (contact impulses, joint torques of the step). `Task` and eval legs read `devita.strikes`.

## Used in scenes
<!-- generated -->

## Tests

- `tests/unit/humanbot.cpp`: "A DeVita stance window runs from the first loaded tick to the last", "DeVita's episode
  stats leave with the reset".
