# WorldsEnvironment

`WorldsEnvironment` in [`src/train/worlds_env.cpp`](../../../src/train/worlds_env.cpp) · Scope: World (every world's
registry, from the training host) · Needs renderer: no · Lanes: none (a service of `dynamical_train`, mode=train) ·
Parameters: no

The trainer environment of `mode=train` on a scene of agent worlds ([TrainHost](service-TrainHost.md) makes it when
the trainer connects). It serves the ML-Agents trainer's RESET and STEP over every world of the scene: a world's
agents are its [AgentIO](../components/AgentIO.md) context variable (one agent per world, a decision every window:
sphere roller, walkers) and every entity with an AgentIO component (agents of several behaviours that are born, die
and ask for decisions at their own rate: pursuit, the [sandbox](Sandbox.md);
[action agents](../../action-agents.md)). Unity's semantics: the trainer only hears from the environment when an
agent asks for a decision or ends; a done agent gets no action.

## Behaviour

**check(runner)** (static, used by TrainHost before serving): empty when every world has an agent (an AgentIO
context with a behaviour, an [AgentsReset](../components/AgentsReset.md), or AgentIO components, all with a
behaviour), the agents of a behaviour share a layout (observation size, sensors, actions, branches), and every
world has world 0's decision period; otherwise the reason.

**behaviors(runner)** (static): the behaviour names of the agents as they stand, sorted (TrainHost names the first
in envparams.txt). **describe()**: "4 Chaser (6 + 5x6 observations, 2 + [2] actions), ..." for the log.

**constructor**: a `tf::Executor` of `threads` named workers when threads > 1; the period = world 0's
`decision_period`; component ids start after the world indices.

**reset()** (RESET): forgets pending and sent agents, sets `reset_requested` on every world's AgentIO context and
`requested` on its AgentsReset, runs every world's Observe phase alone (`SystemSet::fixed_observe_only`, h = 0),
then gathers.

**gather()**: per world, the AgentIO context (id = world index) and the AgentIO components that ask (`requested`)
or ended (`done`), in entity order: each gets an id the first time (kept over its episodes), `requested` is
cleared, and it joins its behaviour's pending list. The first agent of a behaviour fixes its layout; another
layout is an error for the next step.

**step(input)** (STEP): for each behaviour, the trainer must send one action per agent of the last output that did
not end, in order; continuous and discrete sizes must be the behaviour's, discrete choices within their branch.
Actions are written into the agents (done ones get zeros). An agent that left its world without a terminal step is
an error. Then the worlds step one window at a time (`ScenarioRunner::step(period, executor)`, in parallel) and
gather, until some agent is pending (at most 100000 windows).

**write(output)**: per behaviour, an AgentInfo per pending agent (id, reward, done, max step reached = done and
truncated, the vector observation named `sensor_name`, then each sensor: buffers as [rows, size] with variable
length along the rows); the agents sent and which ended are kept for the next STEP. A component's stats are
cleared once sent; the world agent's are rewritten by its systems every window. Brain parameters go with the first
output that has an agent of the behaviour. Stats are averaged per key over the agents of the window and sent in the
stats side channel with their aggregation.

## Parameters

None (threads from `threads=` of the run arguments, 1 by default, through TrainHost).

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [AgentIO](../components/AgentIO.md) | context | readwrite | check, behaviors, describe, gather, step, write, reset | the world's agent: layout checked, id = world index, actions written, observation, reward and stats read, `reset_requested` set on RESET |
| [AgentIO](../components/AgentIO.md) | component | readwrite | check, behaviors, describe, gather, step, write | component agents: layout checked, id given once, `requested` cleared when sent, actions and discrete choices written (zeros for a done one), observation, sensors, reward, done read, stats read and cleared |
| [AgentsReset](../components/AgentsReset.md) | context | readwrite | check, reset | a world with it counts as a world of agents; RESET sets `requested` |
| [WorldClock](../components/WorldClock.md) | context | read | check, constructor | every world must share world 0's decision period, the step length |

## Side effects

- Owns a tf::Executor of `threads` named workers (threads > 1) that steps the worlds in parallel (ScenarioRunner::step).
- On RESET runs every world's Observe phase alone (SystemSet::fixed_observe_only).
- Writes the ML-Agents protobuf output: AgentInfos per behaviour, brain parameters with a behaviour's first agents, averaged stats in the stats side channel.

## Order and dependencies

- The worlds' Observe systems set `requested` / `done` and the observations; their Act systems (`TrainerActions@act`)
  apply the actions it wrote. A world's systems handle `reset_requested` / AgentsReset in their Observe phase.
- Systems that destroy an agent must let it send a done step first (as [Sandbox](Sandbox.md) does with
  `reported`), else STEP fails.

## Used in scenes
<!-- generated -->

## Tests

- `MA.1` to `MA.5` in [`tests/protocol/multi_env.cpp`](../../../tests/protocol/multi_env.cpp): the mock trainer on
  pursuit (two behaviours, agents that end and are born, runners at half the rate), a second RESET, a missing
  action, the check and description, `sandbox_train` (four behaviours).
- `SR.9`, `SR.10` in [`tests/protocol/sphere_env.cpp`](../../../tests/protocol/sphere_env.cpp) (sphere_roll, a scene
  without agents refused); `SR.12` in [`tests/unit/sphere_roller.cpp`](../../../tests/unit/sphere_roller.cpp).
