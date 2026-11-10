#!/usr/bin/env python3
"""Curves and a table of walker runs from their TensorBoard events (pilots, the terrain walker report).

    build/venv/Scripts/python tools/run_curves.py --runs RUN [RUN ...] [--at 1000000,5000000] [--plot OUT.png] [--results build/runs]

Prints, per run, the stats nearest under each step of --at (default: the last step) and the trainer's steps per second
(first to last summary, wall clock). --plot draws the runs together.
"""

import argparse
import glob
import pathlib
import sys

from tensorboard.backend.event_processing.event_accumulator import EventAccumulator

ROOT = pathlib.Path(__file__).resolve().parent.parent
KEYS = [("Death/Dead", "death rate"), ("Environment/Episode Length", "episode length"), ("Environment/Cumulative Reward", "reward"),
        ("Policy/Extrinsic Reward", "extrinsic"), ("Policy/Gail Reward", "GAIL"), ("Policy/GAIL Expert Estimate", "D(expert)"),
        ("Policy/GAIL Policy Estimate", "D(policy)"), ("Root/Root Speed", "root speed"), ("Head/Head Height", "head height"),
        ("Terrain/Route Lost", "route lost"), ("Terrain/Slope Along", "slope along"), ("Terrain/Slope Across", "slope across"),
        ("Terrain/Spawn Draws", "spawn draws")]


def load(run, results, behavior):
    files = sorted(glob.glob(str(pathlib.Path(results) / run / behavior / "events.out.tfevents.*")))
    if not files:
        sys.exit(f"no events for {run}")
    data, wall = {}, {}
    for f in files:
        acc = EventAccumulator(f, size_guidance={"scalars": 0})
        acc.Reload()
        for tag in acc.Tags()["scalars"]:
            for e in acc.Scalars(tag):
                data.setdefault(tag, {})[e.step] = e.value
                wall[e.step] = e.wall_time
    return data, wall


def at(series, step):
    near = [s for s in series if s <= step]
    return series[max(near)] if near else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runs", nargs="+", required=True)
    parser.add_argument("--at", default="")
    parser.add_argument("--behavior", default="HeidelBotBehavior")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    parser.add_argument("--plot")
    parser.add_argument("--list-keys", action="store_true")
    args = parser.parse_args()

    runs = {r: load(r, args.results, args.behavior) for r in args.runs}
    if args.list_keys:
        for r, (data, _) in runs.items():
            print(r, sorted(data))
        return
    steps = [int(float(s)) for s in args.at.split(",") if s]
    names = {k: label for k, label in KEYS}
    print("| run | step | " + " | ".join(label for _, label in KEYS) + " | steps/s |")
    print("|---|---|" + "---|" * (len(KEYS) + 1))
    for r, (data, wall) in runs.items():
        last = max(data.get("Environment/Episode Length", {0: 0}))
        first = min(wall) if wall else 0
        for step in (steps or [last]):
            if step > last + 100000:
                continue
            cells = []
            for k, _ in KEYS:
                v = at(data.get(k, {}), step)
                cells.append("" if v is None else f"{v:.3g}")
            near = [s for s in wall if s <= step]
            rate = (max(near) - first) / (wall[max(near)] - wall[first]) if near and wall[max(near)] > wall[first] else 0.
            print(f"| {r} | {step / 1e6:.1f}M | " + " | ".join(cells) + f" | {rate:.0f} |")

    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        shown = [k for k, _ in KEYS if any(k in data for data, _ in runs.values())]
        cols = 3
        rows = (len(shown) + cols - 1) // cols
        fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 3 * rows), squeeze=False)
        for ax, k in zip(axes.flat, shown):
            for r, (data, _) in runs.items():
                d = data.get(k, {})
                xs = sorted(d)
                ax.plot([x / 1e6 for x in xs], [d[x] for x in xs], label=r, linewidth=1)
            ax.set_title(names[k])
            ax.set_xlabel("steps (M)")
            ax.grid(alpha=0.3)
        axes.flat[0].legend(fontsize=7)
        for ax in axes.flat[len(shown):]:
            ax.axis("off")
        fig.tight_layout()
        pathlib.Path(args.plot).parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(args.plot, dpi=130)
        print(args.plot)


if __name__ == "__main__":
    main()
