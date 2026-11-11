# TrainerActionsSys

> **Kind:** system · **Registry name:** `TrainerActions` · **Scope:** World · **Lane:** Fixed (Phase Act) · **Needs renderer:** no
> **Source:** [`src/logic/agents/sphere_roller.h`](../../../src/logic/agents/sphere_roller.h)

## Purpose
The action slot of training templates (`TrainerActions@act` in `sphere_world` and `sphere_world_hills`): a deliberate no-op, since the training host writes `AgentIO::action` itself between decision windows. Viewer and evaluation scenes replace it with `Policy@act` or `SphereHeuristic@act` through `systems.replace`.

## Lifecycle
- **Construction / params:** `TrainerActionsSys(reg)`, no params.
- **preinit:** None.
- **init:** None.
- **tick(h):** empty (defined inline in the header).
- **finish:** None.

## Expected behavior
- Has no effect on the world; `AgentIO::action` is whatever the host wrote (zeros after a reset).
- Its presence fixes the position of the Act slot in the lane, so `replace` can swap in a real action source at the same place.

## Components and context variables touched
| Component / context var | Registry | Access | Where (phase) | Details |
|---|---|---|---|---|
| None | | | | |

## Side effects
None.

## Ordering and dependencies
- Sits before `SphereControl` in the fixed lane, as every action source must.

## Related
- [SphereHeuristicSys](SphereHeuristicSys.md), [PolicySys](PolicySys.md), [SphereControlSys](SphereControlSys.md)
- [AgentIO](../components/AgentIO.md)
