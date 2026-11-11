#!/usr/bin/env python3
"""Films a physical animal with its skinned mesh on the ragdoll (docs/physical-agents/reports/animal-agents.md): a
policy, the standing pose held by the drives, or the procedural walk of the demonstrations. dynamical renders the
scene frame by frame through a capture script, then ffmpeg (imageio-ffmpeg in build/venv) encodes an MP4.

    build/venv/Scripts/python tools/animal_video.py --species deer_red --run animal_deer_red_20m [--model <checkpoint.onnx>]
    build/venv/Scripts/python tools/animal_video.py --species deer_red --demo            # the kinematic walk
    build/venv/Scripts/python tools/animal_video.py --species deer_red                   # zero actions: it stands

--terrain films on the marching cubes terrain (animal_terrain_view). --shapes draws the ragdoll's shapes too.
--var NAME=VALUE sets a scene variable (ranges, speed, soft_depth, gait for --demo, view_distance, view_side).
The video goes to build/runs/animal_videos/<label>_seed<seed>.mp4 unless --out; the first, middle and last frames
are kept next to it.
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
    parser.add_argument("--species", required=True)
    parser.add_argument("--model", help="an .onnx policy of the species")
    parser.add_argument("--run", help="a run of tools/train_animal.py: its frozen body (the one the policy knows), and its final policy unless --model")
    parser.add_argument("--demo", action="store_true", help="the procedural walk played on the body (animal_demo_view)")
    parser.add_argument("--terrain", action="store_true")
    parser.add_argument("--shapes", action="store_true")
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--seconds", type=float, default=30.)
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--label")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    body = ROOT / "resources" / "animals" / "bodies" / f"{args.species}.body.json"
    if args.run:
        body = BUILD / "runs" / f"{args.run}_env" / "resources" / "animals" / "bodies" / f"{args.species}.body.json"
        if not args.model:
            args.model = str(BUILD / "runs" / args.run / f"Animal_{args.species}.onnx")
    if not body.exists():
        sys.exit(f"{body} is missing: tools/train_animal.py --species {args.species} --no-train, or animal_body.exe")
    scene = "animal_demo_view" if args.demo else "animal_terrain_view" if args.terrain else "animal_view"
    label = args.label or (f"{args.species}_demo" if args.demo else f"{args.species}_{pathlib.Path(args.model).parent.name}" if args.model else f"{args.species}_stand")
    variables = {"body": str(body)}
    if not args.demo:
        variables["num_worlds"] = "1"
        if args.model:
            variables["model"] = str(pathlib.Path(args.model).resolve())
    if args.shapes:
        variables["draw"] = "true"
    variables |= dict(v.split("=", 1) for v in args.var)

    out = pathlib.Path(args.out) if args.out else BUILD / "runs" / "animal_videos" / f"{label}_seed{args.seed}.mp4"
    out = out.resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)

    count = int(args.seconds * args.fps)
    script = {
        "scene": scene,
        "vars": variables,
        # A second and a half first: the species is loaded and drawn, the first decisions are taken
        "sequence": {"start": 90, "count": count, "every": 60 // args.fps, "name": "frame"},
        "hide_ui": True,
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))
    print(f"rendering {count} frames of {scene} ({label}, seed {args.seed})", flush=True)
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
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", "-preset", "slow", str(out)], check=True)
    if not args.keep_frames:
        for f in rendered[1:-1]:
            if f != rendered[len(rendered) // 2]:
                f.unlink()
    print(out)


if __name__ == "__main__":
    main()
