# AnimalRewards

`AnimalRewardsSys` in [`src/logic/animals/animal_agent.cpp`](../../../src/logic/animals/animal_agent.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list as `AnimalRewards@observe` (first of the observe phase systems) · Parameters: no

Rewards the decision window that just ended (`rateAnimal`: speed along the commanded heading, with orders the head and
the mode), writes the trainer's stats, keeps the episode record, and starts a new episode when one is due (the
trainer's RESET or an episode that ended at the last window).

## Behaviour

- **tick** (last tick of each window, also while parked): returns when the bot is not built.
  - **Reset**: when `io.reset_requested` (cleared here) or `bot.pending_reset` (set by `AnimalEpisode`):
    `resetAnimalBot(reg)` and return. The reset: env params applied again; a dynamic body is stood at a new spawn
    point and yaw with a soft depth and first command drawn (see [AnimalFactory](AnimalFactory.md)); a kinematic one
    gets `demo_restart`; with orders a first order; counters, snapshots, the stack of previous frames, the episode
    record cleared; `fresh = true`; `AgentIO` action zeroed, reward 0, done / truncated false, stats cleared; the
    world unparked.
  - **Rating** over `window = max(window_ticks, 1) x dt`: trunk velocity from the snapshot of the last decision;
    `along` = its part along the commanded direction; `root = min(along, snapshot_speed) x root_reward`; with orders
    going over the speed costs as much; divided by `max(snapshot_speed, 0.25 walk_speed)` when the command moves or
    with orders; with `over_speed > 0` any horizontal speed over the command costs `over_speed` times, and a negative
    result is `-life_reward x tanh(-x)` when `life_reward > 0`. Heading error between the trunk's heading and the
    command. With orders: `head = head_reward x exp(-err^2 / (2 x 0.35^2))` on the angle between where the head points
    and where it is asked to; `mode = mode_reward x` Feed: muzzle down to the ground from its standing height,
    Attack: the head's speed along where it points over 2 walk speeds, Mate: stillness of the trunk, Rest: stillness
    of the trunk and of every body; Move: 0.
  - Reward = `life_reward + root + head + mode`; on a death the whole step's reward is replaced by `death_penalty`.
    `io.reward = reward x reward_factor`, added to `cumulative_reward`.
  - **Stats** (`io.stats`, cleared then filled; 3 = histogram): `Root/Root Speed`, `Root/Root Damper Reward`,
    `Root/Root Horizontal Velocity`, `Animal/Speed Ratio`, `Animal/Commanded Speed`, `Animal/Heading Error` (degrees),
    `Animal/Trunk Height` (standing heights), `Animal/Target Clamped`, `Animal/Soft Depth` (when soft), with orders
    `Order/Head Reward`, `Order/Head Error`, `Order/Reward <mode>`, `Order/Feed Muzzle Height`; `Death/Alive`; with
    ranges that stand or run `Gait/Idle|Walk|Run Speed` and `Gait/Walk|Run Speed Ratio`; on a death `Death/Death Frames`,
    `Animal/Fall Trunk Low`, `Animal/Fall Body Touch`; at the end of an episode `Win/Won`, `Death/Dead`,
    `Animal/Episode Speed Ratio` and on a terrain `Terrain/Route Lost`.
  - **Episode record** (`bot.episode`): distance, progress along the command, commanded distance, summed heading
    error, and per kind of command (stand / walk / run by `commandKind`) seconds, distance, progress, commanded.

## Parameters

None (`AnimalParams`: `root_reward`, `life_reward`, `death_penalty`, `reward_factor`, `over_speed`, `head_reward`,
`mode_reward`; env `rootReward`, `lifeReward`, `deathPenalty`, `rewardFactor`, `headReward`, `modeReward`).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AnimalBot](../components/AnimalBot.md) | context | readwrite | tick, resetAnimalBot | rating inputs; `episode`, `cumulative_reward`; reset of the whole episode state |
| [AgentIO](../components/AgentIO.md) | context | readwrite | tick, resetAnimalBot | `reset_requested` read and cleared; `reward`, `stats` written; action, done, truncated cleared on a reset |
| [WorldClock](../components/WorldClock.md) | context | read | tick | `dt` of the window |
| [WorldRng](../components/WorldRng.md) | context | readwrite | resetAnimalBot | spawn, yaw, soft depth, command, order draws |
| [AgentEnvParams](../components/AgentEnvParams.md) | context | read | resetAnimalBot (applyEnvParams) | trainer overrides of the params |
| [PhysicsWorld](../components/PhysicsWorld.md) | context | readwrite | resetAnimalBot (standAnimalBot) | the bodies placed in the standing pose (dynamic bodies) |
| [Parked](../components/Parked.md) | context | write | resetAnimalBot | unparks the world |

## Side effects

- On a reset, teleports the ragdoll's Jolt bodies (zero velocities, warm starts of the constraints reset).
- Logs a warning when a routed reset finds no spawn point in the slope band.

## Order and dependencies

- First of the observe phase: after `AnimalTermination` (killed, truncated), before `AnimalObservation` (which then
  takes the next snapshot; the rating reads the previous one) and `AnimalEpisode`.
- A reset here is followed in the same tick by the observation of the fresh episode; `AnimalEpisode` sees `fresh` and
  reports it not done.
- The eval (`TrainHost::evalAnimals`) sets `reset_requested` and runs the observe phase alone to start every world.

## Used in scenes

<!-- generated -->
- [`common/animal_demo_terrain_world.json`](../../../resources/scenes/common/animal_demo_terrain_world.json): `fixed@observe`
- [`common/animal_demo_world.json`](../../../resources/scenes/common/animal_demo_world.json): `fixed@observe`
- [`common/animal_orders_world.json`](../../../resources/scenes/common/animal_orders_world.json): `fixed@observe`
- [`common/animal_terrain_world.json`](../../../resources/scenes/common/animal_terrain_world.json): `fixed@observe`
- [`common/animal_world.json`](../../../resources/scenes/common/animal_world.json): `fixed@observe`

## Tests

- `AA.11` (a command to stand is lost by moving: `rateAnimal(...).root`), `AA.4`, `AA.6` (the speed the reward measures is the commanded one), in [`tests/unit/animal_agents.cpp`](../../../tests/unit/animal_agents.cpp); `rateAnimal` is also used by the sandbox (`SANDBOX.*`, [`tests/unit/sandbox.cpp`](../../../tests/unit/sandbox.cpp)).
