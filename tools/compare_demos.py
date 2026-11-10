#!/usr/bin/env python3
"""Compares two walker .demo files observation component by component, and checks each for jumps between frames.

    build/venv/Scripts/python tools/compare_demos.py REFERENCE.demo OTHER.demo [--body heidelbot_notoes] [--out report.md]

The layout is heidelbot_notoes_2: 3 header values (speed difference forward, head error, speed difference sideways),
then per joint the child's two Unity axes and its anchor velocity, each in the heading frame of the joint's frame
segment; two frames stacked, the newer one second. Statistics are over the newer frame of every decision.

A component is listed as different when its ranges do not overlap by half, or its means are further apart than the
larger standard deviation. Jumps: the largest change of an axis (a unit vector) and of an anchor velocity between two
consecutive decisions of one episode.
"""

import argparse
import json
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
FRAME = 138


def load(path):
    from mlagents.trainers.demo_loader import load_demonstration
    _, pairs, _ = load_demonstration(str(path))
    obs = np.array([p.agent_info.observations[0].float_data.data for p in pairs], dtype=np.float32)
    done = np.array([p.agent_info.done for p in pairs], dtype=bool)
    actions = np.array([list(p.action_info.continuous_actions) for p in pairs], dtype=np.float32)
    return obs, done, actions


def names(body):
    joints = [j["name"] for j in json.loads((ROOT / "resources" / "agents" / "bodies" / f"{body}.body.json").read_text())["joints"]]
    out = ["header speed difference forward", "header head error", "header speed difference sideways"]
    for j in joints:
        for group in ("axis", "secondary axis", "anchor velocity"):
            for c in "xyz":
                out.append(f"{j} {group} {c}")
    return joints, out


def describe(obs, done, joints):
    frame = obs[:, FRAME:]
    older = obs[:, :FRAME]
    starts = np.concatenate(([True], done[:-1]))
    stats = {"decisions": len(obs), "episodes": int(done.sum()),
             "header_x_zero": bool(np.all(frame[:, 0] == 0)), "header_z_zero": bool(np.all(frame[:, 2] == 0)),
             "empty_stack_at_starts": float(np.mean(np.all(older[starts] == 0, axis=1))),
             "stack_follows": float(np.mean(np.all(older[~starts] == frame[:-1][~starts[1:]], axis=1))) if len(obs) > 1 else 1.0}
    inside = ~starts[1:]
    delta = frame[1:] - frame[:-1]
    axis_jump, velocity_jump, speed = {}, {}, {}
    for k, j in enumerate(joints):
        base = 3 + 9 * k
        a = np.maximum(np.linalg.norm(delta[:, base:base + 3], axis=1), np.linalg.norm(delta[:, base + 3:base + 6], axis=1))
        v = np.linalg.norm(delta[:, base + 6:base + 9], axis=1)
        axis_jump[j] = float(a[inside].max()) if inside.any() else 0.
        velocity_jump[j] = float(v[inside].max()) if inside.any() else 0.
        speed[j] = float(np.linalg.norm(frame[:, base + 6:base + 9], axis=1).max())
    stats["axis_jump"], stats["velocity_jump"], stats["anchor_speed"] = axis_jump, velocity_jump, speed
    return frame, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference")
    parser.add_argument("other")
    parser.add_argument("--body", default="heidelbot_notoes")
    parser.add_argument("--out")
    args = parser.parse_args()

    joints, component = names(args.body)
    lines = []
    frames = []
    for label, path in (("reference", args.reference), ("other", args.other)):
        obs, done, actions = load(path)
        if obs.shape[1] != 2 * FRAME:
            sys.exit(f"{path}: {obs.shape[1]} observations, expected {2 * FRAME}")
        frame, s = describe(obs, done, joints)
        frames.append(frame)
        worst_axis = max(s["axis_jump"], key=s["axis_jump"].get)
        worst_velocity = max(s["velocity_jump"], key=s["velocity_jump"].get)
        fastest = max(s["anchor_speed"], key=s["anchor_speed"].get)
        lines += [f"## {label}: `{pathlib.Path(path).name}`", "",
                  f"- {s['decisions']} decisions, {s['episodes']} episodes; actions all zero: {bool(np.all(actions == 0))}",
                  f"- header: speed difference zeroed forward {s['header_x_zero']}, sideways {s['header_z_zero']}; head error {frame[:, 1].min():.3f} to {frame[:, 1].max():.3f}",
                  f"- stack: empty older half at {100 * s['empty_stack_at_starts']:.0f} % of the episode starts; the older half is the previous frame at {100 * s['stack_follows']:.1f} % of the other decisions",
                  f"- largest change of an axis between two decisions: {s['axis_jump'][worst_axis]:.3f} ({worst_axis}); of an anchor velocity: {s['velocity_jump'][worst_velocity]:.2f} m/s ({worst_velocity})",
                  f"- fastest anchor: {s['anchor_speed'][fastest]:.2f} m/s ({fastest})", ""]

    a, b = frames
    lines += ["## Components whose range or mean differs", "",
              "| Component | reference min | max | mean | std | other min | max | mean | std |", "|---|---|---|---|---|---|---|---|---|"]
    different = 0
    for i, name in enumerate(component):
        x, y = a[:, i], b[:, i]
        lo, hi = max(x.min(), y.min()), min(x.max(), y.max())
        widest = max(x.max() - x.min(), y.max() - y.min(), 1e-6)
        overlap = max(hi - lo, 0.) / widest
        apart = abs(x.mean() - y.mean()) > max(x.std(), y.std(), 0.02)
        if overlap < 0.5 or apart:
            different += 1
            lines.append(f"| {name} | {x.min():.3f} | {x.max():.3f} | {x.mean():.3f} | {x.std():.3f} | {y.min():.3f} | {y.max():.3f} | {y.mean():.3f} | {y.std():.3f} |")
    lines += ["", f"{different} of {len(component)} components differ.", ""]

    lines += ["## Per joint", "", "| Joint | axis: mean distance of the means | anchor speed mean reference | other | anchor speed max reference | other |", "|---|---|---|---|---|---|"]
    for k, j in enumerate(joints):
        base = 3 + 9 * k
        gap = float(np.linalg.norm(a[:, base:base + 6].mean(axis=0) - b[:, base:base + 6].mean(axis=0)))
        sa, sb = np.linalg.norm(a[:, base + 6:base + 9], axis=1), np.linalg.norm(b[:, base + 6:base + 9], axis=1)
        lines.append(f"| {j} | {gap:.3f} | {sa.mean():.2f} | {sb.mean():.2f} | {sa.max():.2f} | {sb.max():.2f} |")

    text = "\n".join(lines) + "\n"
    if args.out:
        pathlib.Path(args.out).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
