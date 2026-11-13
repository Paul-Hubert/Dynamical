# SandboxHeuristic

`SandboxHeuristicSys` in [`src/logic/sandbox/sandbox.cpp`](../../../src/logic/sandbox/sandbox.cpp) · Scope: World · Needs renderer: no
· Lanes: a world's `fixed` list, meant for the Act phase (`SandboxHeuristic@act`); no scene uses it, the
`heuristic` parameter of [Sandbox](Sandbox.md) runs the same code · Parameters: no

The scripted minds of the sandbox as a system: every mind ([SandboxMind](../components/SandboxMind.md) +
[AgentIO](../components/AgentIO.md)) that asked for a decision and is not done gets the action of
`sandboxHeuristic`, computed from its observation alone. For scenes without a policy for the minds and for tests.

## Behaviour

**tick**: for every mind entity with `requested` and not `done`, whose animal still has its
[SandboxAnimal](../components/SandboxAnimal.md): `sandboxHeuristic(species, io)`, then `requested = false` and the
stats cleared (as a host answering it would).

`sandboxHeuristic` (actions: speed, turn, head yaw, head pitch; mode branch):
- **Grazers**: a predator seen within 35 % of `sight`: flee at full speed. Hungry (energy < 0.85): feed (Feed
  mode, head down, standing) where the grass under the muzzle is over 0.12 m, else walk at 0.45 to the tallest of
  the 16 grass samples over 0.1 m, else toward the middle of the arena at 0.5.
- **Predators**, hungry (< 0.9): a carcass seen within 1.2 m: feed, the head turned to it; farther: go to it at
  0.6; else chase the nearest prey at full speed, head on it, Attack mode within 2.5 m; else wander at 0.5.
- Fed: an adult, not pregnant, with energy over `mate_energy + 0.1`, goes to the nearest of its kind of the other
  sex at 0.5 and stands in Mate mode within 1.2 x `mate_reach` m; otherwise Rest.
- Turns are 1.5 x the angle, clamped; a turn sharper than 1 rad slows to 0.4.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| [Sandbox](../components/Sandbox.md) | context | read | tick | the species of the animal a mind is for |
| [SandboxMind](../components/SandboxMind.md) | component | read | tick | the mind's animal |
| [SandboxAnimal](../components/SandboxAnimal.md) | component | read | tick | its species |
| [AgentIO](../components/AgentIO.md) | component | readwrite | tick | observation and `Interests` rows read; action, discrete mode written; `requested` cleared, stats cleared |

## Side effects

- None.

## Order and dependencies

- After the host's actions and the scene's `Policies@act` (so it only answers what they left), before
  [Sandbox](Sandbox.md) reads the minds' actions in the same tick; needs the Sandbox context.

## Used in scenes
<!-- generated -->

## Tests

- `SANDBOX.9` in [`tests/unit/sandbox.cpp`](../../../tests/unit/sandbox.cpp) runs the scripted minds (through
  `sandbox_heuristic`, i.e. the Sandbox `heuristic` parameter): deer graze and breed, wolves kill and feed.
