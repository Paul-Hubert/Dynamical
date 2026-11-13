# Rewards

`RewardsSys` in [`src/logic/agents/humanbot.cpp`](../../../src/logic/agents/humanbot.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `Rewards@observe`, first of the observe phase · Parameters: no

The reward of the HumanBot walker's decision window and its training stats, rewarded against the command the last
observation carried (the decision snapshot). It is also where an episode starts again: a pending reset (after
`done`) or the trainer's RESET is carried out here, before the observation of the new episode.

## Behaviour

**tick** (last tick of each window; observe systems also run while parked). Returns when the bot is not built.

1. **Reset**: `AgentIO::reset_requested` or `HumanBot::pending_reset` -> clears the request, `resetHumanBot(reg)` (see
   [AgentFactory](AgentFactory.md#episode-reset-resethumanbot-also-called-by-rewards)), return: no reward this window.
2. **Root reward**: root velocity = spine displacement since the snapshot over the window (`window_ticks x dt`);
   `root_speed` = its component along the snapshot's command; `root = min(root_speed, target_speed) x rootReward /
   target_speed` (0 for a zero target speed).
3. `dying` again; reward = `dyingReward` when dying, else `lifeReward + root`.
4. **Stats** (`AgentIO::stats` cleared, then Unity's names): `Head/*` (spring error as a histogram, height in body
   height units, max speed / error / height, min height, zeros for the spring and damper rewards), `Death/Dying
   Reward`, `Death/Alive`, `Root/*` (speed, speed error, damper reward, horizontal and total velocity, max speed /
   error / distance from the spawn, spring error and its normalised reward, damper reward derivative),
   `Environment/Total Frame Reward` and the alive / dying split, `Collision/*` (always 0). Dynamic bot: per joint and
   in aggregate `Bot Joint/Spring Factor`, `Damper Factor`, `Spring`, `Damper`, `Angular Velocity Factor` (0),
   `Target Angular Velocity`, `Joint Angular Velocity`, and `Bot Joint/Action <joint>`, from `AgentIO::action`.
5. **End of episode**: killed -> `Death/Death Frames`, `Death/Total Death Reward`; truncated -> `+ winReward`,
   `Win/Total Win Reward`. On a terrain with routes: `Terrain/Slope Along` and `Terrain/Slope Across` of the ground
   under the spine (`GroundField::below`), and at the end `Terrain/Route Lost`, `Terrain/Spawn Draws`. Any end:
   `Win/Win Reward`, `Death/Death Reward`, `Win/Won`, `Death/Dead`.
6. **Energy reward** (Unity's `EnergyRewardProvider`), when `energyBonusWeight > 0` or the window did work: power
   `= positiveMultiplier x pos / T + negativeMultiplier x neg / T + torqueRateWeight x torque variation / T` from the
   window's books of [Energy](Energy.md); speed toward the target smoothed with `1 - exp(-T / tau)`; cost of transport
   `cot = power / clamp(speed, minSpeedFloor, max(minSpeedFloor, target_speed))`; `r = exp(-err² / (2 edge²)) x
   exp(-cot / energyScale) x energyBonusWeight` added to the reward. Stats `EnergyProvider/Energy Reward`, `Positive
   Energy`, `Negative Energy`, `Cost of Transport`, `Smoothed Speed`, `Torque Rate Power` (when weighted), `Torque
   Variation Rate`, and `Energy/Closure Residual` (the books' residual over the absolute work so far). The window's
   books are then zeroed.
7. Killed: the whole reward is `deathPenalty` (0 by default). `AgentIO::reward = reward x rewardFactor`, added to
   `cumulative_reward`.

## Parameters

None. From the params file (`unity_parameters`, `unity_energy`) or the env params: `rootReward` (0.34), `lifeReward`,
`dyingReward`, `winReward`, `deathPenalty` (0), `rewardFactor` (1), `energyBonusWeight` (0), `energyScale` (30000),
`energySpeedEdge` / `speedEdgeWidth` (0.15), `energyMinSpeedFloor` (0.1), `energyVelocitySmoothingTau` (0.2),
`positiveEnergyMultiplier`, `negativeEnergyMultiplier` (1), `torqueRateWeight` (0).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [HumanBot](../components/HumanBot.md) | context | readwrite | tick, reset | state and snapshot read; maxima, `dying`, energy speed, books' window sums, `cumulative_reward` written; whole episode state at reset |
| [AgentIO](../components/AgentIO.md) | context | readwrite | tick, reset | `reset_requested` cleared, `action` read, `stats` and `reward` written |
| [WorldClock](../components/WorldClock.md) | context | read | tick | `dt` for the window's length |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | reset (`applyEnvParams`, `applySlope`) | parameters of the new episode |
| [WorldRng](../components/WorldRng.md) | context | readwrite | reset | speed, spawn, target, yaw, tumble, slope draws |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | reset | bodies placed, masses, warm starts |
| [Ground](../components/Ground.md) | component | read | reset (`applySlope`) | the ground boxes to tilt |
| [Parked](../components/Parked.md) | context | write | reset | unparked |

## Side effects

- At a reset: Jolt bodies of the walker and of the `Ground` boxes moved, mass properties changed; log warning when
  no spawn point is found in the slope band, error for an unusable continuous speed range.

## Order and dependencies

- First of the observe phase: `Rewards@observe`, `Observation@observe`, `Episode@observe`, in that order. It reads the
  snapshot that the previous `Observation` wrote, before this one overwrites it; `Episode` sets the `pending_reset` it
  acts on one window later.
- After `Energy` (window work), `Termination` (`killed`, `truncated`).

## Used in scenes

<!-- generated -->
- [`common/humanbot_terrain_world.json`](../../../resources/scenes/common/humanbot_terrain_world.json): `fixed@observe`
- [`common/humanbot_world.json`](../../../resources/scenes/common/humanbot_world.json): `fixed@observe`
- [`common/rig_world.json`](../../../resources/scenes/common/rig_world.json): `fixed@observe`

## Tests

- `tests/unit/humanbot.cpp`: T8.8 (zero target speed: no NaN, root reward 0), S8.1 / S8.2 (root rewards against
  Unity), S4.3, the torque-rate term, DeVita's stats leave with the reset.
- `tests/unit/humanbot_rig.cpp`: S7.5 (closure of the energy books).
