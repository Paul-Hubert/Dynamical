#!/usr/bin/env python3
"""A video of the sphere roller: dynamical.exe renders sphere_view (the policy on the marching cubes terrain, follow
camera) frame by frame through a capture script, then ffmpeg (imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/sphere_video.py [--model FILE.onnx | --run RUN_ID] [--scene sphere_view|wheel_view]
                                                    [--seconds 30] [--fps 30] [--seed 1] [--heuristic]
                                                    [--out build/artifacts/sphere_video/NAME.mp4]

--run takes the newest checkpoint of build/runs/<RUN_ID>/<behavior> (default: the newest run with checkpoints).
--scene must match the body the policy was trained on: sphere_view (SphereRoller) or wheel_view (WheelRoller); by
default it follows the behaviour's name. walker_terrain_view puts a walker policy on the marching cubes terrain.
--heuristic films the proportional controller instead (scene sphere_view_heuristic). The simulation runs at 60 Hz
with a fixed frame clock, so the video plays in real time whatever the render speed. Frames are 960 x 540.
"""

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time

import imageio_ffmpeg

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
EXE = BUILD / "RelWithDebInfo" / "dynamical.exe"


SCENES = {"SphereRoller": "sphere_view", "WheelRoller": "wheel_view", "HeidelBotBehavior": "walker_view", "AnySpeedBotBehavior": "devita_view"}


def newest_checkpoint(run_dir):
    best, best_step = None, -1
    for f in run_dir.glob("*/*-*.onnx"):
        m = re.match(r"(\w+)-(\d+)\.onnx$", f.name)
        if m and m.group(1) == f.parent.name and int(m.group(2)) > best_step:
            best, best_step = f, int(m.group(2))
    return best, best_step


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model")
    parser.add_argument("--run")
    parser.add_argument("--scene", choices=sorted(set(SCENES.values()) | {"walker_terrain_view", "walker_demo_terrain_view"}))
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--seconds", type=float, default=30.)
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--seed", type=int, default=1, help="the worlds' seed: spawn point and targets")
    parser.add_argument("--heuristic", action="store_true")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    if args.scene == "walker_demo_terrain_view":
        # The kinematic walk recording its demonstrations: no policy
        model, label, args.heuristic = "", "gait_demo", True
    elif args.heuristic:
        model, label = "", "heuristic"
    elif args.model:
        model, label = str(pathlib.Path(args.model).resolve()), pathlib.Path(args.model).stem
    else:
        runs = [BUILD / "runs" / args.run] if args.run else sorted((BUILD / "runs").iterdir(), key=lambda p: p.stat().st_mtime, reverse=True)
        found = [(r, *newest_checkpoint(r)) for r in runs if r.is_dir()]
        found = [f for f in found if f[1]]
        if not found:
            sys.exit("no SphereRoller checkpoint under build/runs")
        run_dir, checkpoint, step = found[0]
        model, label = str(checkpoint), f"{run_dir.name}_{step // 1_000_000}M"

    behavior = re.match(r"(\w+?)(-\d+)?$", pathlib.Path(model).stem).group(1) if model else "SphereRoller"
    scene = args.scene or SCENES.get(behavior, "sphere_view")

    out = pathlib.Path(args.out) if args.out else BUILD / "artifacts" / "sphere_video" / f"{label}_seed{args.seed}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    count = int(args.seconds * args.fps)
    script = {
        "scene": scene if scene == "walker_demo_terrain_view" else "sphere_view_heuristic" if args.heuristic else scene,
        "vars": {"num_worlds": "1"} | ({} if args.heuristic else {"model": model}) | dict(v.split("=", 1) for v in args.var),
        # Half a second first: the first decisions are taken and the terrain around the spawn point is meshed
        "sequence": {"start": 30, "count": count, "every": 60 // args.fps, "name": "frame"},
        # The walker views frame the body in the middle of the window, under the debug panels
        "hide_ui": scene in ("walker_view", "devita_view", "walker_terrain_view", "walker_demo_terrain_view"),
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))

    print(f"rendering {count} frames of {script['scene']} ({label}, seed {args.seed})", flush=True)
    start = time.monotonic()
    proc = subprocess.run([str(EXE), f"capture={frames / 'capture.json'}", f"capture_dir={frames}", f"seed={args.seed}"],
                          cwd=BUILD, capture_output=True, text=True, timeout=3600)
    (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
    rendered = sorted(frames.glob("frame_*.png"))
    print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - start:.0f} s", flush=True)
    if proc.returncode != 0 or len(rendered) != count:
        print((proc.stdout + proc.stderr)[-3000:])
        sys.exit(1)

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps), "-i", str(frames / "frame_%05d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", "-preset", "slow", str(out)], check=True)
    if not args.keep_frames:
        for f in rendered[1:-1]:
            if f != rendered[len(rendered) // 2]:
                f.unlink()
    print(out)


if __name__ == "__main__":
    main()
