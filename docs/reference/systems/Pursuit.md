# Pursuit

`PursuitSys` in [`src/logic/agents/pursuit.cpp`](../../../src/logic/agents/pursuit.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, every tick (template `common/pursuit_world.json`) · Parameters: yes

The pursuit fixture (docs/action-agents.md, several agents in a world): Unity's Predator and Prey as points on a
square field, without physics. It is the test bed of what the sandbox asks of the training host: agents of two
behaviours (`Chaser`, `Runner`) as [`AgentIO`](../components/AgentIO.md) **components** in one world, a vector and a
buffer observation, continuous and discrete actions, agents that die and are born with another id, and a behaviour
that decides at half the rate of the other. This system owns the dots: it creates them, moves them, and judges catches.

## Behaviour

- **preinit**: emplaces the [`PursuitField`](../components/PursuitField.md) context variable with the parameters, and an
  [`AgentsReset`](../components/AgentsReset.md) flag when there is none (which tells the host the world has agents
  before any exists).
- **init**: `populate`: destroys every entity with a [`PursuitDot`](../components/PursuitDot.md), then creates
  `chasers` chasers and `runners` runners. Each is an entity with a `PursuitDot` placed at random on the field
  (random yaw, full stamina, `fresh`, a random `phase` in [0, `runner_every`)) and an `AgentIO` component configured
  as behaviour `Chaser` or `Runner`, 6 observation floats, 2 continuous actions, one discrete branch of 2 (walk,
  sprint) and a buffer sensor `"Others"` of `others` rows of 6.
- **tick**, on the first tick of a window (`tick % decision_period == 0`):
  - Dots whose terminal step went out (`ended`): a chaser is placed again at random with the same entity (its
    `AgentIO` done, truncated, reward, action and discrete cleared); a runner's entity is destroyed and a new runner
    is spawned (a new entity, hence a new agent id), `births` counted.
  - Every dot reads its `AgentIO`: speed = `(action[0] + 1) / 2` clamped to [0, 1] (-1 when missing or not finite,
    i.e. 0), turn = `action[1]` clamped to [-1, 1], sprint = `discrete[0] == 1`. A `fresh` dot (no decision yet in
    this episode), and every runner with `runners_still`, is held at rest.
- **tick**, every tick (dots in entity order, sorted so that destroyed entities do not change the order):
  - A caught dot stops. Others drain stamina while sprinting with stamina left (`stamina_drain`/s), gain it otherwise
    (`stamina_gain`/s), turn by `turn * turn_rate`, move forward at `speed * speed_param` (times `sprint` while
    sprinting), are clamped to the field and get their velocity from the displacement.
  - Every chaser within `catch_radius` of a runner not yet caught catches it: the runner gets -1 and `caught`, the
    chaser +1 and a catch, `PursuitField::catches` counts it.

## Parameters

| Key | Type | Default | Meaning |
|---|---|---|---|
| `field` | float | 20 | half side of the square field, m |
| `chasers` | int | 2 | chasers per world |
| `runners` | int | 4 | runners per world (kept constant: a caught or old runner is replaced) |
| `catch_radius` | float | 1 | m |
| `speed` | float | 3 | m/s for a speed action of 1 |
| `sprint` | float | 1.7 | speed factor while sprinting |
| `turn_rate` | float | 3 | rad/s for a turn action of 1 |
| `stamina_drain` | float | 0.5 | of a full stamina per second of sprint |
| `stamina_gain` | float | 0.15 | per second otherwise |
| `chaser_decisions` | int | 300 | a chaser's episode, in its decisions |
| `runner_decisions` | int | 200 | a runner's old age, in its own decisions |
| `runner_every` | int | 2 | windows between two decisions of a runner (at least 1) |
| `alive_reward` | float | 0.01 | a runner's reward per decision it is not caught in |
| `others` | int | 8 | rows of the `Others` buffer (at least 1) |
| `runners_still` | bool | false | runners do not move (a check that chasers learn) |
| `approach_reward` | float | 0 | a chaser's reward per metre it comes closer to the nearest runner |

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [PursuitField](../components/PursuitField.md) | context | readwrite+create | preinit (create), tick | parameters; `catches` and `births` counted |
| [AgentsReset](../components/AgentsReset.md) | context | write+create | preinit | created when missing, so that the host sees an agent world |
| [PursuitDot](../components/PursuitDot.md) | component | readwrite+create+destroy | init (populate), tick | the dots' state; runners destroyed and born |
| [AgentIO](../components/AgentIO.md) | component | readwrite+create+destroy | init (populate), tick | one agent per dot: configured at birth, actions read, cleared for a chaser's new episode |
| [WorldClock](../components/WorldClock.md) | context | read | tick | window boundaries |
| [WorldRng](../components/WorldRng.md) | context | readwrite | init, tick | places, yaws, phases |

## Side effects

- Entity creation and destruction: every runner's end destroys its entity and creates another.
- No physics, no log.

## Order and dependencies

- After the action slot (`TrainerActions@act`, or `Policy@act` / `Policies` in evaluation) so that the actions of the
  window are written; before `PursuitObserve@observe`, which pays the rewards, ends agents and writes observations.
- `PursuitObserve` sets `ended` and `fresh`, which this system acts on at the start of the next window.

## Used in scenes

<!-- generated -->
- [`common/pursuit_world.json`](../../../resources/scenes/common/pursuit_world.json): `fixed`

## Tests

- `PURSUIT.1` to `PURSUIT.6` in [`tests/unit/pursuit.cpp`](../../../tests/unit/pursuit.cpp)
- `MA.1` to `MA.4` in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp)
- `AP.3` in [`tests/onnx/agent_policy.cpp`](../../../tests/onnx/agent_policy.cpp)
