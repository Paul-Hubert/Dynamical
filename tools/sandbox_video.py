#!/usr/bin/env python3
"""The sandbox from above (docs/action-agents.md): an MP4 of a replay that TrainingEnv.exe mode=eval wrote on a scene
of sandbox worlds (var.replay=<prefix>: <prefix>.sandbox.json, .sandbox.bin, .grass.bin).

    build/RelWithDebInfo/TrainingEnv.exe scene=sandbox_eval mode=eval var.models=build/runs/sandbox_r1 var.seconds=300 var.replay=build/runs/sandbox_r1_replay
    build/venv/Scripts/python tools/sandbox_video.py --replay build/runs/sandbox_r1_replay --out build/runs/sandbox_r1.mp4 [--speed 4] [--seconds 120]

Every animal is an arrow where it heads, with the wedge of where its head looks: deer brown, wolves grey, the size of
the animal; red while it attacks, green while it feeds, pink while it mates, pale while it rests; a cross for a
carcass. Its store is the blue bar under it, its health a red one when it is wounded. The grass is the field as it stood at the end of the replay (grazed cells
lighter than their neighbours). Animals are drawn larger than life (--zoom). --speed plays the replay faster than it happened.
"""

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

import imageio_ffmpeg
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

RECORD = 12
MODE_COLOURS = {0: None, 1: "#2e9c3f", 2: "#d2262b", 3: "#e377c2", 4: "#c9c9c9"}


def read_frames(prefix):
    header = json.loads(pathlib.Path(str(prefix) + ".sandbox.json").read_text())
    data = np.fromfile(str(prefix) + ".sandbox.bin", dtype=np.float32)
    frames, at = [], 0
    while at < len(data):
        count = int(data[at])
        frames.append(data[at + 1: at + 1 + count * RECORD].reshape(count, RECORD))
        at += 1 + count * RECORD
    return header, frames


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--replay", required=True, help="the prefix given to var.replay")
    parser.add_argument("--out", required=True)
    parser.add_argument("--speed", type=float, default=4.0, help="times faster than it happened")
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--seconds", type=float, help="of the replay, from its start (default: all of it)")
    parser.add_argument("--start", type=float, default=0.0, help="seconds of the replay to skip")
    parser.add_argument("--size", type=int, default=900, help="pixels a side")
    parser.add_argument("--zoom", type=float, default=2.5, help="animals are drawn this many times their size")
    args = parser.parse_args()

    header, frames = read_frames(args.replay)
    if not frames:
        sys.exit("no frame in the replay")
    dt, arena = header["dt"], header["arena"]
    species = header["species"]
    side = header["grass_side"]
    grass = np.fromfile(str(args.replay) + ".grass.bin", dtype=np.float32).reshape(2, side, side)
    full, now = grass[0], grass[1]

    first = int(args.start / dt)
    last = len(frames) if args.seconds is None else min(len(frames), first + int(args.seconds / dt))
    step = args.speed / (args.fps * dt)        # replay frames per video frame
    picks = [int(first + i * step) for i in range(int((last - first) / step))]
    if not picks:
        sys.exit("nothing to draw in that span")

    work = pathlib.Path(tempfile.mkdtemp(prefix="sandbox_video_"))
    fig = plt.figure(figsize=(args.size / 100, args.size / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    # Bare ground, then grass by how tall it stands: grazed cells come out darker than their neighbours
    ground = np.zeros((side, side, 3))
    ground[:] = (0.80, 0.74, 0.60)
    has = full > 0
    green = np.stack([0.50 - 0.25 * now, 0.72 - 0.22 * now, 0.38 - 0.18 * now], axis=-1)
    ground[has] = green[has]
    try:
        for n, index in enumerate(picks):
            ax.clear()
            ax.imshow(ground, extent=(-arena, arena, arena, -arena), interpolation="nearest")
            alive = [0] * len(species)
            for a in frames[index]:
                group, kind, dead, male, x, z, heading, energy, health, size, mode, head = a
                kind = int(kind)
                length = species[kind]["length"] * size * args.zoom
                colour = "#6f6f78" if species[kind]["predator"] else "#8a5a2b"
                if dead > 0.5:
                    ax.plot([x], [z], marker="x", color="#3a2a20", markersize=7, markeredgewidth=2)
                    continue
                alive[kind] += 1
                # The animal looks down -z at heading 0, positive headings turn left (toward -x)
                fx, fz = -np.sin(heading), -np.cos(heading)
                hx, hz = -np.sin(head), -np.cos(head)
                body = MODE_COLOURS.get(int(mode)) or colour
                ax.arrow(x - 0.5 * length * fx, z - 0.5 * length * fz, length * fx, length * fz, width=0.35 * length, head_width=0.7 * length,
                         head_length=0.4 * length, length_includes_head=True, color=body, ec="black" if male > 0.5 else colour, lw=0.6, zorder=3)
                ax.plot([x + 0.5 * length * fx, x + 0.5 * length * fx + 4 * hx], [z + 0.5 * length * fz, z + 0.5 * length * fz + 4 * hz], color=colour, lw=0.7, alpha=0.7, zorder=2)
                ax.plot([x - 1.5, x - 1.5 + 3 * max(energy, 0)], [z + 1.2 * length + 0.6, z + 1.2 * length + 0.6], color="#1f77b4", lw=1.6, zorder=4)
                if health < 0.98:
                    ax.plot([x - 1.5, x - 1.5 + 3 * max(health, 0)], [z + 1.2 * length + 1.3, z + 1.2 * length + 1.3], color="#d2262b", lw=1.6, zorder=4)
            label = f"{index * dt / 60:5.1f} min   " + "   ".join(f"{s['id']} {alive[i]}" for i, s in enumerate(species))
            ax.text(-arena + 3, -arena + 6, label, fontsize=11, color="black", bbox=dict(facecolor="white", alpha=0.7, edgecolor="none"), zorder=5)
            ax.set_xlim(-arena, arena)
            ax.set_ylim(arena, -arena)
            ax.set_axis_off()
            fig.savefig(work / f"frame_{n:05d}.png")
        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        out = pathlib.Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps), "-i", str(work / "frame_%05d.png"),
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", str(out)], check=True)
        print(f"{out}: {len(picks)} frames, {len(picks) / args.fps:.1f} s for {(picks[-1] - picks[0]) * dt:.0f} s of the sandbox")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
