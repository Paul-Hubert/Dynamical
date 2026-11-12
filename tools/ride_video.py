#!/usr/bin/env python3
"""A video of the player getting on an animal, riding it and getting off (docs/players.md): dynamical.exe plays the
scripted input of a capture script (tests/capture/player_horse.capture.json: the kinematic horse on the terrain,
player_deer.capture.json: the physical deer with its policy) and renders it frame by frame, then ffmpeg
(imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/ride_video.py [--ride horse|deer] [--fps 30] [--width 1280] [--height 720]
                                                  [--third-person 5] [--var NAME=VALUE] [--out build/artifacts/ride_video/NAME.mp4]

The video runs from a little before the script's first input to its last shot. The simulation runs at 60 Hz with a
fixed frame clock, so the video plays in real time whatever the render speed.
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
    parser.add_argument("--ride", default="horse", choices=["horse", "deer"])
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--third-person", type=float, default=None, help="metres the camera is behind the player's eyes (the script's own without it)")
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--crf", type=int, default=24, help="x264 quality, lower is better and larger")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    source = json.loads((ROOT / "tests" / "capture" / f"player_{args.ride}.capture.json").read_text())
    out = pathlib.Path(args.out) if args.out else BUILD / "artifacts" / "ride_video" / f"{args.ride}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    # From a second before the first key to the last shot
    first = min(e["frame"] for e in source["input"] if "key" in e)
    last = max(s["frame"] for s in source["shots"])
    start = max(first - 60, 1)
    every = 60 // args.fps
    count = (last - start) // every
    variables = dict(source.get("vars", {})) | dict(v.split("=", 1) for v in args.var)
    if args.third_person is not None:
        variables["third_person"] = args.third_person
    script = {
        "scene": source["scene"],
        "vars": variables,
        "input": source["input"],
        "sequence": {"start": start, "count": count, "every": every, "name": "frame"},
        "hide_ui": True,
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))

    print(f"rendering {count} frames of {source['scene']} ({args.width} x {args.height})", flush=True)
    begin = time.monotonic()
    proc = subprocess.run([str(EXE), f"capture={frames / 'capture.json'}", f"capture_dir={frames}", "fullscreen=false",
                           f"window_width={args.width}", f"window_height={args.height}", "vr_mode=0"],
                          cwd=BUILD, capture_output=True, text=True, timeout=7200)
    (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
    rendered = sorted(frames.glob("frame_*.png"))
    print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - begin:.0f} s", flush=True)
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
