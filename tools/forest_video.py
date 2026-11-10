#!/usr/bin/env python3
"""A video of a walk through the forests: dynamical.exe renders forest_walk (docs/forest.md: the camera walks from
open land through a forest and out, around the trees) frame by frame through a capture script, then ffmpeg
(imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/forest_video.py [--seed 1] [--distance 280] [--speed 4] [--fps 30]
                                                    [--width 1280] [--height 720] [--crf 26] [--var NAME=VALUE]
                                                    [--out build/artifacts/forest_video/NAME.mp4]

--seed places the forests and their plants (the scene's forest_seed), so it also picks the walk. The walk lasts
distance / speed seconds. The simulation runs at 60 Hz with a fixed frame clock, so the video plays in real time
whatever the render speed; the first frame waits for the terrain and the plants to be loaded.
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
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--distance", type=float, default=280.)
    parser.add_argument("--speed", type=float, default=4.)
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--crf", type=int, default=26, help="x264 quality, lower is better and larger (grass does not compress)")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    out = pathlib.Path(args.out) if args.out else BUILD / "artifacts" / "forest_video" / f"forest_walk_seed{args.seed}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    count = int(args.distance / args.speed * args.fps)
    script = {
        "scene": "forest_walk",
        "vars": {"forest_seed": str(args.seed), "distance": str(args.distance), "speed": str(args.speed)}
                | dict(v.split("=", 1) for v in args.var),
        "sequence": {"start": 30, "count": count, "every": 60 // args.fps, "name": "frame"},
        "hide_ui": True,
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))

    print(f"rendering {count} frames of forest_walk (seed {args.seed}, {args.width} x {args.height})", flush=True)
    start = time.monotonic()
    proc = subprocess.run([str(EXE), f"capture={frames / 'capture.json'}", f"capture_dir={frames}", "fullscreen=false",
                           f"window_width={args.width}", f"window_height={args.height}", "vr_mode=0"],
                          cwd=BUILD, capture_output=True, text=True, timeout=7200)
    (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
    rendered = sorted(frames.glob("frame_*.png"))
    print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - start:.0f} s", flush=True)
    if proc.returncode != 0 or len(rendered) != count:
        print((proc.stdout + proc.stderr)[-3000:])
        sys.exit(1)

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps), "-i", str(frames / "frame_%05d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", str(args.crf), "-preset", "slow", str(out)], check=True)
    if not args.keep_frames:
        keep = {rendered[0], rendered[-1]} | set(rendered[::max(len(rendered) // 8, 1)])
        for f in rendered:
            if f not in keep:
                f.unlink()
    print(out)


if __name__ == "__main__":
    main()
