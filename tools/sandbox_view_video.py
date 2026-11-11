#!/usr/bin/env python3
"""A video of the sandbox in the viewer (docs/action-agents.md): dynamical.exe renders sandbox_view (the ragdolls as
shapes, the policies of a run in them, the camera round one animal) frame by frame through a capture script, then
ffmpeg (imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/sandbox_view_video.py --models build/runs/sandbox_r1 [--watch wolf_2] [--seconds 30]
                                                          [--prefix Animal_] [--var NAME=VALUE ...] [--out FILE.mp4]

--models: a folder with <behavior>.onnx or <behavior>/<behavior>-<step>.onnx for the behaviours it has (a run of
tools/sandbox_round.py; for walkers alone, a folder of copies). The minds no model answers are the scripted ones.
--prefix Animal_ takes the walkers of the run and leaves its minds to the scripted ones. The simulation runs at 60 Hz
with a fixed frame clock, so the video plays in real time whatever the render speed. Frames are 960 x 540.
tools/sandbox_video.py draws the whole arena from above instead, from a replay.
"""

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import time

import imageio_ffmpeg

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
EXE = BUILD / "RelWithDebInfo" / "dynamical.exe"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="", help="empty: zero actions for the walkers (they stand), the scripted minds")
    parser.add_argument("--prefix", default="")
    parser.add_argument("--watch", default="", help="the species the camera stays with (default: a predator)")
    parser.add_argument("--distance", type=float, default=3.5, help="of the camera, in sizes of the animal")
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--seconds", type=float, default=30.)
    parser.add_argument("--skip", type=float, default=2., help="seconds simulated before the first frame")
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    label = pathlib.Path(args.models).name if args.models else "standing"
    out = pathlib.Path(args.out) if args.out else BUILD / "runs" / "sandbox_videos" / f"{label}_{args.watch or 'predator'}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    count = int(args.seconds * args.fps)
    variables = {"num_worlds": "1", "models": str(pathlib.Path(args.models).resolve()) if args.models else "", "prefix": args.prefix,
                 "watch": args.watch, "watch_distance": str(args.distance)}
    variables |= dict(v.split("=", 1) for v in args.var)
    script = {"scene": "sandbox_view", "vars": variables, "hide_ui": True,
              "sequence": {"start": int(args.skip * 60), "count": count, "every": 60 // args.fps, "name": "frame"}}
    (frames / "capture.json").write_text(json.dumps(script, indent=2))

    print(f"rendering {count} frames of sandbox_view ({label}, the camera with {args.watch or 'a predator'})", flush=True)
    start = time.monotonic()
    proc = subprocess.run([str(EXE), f"capture={frames / 'capture.json'}", f"capture_dir={frames}", f"seed={args.seed}"],
                          cwd=BUILD, capture_output=True, text=True, timeout=7200)
    (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
    rendered = sorted(frames.glob("frame_*.png"))
    print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - start:.0f} s", flush=True)
    if proc.returncode != 0 or len(rendered) != count:
        print((proc.stdout + proc.stderr)[-3000:])
        sys.exit(1)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps), "-i", str(frames / "frame_%05d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", str(out)], check=True)
    if not args.keep_frames:
        keep = frames / f"frame_{count // 2:05d}.png"
        if keep.exists():
            shutil.copy2(keep, out.with_suffix(".png"))
        shutil.rmtree(frames, ignore_errors=True)
    print(out)


if __name__ == "__main__":
    main()
