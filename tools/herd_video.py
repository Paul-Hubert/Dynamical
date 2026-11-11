#!/usr/bin/env python3
"""A video of a herd in the forests: dynamical.exe renders forest_herds_watch (docs/animals.md: the camera goes
round a herd that grazes and moves on) frame by frame through a capture script, then ffmpeg (imageio-ffmpeg in
build/venv) encodes an MP4.

    build/venv/Scripts/python tools/herd_video.py [--kind "red deer"] [--seconds 40] [--seed 1] [--fps 30]
                                                  [--width 1280] [--height 720] [--crf 24] [--var NAME=VALUE]
                                                  [--scene forest_herds_watch] [--out build/artifacts/herd_video/NAME.mp4]

    # the procedural walk, the camera round the first animal
    build/venv/Scripts/python tools/herd_video.py --scene animals_gait --var species=deer_red --var path=eight --var follow=0

--kind is a herd of resources/herds/tautavel.herds.json (red deer, reindeer, bison, horses, wolves, ...): the camera
takes the nearest one. The simulation runs at 60 Hz with a fixed frame clock, so the video plays in real time
whatever the render speed; the first frame waits for the terrain, the plants and the animals to be loaded.
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
    parser.add_argument("--scene", default="forest_herds_watch")
    parser.add_argument("--kind", default="")
    parser.add_argument("--seconds", type=float, default=40.)
    parser.add_argument("--seed", type=int, default=1, help="places the herds (the scene's herd_seed)")
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--crf", type=int, default=24, help="x264 quality, lower is better and larger")
    parser.add_argument("--start", type=int, default=240, help="frames before the first one of the video")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    parser.add_argument("--preview", type=int, default=0, metavar="N", help="N frames spread over the video, no MP4 : to choose a seed")
    args = parser.parse_args()

    name = (args.kind.replace(" ", "_") or "herd") + f"_seed{args.seed}"
    out = pathlib.Path(args.out) if args.out else BUILD / "artifacts" / "herd_video" / f"{name}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    count = args.preview or int(args.seconds * args.fps)
    every = int(args.seconds * 60) // args.preview if args.preview else 60 // args.fps
    # Other scenes (animals_gait, animals_test) take their variables from --var alone
    variables = {"herd_seed": str(args.seed)} if "herds" in args.scene else {}
    if args.kind:
        variables["kind"] = args.kind
    script = {
        "scene": args.scene,
        "vars": variables | dict(v.split("=", 1) for v in args.var),
        "sequence": {"start": args.start, "count": count, "every": every, "name": "frame"},
        "hide_ui": True,
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))

    print(f"rendering {count} frames of {args.scene} ({args.width} x {args.height})", flush=True)
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
