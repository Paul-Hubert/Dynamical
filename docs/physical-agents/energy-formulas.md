# Energy formulas of a long rollout

`mode=eval` with `var.out` writes `energy_formulas` in its summary. `tools/plot_energy_formulas.py` draws the two graphs.
The system is `EnergyFormulas` (`src/logic/agents/humanbot_formulas.cpp`). It reads only, and only in eval scenes
(`view_truncate`).

## The reward

`EnergyProvider` in `RewardsSys` charges the drives' own work. Per joint and per tick, `W = L_motor · ω_rel,mid` with
`L_motor` from `GetTotalLambdaMotor` on Jolt's motor axes, split by sign, summed over the 15 joints and the decision
window. The term is `gate(speed) · exp(−CoT / energyScale)`, `CoT = (W⁺ + W⁻) / T / v`. A torque held at zero joint speed
costs nothing, so the channel is drive work, not torque magnitude. Limits and contacts are not charged.
Unity's DeVita configs charge `energySource 2`, the Newton–Euler joint torque, which also contains the limits.

Two options change the cost: `positiveEnergyMultiplier` / `negativeEnergyMultiplier` (Margaria's muscle efficiencies,
4.0 and 0.8333 for 25 % and −120 %) and `torqueRateWeight`, which adds `weight · Σ_joints Σ_ticks ‖Δτ_motor‖ / T` to the
power (J per N·m of torque change, 0 by default). `training/devita_h173_margaria.yaml` and
`training/devita_h173_torque_rate.yaml` use them; results are in `reports/P4-P13.md`.

## The formulas

All run per physics tick on the state after the step and the start of step state. Ticks in the first
`statsSettleSeconds` of an episode are left out. Per-joint sums are split by sign per joint and per tick, so that generation
at one joint never cancels absorption at another.

| | Full body | DeVita mode |
|---|---|---|
| **1. Internal work** | `Σ_t \|Δ Σ_seg (T − V)\|`, `T = ½mv² + ½ωᵀIω`, `V = m g y_com` (Unity's `LagrangianInternalWork`) | not defined by DeVita |
| **2. Recursive Newton–Euler** | 3D `L_j · ω_rel,mid`, every joint, every tick | `(L_j · ml)(ω_rel,mid · ml)`, hip, knee, ankle of the stance limb, stance only |
| **3. Drive** | 3D `L_motor · ω_rel,mid`, every joint, every tick | `(L_motor · ml)(ω_rel,mid · ml)`, same joints and windows |

**Formula 2.** Leaf to root, on impulses over the step, from the measured kinematics and the ground impulse only:

```
J_j = m Δv − m g dt + Σ_children J_k − J_ground
L_j = I₀ Δω − r_j × J_j + Σ_children (L_k + r_k × J_k) − L_ground
```

`I₀` and the lever arms are at the start of the step, and there is no gyroscopic term, because that is how Jolt integrates.
Unity's `RealizedWork` has a gyroscopic term for PhysX; adding one here would invent torques the simulator never applied.
`J_ground` and `L_ground` are Jolt's solved impulses (patch P-C, `ports/joltphysics/contact-impulse-listener.patch`): the
normal impulse at each point, linear friction at the friction centre, torsional friction about the normal.

**DeVita mode.** `ml = up × heading`. A stance window is Unity's: it opens at the first loaded tick and closes at the last,
with foot normal force ≥ 0.05 m g, 3 ticks of debounce, 0.15–2 s, starting after the settle time. The foot is taken as
loaded from Jolt's normal impulses.

## Verification

Against a 2000-episode rollout of `devita_walk150_20m`, and in the test *Energy formulas…*:

| Check | Result |
|---|---|
| Physics unchanged by patch P-C (walker eval metrics before and after) | identical |
| Jolt's contact impulse against the foot momentum balance | 1.1e-7 |
| Newton–Euler joint force against Jolt's point constraint | 3.9e-7 |
| Newton–Euler joint torque against the motor, no limit active | 6.2e-5 |
| What the recursion leaves on the root, against body weight | 4.6e-7 |
| Internal work against a recomputation from Jolt's own bodies | within 0.5 % |
| `dE` against joint, contact and point work, net (S7.5 on random actions: 0.74 %) | 0.04 % |
| The same, tick by tick | 16 % |

The tick-wise remainder has not been isolated. The position solve moves bodies without doing work, and gravity acts on the
end-of-step velocity, so both are expected to contribute.

## Rollout

```bash
./Release/TrainingEnv.exe scene=walker_eval mode=eval var.body=agents/bodies/heidelbot_notoes_h173.body.json var.params=agents/params/devita_h173.params.json var.view_speed=1.5 var.model=runs/devita_walk150_20m/AnySpeedBotBehavior.onnx var.episodes=2000 var.out=runs/eval/devita_walk150_20m.formulas.json
```

```bash
build/venv/Scripts/python.exe tools/plot_energy_formulas.py build/runs/eval/devita_walk150_20m.formulas.json
```

Graphs are per stance (`stances` in the summary) to compare with DeVita's J per step. They are written as
`<run>_energy_{full_body,stance}.png`.
