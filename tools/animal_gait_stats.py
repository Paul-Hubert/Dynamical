#!/usr/bin/env python3
"""How an animal walks in a replay of its segments: the height of its trunk, how often and how high each foot is
lifted, the speed along its path (docs/physical-agents/reports/animal-agents.md). The same measure for a policy
(mode=eval var.replay=<prefix>, tools/animal_replay_video.py keeps one) and for the procedural walk of the
demonstrations (scene animal_demo, mode=record-demo var.replay=<prefix>), to set the two side by side.

    build/venv/Scripts/python tools/animal_gait_stats.py --body <body.json> --replay <prefix> [--replay <prefix> ...]

A foot is lifted when its segment's origin is more than 1.5 % of the animal's height over where it is in the standing
pose; a lift is counted when it leaves that band. Walks of a recording follow each other in a replay: the jump from
one to the next is left out of the speed.
"""

import argparse
import json
import pathlib

import numpy as np


def stats(body, prefix):
    header = json.loads(pathlib.Path(str(prefix) + ".replay.json").read_text())
    data = np.fromfile(str(prefix) + ".replay.bin", "<f4").reshape(header["frames"], header["segments"], 7).astype(np.float64)
    dt = header["dt"]
    feet = [i for i, s in enumerate(body["segments"]) if s["role"] == "foot"]
    stand = np.array([body["segments"][i]["spawn_position"][1] for i in feet])
    band = 0.015 * body["height"]
    height = data[:, feet, 1] - stand
    lifted = height > band
    lifts = (np.diff(lifted.astype(int), axis=0) == 1).sum(axis=0)
    seconds = header["frames"] * dt
    step = np.linalg.norm(np.diff(data[:, 0, [0, 2]], axis=0), axis=1)
    # A new walk or a new episode starts somewhere else
    moving = step < 5 * np.median(step) + 1e-6
    out = {
        "replay": str(prefix), "what": header.get("model", ""), "seconds": seconds,
        "trunk_height": float(np.mean(data[:, 0, 1]) / body["stand_height"]),
        "trunk_height_range": [float(np.min(data[:, 0, 1]) / body["stand_height"]), float(np.max(data[:, 0, 1]) / body["stand_height"])],
        "speed": float(step[moving].sum() / (moving.sum() * dt)),
        "lifts_per_second": {body["segments"][i]["name"]: float(n / seconds) for i, n in zip(feet, lifts)},
        "lift_height_over_height": {body["segments"][i]["name"]: float(np.percentile(height[:, k], 99) / body["height"]) for k, i in enumerate(feet)},
        "time_lifted": {body["segments"][i]["name"]: float(lifted[:, k].mean()) for k, i in enumerate(feet)},
    }
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--body", required=True)
    parser.add_argument("--replay", action="append", required=True)
    parser.add_argument("--out")
    args = parser.parse_args()
    body = json.loads(pathlib.Path(args.body).read_text())
    rows = [stats(body, prefix) for prefix in args.replay]
    for r in rows:
        lifts = ", ".join(f"{k} {v:.2f}" for k, v in r["lifts_per_second"].items())
        high = ", ".join(f"{100 * v:.1f}" for v in r["lift_height_over_height"].values())
        up = ", ".join(f"{100 * v:.0f}" for v in r["time_lifted"].values())
        print(f"{pathlib.Path(r['replay']).parent.name}/{pathlib.Path(r['replay']).name} ({r['what'][-40:]}): {r['seconds']:.0f} s, trunk {r['trunk_height']:.3f} of its standing height "
              f"({r['trunk_height_range'][0]:.2f} to {r['trunk_height_range'][1]:.2f}), {r['speed']:.2f} m/s, lifts per second: {lifts}; "
              f"lift height (% of the height, 99th percentile): {high}; time lifted (%): {up}")
    if args.out:
        pathlib.Path(args.out).write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
