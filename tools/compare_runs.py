#!/usr/bin/env python3
"""Compares a walker run against the Unity reference at matched trainer steps, from their TensorBoard events.

    build/venv/Scripts/python tools/compare_runs.py --ref unity_ref_walk136_40m --run walker_walk136_40m_a [--every 2000000]

Stats: dead rate (Death/Dead), episode length, root speed, head height, cumulative reward. The Unity side's run has demo
velocity spikes (docs/physical-agents/humanbot-port-review.md), which the engine's does not. Reports the missing stat keys
too (the engine's must include every Unity key). Writes a CSV and a PNG next to the --run events.
"""

import argparse
import csv
import glob
import pathlib
import sys

from tensorboard.backend.event_processing.event_accumulator import EventAccumulator

ROOT = pathlib.Path(__file__).resolve().parent.parent
KEYS = ["Death/Dead", "Environment/Episode Length", "Root/Root Speed", "Head/Head Height", "Environment/Cumulative Reward"]


def load(run, results, behavior):
    files = sorted(glob.glob(str(pathlib.Path(results) / run / behavior / "events.out.tfevents.*")))
    if not files:
        sys.exit(f"no events for {run}")
    data = {}
    tags = set()
    for f in files:
        acc = EventAccumulator(f, size_guidance={"scalars": 0})
        acc.Reload()
        tags |= set(acc.Tags()["scalars"])
        for tag in acc.Tags()["scalars"]:
            data.setdefault(tag, {}).update({e.step: e.value for e in acc.Scalars(tag)})
    return data, tags


def at(series, step):
    near = [s for s in series if s <= step]
    return series[max(near)] if near else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--every", type=int, default=2_000_000)
    parser.add_argument("--behavior", default="HeidelBotBehavior")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    args = parser.parse_args()

    ref, ref_tags = load(args.ref, args.results, args.behavior)
    run, run_tags = load(args.run, args.results, args.behavior)

    ignored = {t for t in ref_tags if t.startswith("Environment/Lesson Number")}
    missing = sorted((ref_tags - ignored) - run_tags)
    print(f"Unity keys {len(ref_tags)}, run keys {len(run_tags)}, Unity keys missing from the run: {len(missing)}")
    for k in missing[:40]:
        print("  ", k)

    last = min(max(ref["Environment/Episode Length"]), max(run["Environment/Episode Length"]))
    rows = []
    step = args.every
    while step <= last:
        row = {"step": step}
        for k in KEYS:
            row["ref " + k] = at(ref.get(k, {}), step)
            row["run " + k] = at(run.get(k, {}), step)
        rows.append(row)
        step += args.every
    out = pathlib.Path(args.results) / f"{args.run}_compare.csv"
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["step"])
        w.writeheader()
        w.writerows(rows)
    print(f"{out}: {len(rows)} steps up to {last}")
    if rows:
        keep = rows[-1]
        for k in KEYS:
            print(f"at {keep['step']}: {k}: Unity {keep['ref ' + k]}, run {keep['run ' + k]}")

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(2, 3, figsize=(13, 7))
        for ax, k in zip(axes.flat, KEYS):
            for name, data, c in (("Unity", ref, "tab:blue"), ("engine", run, "tab:orange")):
                d = data.get(k, {})
                xs = sorted(d)
                ax.plot([x / 1e6 for x in xs], [d[x] for x in xs], label=name, color=c)
            ax.set_title(k)
            ax.set_xlabel("steps (M)")
            ax.legend()
        fig.tight_layout()
        fig.savefig(out.with_suffix(".png"), dpi=130)
    except Exception as e:
        print("plot skipped:", e)


if __name__ == "__main__":
    main()
