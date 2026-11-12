#!/usr/bin/env python3
"""A video of a walk through the forests: dynamical.exe renders forest_walk (docs/forest.md: the camera walks from
open land through a forest and out, around the trees) frame by frame through a capture script, then ffmpeg
(imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/forest_video.py [--scene forest_walk] [--seed 1] [--distance 280] [--speed 4]
                                                    [--fps 30] [--crf 26] [--var NAME=VALUE] [--preview N]
                                                    [--exe build/RelWithDebInfo/dynamical.exe] [--cwd build]
                                                    [--arg KEY=VALUE]
                                                    [--out build/artifacts/forest_video/NAME.mp4]

    # the walk over the soil patches, past the herds and through a forest (docs/tautavel-walk.md)
    build/venv/Scripts/python tools/forest_video.py --scene tautavel_walk

--seed places the forests and their plants (the scene's forest_seed), so it also picks the walk. The seed, the
distance and the speed are the scene's own when they are not given; the walk lasts distance / speed seconds. The
simulation runs at 60 Hz with a fixed frame clock, so the video plays in real time whatever the render speed; the
first frame waits for the terrain, the plants and the animals to be loaded. Captures are 960 x 540.
--preview N renders N frames spread over the walk and no MP4.
--arg passes an argument on to the engine (--arg ray_tracing=false --arg lighting=off : the reference of the goldens),
--cwd runs it in another folder than build/ (a copy of the executable and of resources/, when other sessions build).
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
    parser.add_argument("--scene", default="forest_walk", help="a scene with a CameraWalk and the variables distance and speed")
    parser.add_argument("--seed", type=int, help="the scene's forest_seed by default")
    parser.add_argument("--distance", type=float, help="metres, the scene's by default")
    parser.add_argument("--speed", type=float, help="m/s, the scene's by default")
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--crf", type=int, default=26, help="x264 quality, lower is better and larger (grass does not compress)")
    parser.add_argument("--exe", default=str(EXE), help="the engine (a copy of it, when the build directory is being built in)")
    parser.add_argument("--cwd", default=str(BUILD), help="the folder the engine runs in : its resources/ are the ones read")
    parser.add_argument("--arg", action="append", default=[], metavar="KEY=VALUE", help="an argument of the engine, repeatable")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    parser.add_argument("--preview", type=int, default=0, metavar="N", help="N frames spread over the walk, no MP4 : to choose a seed")
    args = parser.parse_args()

    variables = json.loads((ROOT / "resources" / "scenes" / f"{args.scene}.scene.json").read_text())["variables"]
    distance = args.distance if args.distance is not None else float(variables["distance"]["default"])
    speed = args.speed if args.speed is not None else float(variables["speed"]["default"])
    seed = args.seed if args.seed is not None else int(variables["forest_seed"]["default"])

    out = pathlib.Path(args.out) if args.out else BUILD / "artifacts" / "forest_video" / f"{args.scene}_seed{seed}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    seconds = distance / speed
    count = args.preview or int(seconds * args.fps)
    every = max(int(seconds * 60) // args.preview, 1) if args.preview else 60 // args.fps
    script = {
        "scene": args.scene,
        "vars": {"forest_seed": str(seed), "distance": str(distance), "speed": str(speed)}
                | dict(v.split("=", 1) for v in args.var),
        "sequence": {"start": 30, "count": count, "every": every, "name": "frame"},
        "hide_ui": True,
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))

    print(f"rendering {count} frames of {args.scene} (seed {seed})", flush=True)
    start = time.monotonic()
    proc = subprocess.run([args.exe, f"capture={frames / 'capture.json'}", f"capture_dir={frames}", "fullscreen=false",
                           f"window_width={args.width}", f"window_height={args.height}", "vr_mode=0"] + args.arg,
                          cwd=args.cwd, capture_output=True, text=True, timeout=7200)
    (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
    rendered = sorted(frames.glob("frame_*.png"))
    print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - start:.0f} s", flush=True)
    if proc.returncode != 0 or len(rendered) != count:
        print((proc.stdout + proc.stderr)[-3000:])
        sys.exit(1)

    if args.preview:
        print(frames)
        return

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
