# `training/`

> [Source](../../../training/) · Parent: repository root

## Summary
ML-Agents trainer configurations (YAML, `behaviors:` keyed by behaviour name) for the agent scenes run by `dynamical_train mode=train`. They are passed to the fork's `mlagents-learn` by `tools/train_sphere.py`, which sets `max_steps` (and `init_path` with `--init-path`) and the env arguments `scene=<scene> mode=train`.

## Files
| File | Contents |
|---|---|
| `sphere_roller_ppo.yaml` | PPO for `sphere_roll`, behaviour `SphereRoller`: 16 observations in the goal frame, 3 continuous actions (torque), 15 decisions/s, 600-decision episodes ended by truncation |
| `wheel_roller_ppo.yaml` | PPO for `wheel_roll`, behaviour `WheelRoller`: the same task on a sphere of the walker's hip height (0.94 m, 69 kg) |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
None.
