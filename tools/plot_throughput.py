#!/usr/bin/env python3
"""P3 visual: trainer steps/s against the number of envs (build/artifacts/P3/bench_null.csv), with the Unity
reference line at 424 steps/s (20 envs). Writes build/artifacts/P3/plots/throughput.png.

    build/venv/Scripts/python tools/plot_throughput.py
"""

import csv
import pathlib
import statistics

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = pathlib.Path(__file__).resolve().parent.parent
UNITY_REFERENCE = 424.0


def main():
    src = ROOT / "build" / "artifacts" / "P3" / "bench_null.csv"
    rows = [r for r in csv.DictReader(open(src)) if r["steps_per_s"] not in ("", "None")]
    envs = sorted({int(r["envs"]) for r in rows})
    runs = {n: [float(r["steps_per_s"]) for r in rows if int(r["envs"]) == n] for n in envs}

    fig, ax = plt.subplots(figsize=(7, 4.5), dpi=120)
    for n in envs:
        ax.scatter([n] * len(runs[n]), runs[n], color="#4878d0", alpha=0.5, s=18, zorder=3)
    means = [statistics.mean(runs[n]) for n in envs]
    sds = [statistics.stdev(runs[n]) if len(runs[n]) > 1 else 0.0 for n in envs]
    ax.errorbar(envs, means, yerr=sds, color="#1f3f8f", marker="o", capsize=4, label="TrainingEnv.exe mode=null (mean ± sd)", zorder=4)
    # Supplementary: the walker's PPO network and batch (bench_null.py --tag _walker), when measured
    walker = ROOT / "build" / "artifacts" / "P3" / "bench_null_walker.csv"
    if walker.exists():
        wrows = [r for r in csv.DictReader(open(walker)) if r["steps_per_s"] not in ("", "None")]
        ax.scatter([int(r["envs"]) for r in wrows], [float(r["steps_per_s"]) for r in wrows], color="#dd8452", marker="s", s=40,
                   label="mode=null, the walker's PPO settings without GAIL", zorder=4)
    ax.axhline(UNITY_REFERENCE, color="#c44e52", linestyle="--", label="Unity build, 20 envs (≈424 steps/s)")
    ax.set_xlabel("envs (TrainingEnv.exe processes, 25 agents each)")
    ax.set_ylabel("trainer steps/s")
    ax.set_title("Trainer throughput against the null environment (B3.1)")
    ax.set_xticks(envs)
    ax.set_ylim(bottom=0)
    ax.grid(alpha=0.3)
    ax.legend(loc="center", fontsize=8)
    out = ROOT / "build" / "artifacts" / "P3" / "plots"
    out.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out / "throughput.png")
    print(out / "throughput.png")


if __name__ == "__main__":
    main()
