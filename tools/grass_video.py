#!/usr/bin/env python3
"""A video of the grass seen from close: the camera glides slowly just over the blades of the base scene, forward,
sideways, looking down, turning, then up to a crest where the sky shows between them (docs/grass.md).
dynamical.exe renders it frame by frame through a capture script with one shot per frame, each camera a height
over the ground (the "above" of src/view/capture.h), then ffmpeg (imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/grass_video.py [--seconds 6] [--fps 30] [--lift 0] [--crf 20]
                                                   [--out build/artifacts/grass_video/grass_close.mp4]

--seconds is the time of each of the legs of the path, --lift raises the whole path (metres). The simulation runs
at 60 Hz with a fixed frame clock, so the wind blows in real time whatever the render speed. Captures are 960 x 540.
"""

import argparse
import json
import math
import pathlib
import shutil
import subprocess
import sys
import time

import imageio_ffmpeg

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
EXE = BUILD / "RelWithDebInfo" / "dynamical.exe"

# The path : x, z, height over the ground (the grass is 1 m at its tallest), yaw and pitch in degrees
# (yaw 0 looks along -z, 90 along -x). The camera eases from each to the next
PATH = [
    (-21.0, 25.0, 1.9, 0, -14),     # over the meadow, the boxes ahead
    (-21.0, 20.0, 1.35, 0, -10),    # forward and down to the blades
    (-17.0, 20.0, 1.3, 0, -20),     # sideways : blades slide past each other
    (-17.0, 16.0, 1.5, 0, -62),     # forward, looking down into the grass
    (-19.0, 13.5, 1.3, 90, -16),    # turning to the left
    (-25.0, 13.5, 1.25, 90, -7),    # skimming the tips
    (-27.0, 11.0, 1.4, 200, -28),   # round and back, a look behind
    (-23.5, 10.0, 1.5, 360, -5),    # towards the crest
    (-23.0, 6.5, 1.3, 360, -3),     # up to it : the sky between the blades
]

FIRST_FRAME = 600   # the terrain is there, the shot waits if it is not


def ease(t):
    return t * t * t * (t * (6. * t - 15.) + 10.)


def cameras(frames_per_leg):
    for a, b in zip(PATH, PATH[1:]):
        for k in range(frames_per_leg):
            t = ease(k / frames_per_leg)
            yield tuple(a[i] + (b[i] - a[i]) * t for i in range(5))
    yield PATH[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=6., help="of each leg of the path")
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--lift", type=float, default=0., help="metres added to the height of the camera over the ground")
    parser.add_argument("--crf", type=int, default=20, help="x264 quality, lower is better and larger (grass does not compress)")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    out = pathlib.Path(args.out) if args.out else BUILD / "artifacts" / "grass_video" / "grass_close.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    every = 60 // args.fps
    shots = []
    for i, (x, z, above, yaw, pitch) in enumerate(cameras(int(args.seconds * args.fps))):
        shots.append({"frame": FIRST_FRAME + i * every, "name": f"frame_{i:05d}",
                      "camera": {"pos": [round(x, 4), 0, round(z, 4)], "above": round(above + args.lift, 4),
                                 "yaw": round(math.radians(yaw), 5), "pitch": round(math.radians(pitch), 5)}})
    script = {"scene": "base", "hide_ui": True, "shots": shots}
    (frames / "capture.json").write_text(json.dumps(script))

    print(f"rendering {len(shots)} frames of the grass", flush=True)
    start = time.monotonic()
    rendered = []
    # The engine sometimes dies before its first frame when something else holds the GPU or the build directory
    for attempt in range(12):
        proc = subprocess.run([str(EXE), f"capture={frames / 'capture.json'}", f"capture_dir={frames}"],
                              cwd=BUILD, capture_output=True, text=True, timeout=7200)
        (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
        rendered = sorted(frames.glob("frame_*.png"))
        print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - start:.0f} s", flush=True)
        if rendered:
            break
    if len(rendered) != len(shots):
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
