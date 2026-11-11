#!/usr/bin/env python3
"""Films a trained animal without the viewer: the policy is rolled out by the run's frozen TrainingEnv.exe (mode=eval,
var.replay), and the skinned mesh of the library is drawn on the ragdoll's segments frame by frame with matplotlib
(docs/physical-agents/reports/animal-agents.md). tools/animal_video.py films the same in the engine, textured; this
one needs no GPU and no dynamical.exe.

    build/venv/Scripts/python tools/animal_replay_video.py --run animal_deer_red_20m [--checkpoint 5000000] [--seconds 30]

Each vertex follows the segments its joints ride on (a joint that is not a body rides on its segment as in the bind
pose). The view is fixed in direction and follows the trunk; the ground carries a 1 m grid, the arrow is the
commanded heading as the trunk's path shows it. Writes build/runs/animal_videos/<run>[_<step>]_replay.mp4.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

import imageio_ffmpeg
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from animal_sheets import load_species, quat_matrix  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True)
    parser.add_argument("--species")
    parser.add_argument("--checkpoint", type=int)
    parser.add_argument("--seconds", type=float, default=30.)
    parser.add_argument("--stochastic", action="store_true")
    parser.add_argument("--azimuth", type=float, default=55., help="degrees: where the view looks from, about the vertical")
    parser.add_argument("--elevation", type=float, default=12.)
    parser.add_argument("--out")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    args = parser.parse_args()

    results = pathlib.Path(args.results)
    env = results / f"{args.run}_env"
    bodies = sorted((env / "resources" / "animals" / "bodies").glob("*.body.json"))
    if args.species:
        bodies = [b for b in bodies if b.name == f"{args.species}.body.json"]
    if len(bodies) != 1:
        sys.exit(f"{env}: {len(bodies)} bodies frozen with the run, name one with --species")
    body_path = bodies[0]
    species = body_path.name[:-len(".body.json")]
    behavior = f"Animal_{species}"
    model = results / args.run / f"{behavior}.onnx"
    label = args.run
    if args.checkpoint:
        found = []
        for f in (results / args.run / behavior).glob(f"{behavior}-*.onnx"):
            m = re.match(rf"{re.escape(behavior)}-(\d+)\.onnx$", f.name)
            if m:
                found.append((abs(int(m.group(1)) - args.checkpoint), f))
        if not found:
            sys.exit("no checkpoint")
        model = min(found)[1]
        label += f"_{args.checkpoint // 1000}k"
    out = pathlib.Path(args.out) if args.out else results / "animal_videos" / f"{label}_replay.mp4"
    frames_dir = out.with_suffix("")
    frames_dir.mkdir(parents=True, exist_ok=True)
    prefix = frames_dir / "rollout"
    console = results / f"{args.run}.console.txt"
    used = re.search(r"var\.ranges=(\S+)", console.read_text(encoding="utf-8", errors="replace")) if console.exists() else None
    ranges = used.group(1) if used else str(env / "resources" / "agents" / "ranges" / "walk_basic.json")
    used = re.search(r"var\.params=(\S+)", console.read_text(encoding="utf-8", errors="replace")) if console.exists() else None
    params = used.group(1) if used else str(env / "resources" / "agents" / "params" / "animal.params.json")
    cmd = ["", "scene=animal_eval", "mode=eval", f"var.body={body_path}", f"var.ranges={ranges}",
           f"var.params={params}", f"var.model={model}", "var.episodes=1", "var.num_worlds=1",
           f"var.stochastic={1 if args.stochastic else 0}", f"var.replay={prefix}", f"var.replay_seconds={args.seconds}"]
    header_file = pathlib.Path(str(prefix) + ".replay.json")
    header_file.unlink(missing_ok=True)
    # The run's own executable when it knows var.replay; an engine frozen before that does not: the current build then
    # plays the frozen body, params and ranges (said in the output)
    for exe in (env / "TrainingEnv.exe", ROOT / "build" / "RelWithDebInfo" / "TrainingEnv.exe"):
        cmd[0] = str(exe)
        result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if result.returncode == 0 and header_file.exists():
            print(f"rolled out by {exe}", flush=True)
            break
    else:
        print((result.stdout + result.stderr)[-2000:])
        sys.exit("no replay written")
    header = json.loads(header_file.read_text())
    segments, count, dt = header["segments"], header["frames"], header["dt"]
    data = np.fromfile(str(prefix) + ".replay.bin", "<f4").reshape(count, segments, 7)

    body = json.loads(body_path.read_text())
    info, vertices, used_vertices, bind = load_species(species)
    position = vertices["p"][used_vertices].astype(np.float64)
    joints = vertices["j"][used_vertices]
    weights = vertices["w"][used_vertices].astype(np.float64) / 255.
    if len(position) > 9000:
        pick = np.random.default_rng(1).choice(len(position), 9000, replace=False)
        position, joints, weights = position[pick], joints[pick], weights[pick]
    joint_segment = np.array(body["skeleton"])
    vertex_segment = joint_segment[joints]                                   # (n, 4)
    segment_bind = np.array([bind[s["joint"]] for s in body["segments"]])    # where each segment's joint is in the bind pose

    az, el = np.radians(args.azimuth), np.radians(args.elevation)
    # View axes: right, up; the animal looks down -z at yaw 0
    right = np.array([np.cos(az), 0., -np.sin(az)])
    forward = np.array([np.sin(az) * np.cos(el), -np.sin(el), np.cos(az) * np.cos(el)])
    up = np.cross(forward, right)
    up /= np.linalg.norm(up)
    size = max(body["length"], body["height"])
    half = 1.05 * size

    fps = int(round(1. / dt))
    ends = set(header.get("episode_ends", []))
    focus = data[0, 0, :3].astype(np.float64).copy()
    episode = 0
    for f in range(count):
        pose = data[f].astype(np.float64)
        if f in ends:
            episode += 1
        trunk = pose[0, :3]
        focus += (trunk - focus) * (1. if np.linalg.norm(trunk - focus) > 3 * size else 0.15)
        rotations = np.array([quat_matrix(q) for q in pose[:, 3:7]])
        # Each vertex with each of its segments, blended by its weights
        moved = np.zeros_like(position)
        for k in range(4):
            seg = vertex_segment[:, k]
            local = position - segment_bind[seg]
            moved += weights[:, k:k + 1] * (np.einsum("nij,nj->ni", rotations[seg], local) + pose[seg, :3])
        rel = moved - focus
        x, y, depth = rel @ right, rel @ up, rel @ forward

        fig = plt.figure(figsize=(9.6, 5.4), dpi=100, facecolor="#dfe6ee")
        ax = fig.add_axes([0, 0, 1, 1])
        ax.set_facecolor("#b9cfe6")
        # The ground: a 1 m grid fixed to the world
        step = 1.0 if size > 0.6 else 0.25
        g0 = np.floor((focus[[0, 2]] - 3 * half) / step) * step
        lines = np.arange(0, 6 * half + step, step)
        for v in lines:
            for a, b in (((g0[0] + v, 0, g0[1]), (g0[0] + v, 0, g0[1] + 6 * half)), ((g0[0], 0, g0[1] + v), (g0[0] + 6 * half, 0, g0[1] + v))):
                p0, p1 = np.array(a) - focus, np.array(b) - focus
                ax.plot([p0 @ right, p1 @ right], [p0 @ up, p1 @ up], color="#9a9c8c", linewidth=0.6, zorder=1)
        order = np.argsort(-depth)
        shade = 0.35 + 0.5 * (1 - (depth[order] - depth.min()) / max(np.ptp(depth), 1e-6))
        ax.scatter(x[order], y[order], s=5.0, c=np.stack([0.75 * shade, 0.55 * shade, 0.35 * shade], axis=1), linewidths=0, zorder=3)
        # The shadow of the trunk on the ground: where the animal is over the grid
        shadow = np.array([trunk[0], 0., trunk[2]]) - focus
        ax.scatter([shadow @ right], [shadow @ up], s=60, c="#8a8c7c", marker="o", linewidths=0, zorder=2)
        ax.set_xlim(-half * 16 / 9, half * 16 / 9)
        ax.set_ylim(-half + 0.1 * body["height"], half + 0.1 * body["height"])
        ax.set_aspect("equal")
        ax.axis("off")
        ax.text(0.01, 0.97, f"{species}  {model.name}  t = {f * dt:5.2f} s  episode {episode + 1}", transform=ax.transAxes, fontsize=8, va="top", color="#222222")
        fig.savefig(frames_dir / f"frame_{f:05d}.png")
        plt.close(fig)
        if f % 150 == 0:
            print(f"frame {f} of {count}", flush=True)

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", str(frames_dir / "frame_%05d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", "-preset", "slow", str(out)], check=True)
    kept = {0, count // 2, count - 1}
    for f in range(count):
        if f not in kept:
            (frames_dir / f"frame_{f:05d}.png").unlink(missing_ok=True)
    print(out)


if __name__ == "__main__":
    main()
