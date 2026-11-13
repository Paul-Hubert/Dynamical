# Reference: systems, components, folders

Plain Markdown (GitHub flavoured, relative links only, no HTML) so that it exports as is: GitHub,
MkDocs (`mkdocs.yml` at the root of the repository, `mkdocs build`), Pandoc (`pandoc -s` per file).
It describes the code as it is; the design docs one level up (`docs/*.md`) say why.

| Part | What | Written by |
|---|---|---|
| [systems/](systems/index.md) | one page per registered system (the name scenes use) and per render pass | by hand, from the code |
| [components/](components/index.md) | one page per component and context variable: what it holds, **every system that touches it**, read / write / readwrite, side effects | generated from `data/*.json` |
| [folders/](folders/index.md) | one page per folder of `src/`: what is in it, its files, its systems and components | by hand |
| `data/*.json` | the access tables of every system (the source of the component pages) | by hand, with the system pages |

## Updating

1. Change the system page and its entry in `data/<area>.json` together when a system changes.
2. `python tools/gen_reference_docs.py` rewrites `components/`, the three `index.md` and the scene usage of the systems.
3. `python tools/gen_reference_docs.py --check` lists what the static scan (`tools/docs_scan.py`) sees a system touch that its table does not say, and registered systems without a page. Fix the table (or the page), not the scan.

## Words

- **read**: the system only looks (a `const` view, `get` used for reading, `all_of`, `find` without writing).
- **write**: the system sets it without reading what was there (`emplace`, `emplace_or_replace`, `replace`, overwrite of every field, `insert_or_assign`).
- **readwrite**: both (a mutable view whose values are updated from what they were, `patch`).
- **create** / **destroy**: the system adds the component to entities (or makes the entities) / removes it (or destroys them). Listed with the access that goes with it.
- **context**: a registry context variable (`reg.ctx()`), one per registry.
- **Scope** of a system: `Host` (the main registry), `World` (a training or physics world's registry, many per process), `Both`.
- **Lane**: where scenes put it: `host.pre`, `host.post`, a world's `fixed` list; `@act` runs on the first tick of a decision window, `@observe` on the last.
- **Side effect**: anything else than a component or context variable: physics bodies, GPU work, files, the network, the log, the window, entity creation, signals connected.

## System page template

```markdown
# <RegistryName>

`<Class>` in [`src/.../file.cpp`](../../../src/.../file.cpp) · Scope: Host|World|Both · Needs renderer: yes|no
· Lanes: <where scenes put it> · Parameters: yes|no

<One paragraph: what it is for.>

## Behaviour
<preinit / init / tick / finish, in order, what each does, what it expects to find, what it guarantees after.>

## Parameters
| Key | Type | Default | Meaning |

## Components and context
| Name | Kind | Access | Where | What for |
(Kind: component / context / tag; Access: read / write / readwrite, + create / destroy)

## Side effects
- ...

## Order and dependencies
<What must run before / after, and why.>

## Used in scenes
<!-- generated -->

## Tests
```

## Data file (`data/<area>.json`)

```json
{
  "systems": [{
    "name": "Forest", "class": "ForestSys", "file": "src/logic/vegetation/forest.cpp",
    "kind": "system",            // or "pass" (render pass), "service" (renderer/game object that touches the registry)
    "scope": "Host", "summary": "one line",
    "access": [
      { "name": "ChunkVolume", "kind": "component", "access": "read", "where": "tick",
        "effect": "what it does with it, one sentence" }
    ],
    "side_effects": ["one sentence each"]
  }],
  "components": [{
    "name": "Plant", "kind": "component", "file": "src/logic/vegetation/vegetation.h",
    "summary": "one line", "json": "Plant or null (name in scenes and prefabs)",
    "fields": [{ "name": "species", "type": "int", "desc": "..." }],
    "notes": "lifetime, invariants, who owns it"
  }]
}
```
