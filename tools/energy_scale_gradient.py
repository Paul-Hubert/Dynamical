"""The energyScale that gives the energy reward its largest gradient, from a stochastic rollout's per-decision energy.

    build/Release/TrainingEnv.exe scene=walker_eval mode=eval ... var.stochastic=1 var.cot_out=runs/eval/<run>.cot.bin
    build/venv/Scripts/python.exe tools/energy_scale_gradient.py build/runs/eval/<run>.cot.bin --config build/runs/<run>.cfg.yaml

The reward per decision is r = g(v) exp(-c / s), with c = (m+ P+ + m- P- + w dtau) / clamp(v, floor, max(floor, target))
(RewardsSys). If the gait makes every decision's energy a fraction eps cheaper, the mean reward changes by
eps E[g (c/s) exp(-c/s)]: that sensitivity is what the scale s should maximise. For a single c it peaks at s = c with the
value 1/e; over the rollout's spread of c it peaks near the median. Also printed: the same per unit of absolute CoT
(dr/dc, which favours small s), and the sensitivity over the reward's standard deviation across decisions.
"""
import argparse
import re
from pathlib import Path

import numpy as np


def env_param(text, key, default):
    m = re.search(rf"^\s*{key}:\s*([-0-9.eE+]+)", text, re.M)
    return float(m.group(1)) if m else default


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cot", type=Path)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    cfg = args.config.read_text()
    pos_m = env_param(cfg, "positiveEnergyMultiplier", 1.0)
    neg_m = env_param(cfg, "negativeEnergyMultiplier", 1.0)
    w = env_param(cfg, "torqueRateWeight", 0.0)
    scale = env_param(cfg, "energyScale", 0.0)
    floor = env_param(cfg, "energyMinSpeedFloor", 0.5)
    edge = env_param(cfg, "energySpeedEdge", 0.35)

    d = np.fromfile(args.cot, dtype=np.float32).reshape(-1, 5).astype(np.float64)
    pos, neg, tau, v, target = d.T
    denominator = np.clip(v, floor, np.maximum(floor, target))
    c = (pos_m * pos + neg_m * neg + w * tau) / denominator
    g = np.exp(-(v - target) ** 2 / (2 * edge * edge))
    q = np.percentile(c, [5, 25, 50, 75, 95])
    print(f"{len(c):,} decisions; CoT mean {c.mean():.0f}, percentiles 5/25/50/75/95: " + " / ".join(f"{x:.0f}" for x in q))
    print(f"gate mean {g.mean():.3f}; config energyScale {scale:g}")

    def stats(s):
        e = np.exp(-c / s)
        r = g * e
        sens = (g * (c / s) * e).mean()          # d mean r / d eps, energy x (1 - eps)
        abs_grad = (g * e / s).mean()             # d mean r / d c, per J/m
        return r.mean(), sens, abs_grad, sens / r.std()

    scales = np.unique(np.concatenate([np.geomspace(q[0] / 2, q[4] * 2, 400), [scale] if scale else []]))
    rows = np.array([stats(s) for s in scales])
    best = scales[rows[:, 1].argmax()]
    best_snr = scales[rows[:, 3].argmax()]
    print(f"\nbest scale for the proportional gradient: {best:.0f} (sensitivity {rows[:, 1].max():.4f}, 1/e = {1 / np.e:.4f} at most)")
    print(f"best scale for gradient / reward std:     {best_snr:.0f}")
    print(f"\n{'scale':>8} {'mean r':>8} {'dr/deps':>8} {'dr/dc x1e4':>11} {'/std':>6}")
    for s in sorted({round(x) for x in [best, best_snr, scale, q[1], q[2], q[3], c.mean(), 2 * best, best / 2] if x}):
        m, sens, a, snr = stats(s)
        print(f"{s:8.0f} {m:8.3f} {sens:8.4f} {a * 1e4:11.3f} {snr:6.3f}")


if __name__ == "__main__":
    main()
