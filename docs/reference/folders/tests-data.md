# `tests/data/`

> [Source](../../../tests/data/) · Parent: [`tests/`](tests.md)

## Summary
Fixtures read by the tests and tools by repo-relative path.

## Files
| File | Contents |
|---|---|
| `null_ppo.yaml` | tiny PPO config for `HeidelBotBehavior` against `mode=null` (`tools/train_smoke.py`, `tools/bench_null.py`) |
| `null_walker_ppo.yaml` | the walker's PPO settings without GAIL, for the B3.1 supplementary benchmark against `mode=null` |
| `scene/resources/` | a resources root for loader and world tests (`SceneLoadOptions::resources = "tests/data/scene/resources"`): `scenes/` (`incline`, `vars`, `world_vars`, `worlds`, `common/` includes, `bad/` malformed scenes one per error), `prefabs/` (`base_box`, `crate`), `params/test.params.json` (`"<file>#<key>"` references) |
| `sidechannel/` | `*.bin` side-channel payloads captured from the fork's Python classes (`tools/capture_sidechannels.py`) and `expected.json` |
| `onnx/` | `sphere_roller.onnx` and `sphere_roller.reference.json` (`tools/onnx_reference.py`) |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
`scene/`, `sidechannel/`, `onnx/` (described above; no separate docs).
