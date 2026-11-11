#!/usr/bin/env python3
"""Films a trained animal through every order it was trained on, in the engine (the skinned mesh on the ragdoll), in
one take (docs/physical-agents/reports/animal-agents.md): the slowest and the fastest speed of its ranges straight
ahead, the tightest and the widest turn to each side, then orders drawn as in training; with ranges that stand and run
(gaits_basic), standing still from a walk and from a run, and the run's own speeds and turns. AnimalAgentView gives the
orders (scene variable `orders`) and lays a chequer on the ground; the film says at every frame what is asked and
what the animal does, and draws its path and where the order is in the trained ranges.

    build/venv/Scripts/python tools/animal_orders_video.py --run animal_deer_red_20m [--drawn 30] [--model <checkpoint.onnx>]

The body, the params and the ranges are the ones frozen with the run. The ground is the one of the run's training
scene: level and hard unless --terrain was trained (not filmed here). Writes
build/runs/animal_videos/<run>_orders.mp4 and its log (.csv), and keeps a few frames.
"""

import argparse
import csv
import json
import math
import pathlib
import re
import shutil
import subprocess
import sys
import time

import imageio_ffmpeg
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
EXE = BUILD / "RelWithDebInfo" / "dynamical.exe"


def tour(ranges, drawn, run):
    """[seconds, speed (walk speeds), radius (lengths, > 0 left)] over the corners of the ranges. run: the run clip's
    speed in walk speeds, 0 for an animal or ranges that do not run. A speed of 1e-6 stands (0 is "drawn")."""
    slow, fast = ranges["speed"]
    tight, wide = ranges["radius"]
    mid = 0.5 * (slow + fast)
    stand = 1e-6
    if ranges.get("idle", 0) > 0 and ranges.get("run", 0) > 0 and run > 0:
        run_slow, run_fast = (run * v for v in ranges["run_speed"])
        run_tight, run_wide = ranges["run_radius"]
        orders = [
            [5, stand, 0], [8, mid, 0], [6, stand, 0], [6, mid, 0],
            [8, run_fast, 0], [10, run_slow, run_tight], [10, run_fast, -run_tight], [6, stand, 0],
            [8, slow, tight], [8, fast, -tight], [8, 0.5 * (run_slow + run_fast), run_wide], [5, stand, 0],
        ]
    else:
        orders = [
            [8, mid, 0], [8, slow, 0], [8, fast, 0],
            [25, mid, tight], [12, fast, -tight], [12, slow, tight],
            [8, mid, wide], [8, mid, -wide],
        ]
        if ranges.get("idle", 0) > 0:
            orders += [[6, stand, 0], [6, mid, 0]]
    if drawn > 0:
        orders.append([drawn, 0, 0])
    return orders


def font(size, bold=False):
    for name in (("segoeuib.ttf", "arialbd.ttf") if bold else ("segoeui.ttf", "arial.ttf")):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True)
    parser.add_argument("--species")
    parser.add_argument("--model")
    parser.add_argument("--drawn", type=float, default=30., help="seconds of orders drawn as in training, at the end")
    parser.add_argument("--fps", type=int, default=30, choices=[15, 20, 30, 60])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--distance", type=float, default=2.0, help="of the camera, in sizes of the animal")
    parser.add_argument("--out")
    parser.add_argument("--keep-frames", action="store_true")
    args = parser.parse_args()

    env = BUILD / "runs" / f"{args.run}_env"
    bodies = sorted((env / "resources" / "animals" / "bodies").glob("*.body.json"))
    if args.species:
        bodies = [b for b in bodies if b.name == f"{args.species}.body.json"]
    if len(bodies) != 1:
        sys.exit(f"{env}: {len(bodies)} bodies frozen with the run, name one with --species")
    body_path = bodies[0]
    species = body_path.name[:-len(".body.json")]
    model = pathlib.Path(args.model).resolve() if args.model else BUILD / "runs" / args.run / f"Animal_{species}.onnx"
    console = BUILD / "runs" / f"{args.run}.console.txt"
    used = re.search(r"var\.ranges=(\S+)", console.read_text(encoding="utf-8", errors="replace")) if console.exists() else None
    ranges_path = pathlib.Path(used.group(1)) if used else env / "resources" / "agents" / "ranges" / "walk_basic.json"
    ranges = json.loads(ranges_path.read_text())
    used = re.search(r"var\.params=(\S+)", console.read_text(encoding="utf-8", errors="replace")) if console.exists() else None
    params = pathlib.Path(used.group(1)) if used else env / "resources" / "agents" / "params" / "animal.params.json"
    body = json.loads(body_path.read_text())
    walk, length = body["walk_speed"], body["length"]

    run = body.get("run_speed", 0.) / walk
    orders = tour(ranges, args.drawn, run)
    runs = ranges.get("run", 0) > 0 and run > 0
    top = run * ranges["run_speed"][1] if runs else ranges["speed"][1]
    bottom = 0. if ranges.get("idle", 0) > 0 else ranges["speed"][0]
    seconds = sum(o[0] for o in orders)
    out = (pathlib.Path(args.out) if args.out else BUILD / "runs" / "animal_videos" / f"{args.run}_orders.mp4").resolve()
    frames = out.with_suffix("")
    shutil.rmtree(frames, ignore_errors=True)
    frames.mkdir(parents=True)
    log = frames / "view.csv"

    start, every = 20, 60 // args.fps
    count = int(seconds * args.fps) - start // every
    script = {
        "scene": "animal_view",
        "vars": {"body": str(body_path), "params": str(params), "ranges": str(ranges_path), "model": str(model), "num_worlds": "1",
                 "orders": json.dumps(orders), "marks": "2", "view_log": str(log), "view_distance": str(args.distance)},
        "sequence": {"start": start, "count": count, "every": every, "name": "frame"},
        "hide_ui": True,
    }
    (frames / "capture.json").write_text(json.dumps(script, indent=2))
    print(f"rendering {count} frames: {seconds:.0f} s of {species}, {len(orders)} orders", flush=True)
    began = time.monotonic()
    proc = subprocess.run([str(EXE), f"capture={frames / 'capture.json'}", f"capture_dir={frames}", f"seed={args.seed}"],
                          cwd=BUILD, capture_output=True, text=True, timeout=7200)
    (frames / "dynamical.log").write_text(proc.stdout + proc.stderr)
    rendered = sorted(frames.glob("frame_*.png"))
    print(f"exit {proc.returncode}, {len(rendered)} frames in {time.monotonic() - began:.0f} s", flush=True)
    if proc.returncode != 0 or len(rendered) != count:
        print((proc.stdout + proc.stderr)[-3000:])
        sys.exit(1)

    # A capture that waited for the species is late by as many frames, and the rest with it
    late = sum(int(a) - int(b) for a, b in re.findall(r"ready at frame (\d+) instead of (\d+)", proc.stdout + proc.stderr))
    rows = {}
    with open(log, newline="") as f:
        for r in csv.DictReader(f):
            rows[int(r["frame"])] = {k: float(v) for k, v in r.items()}
    known = sorted(rows)
    if not known:
        sys.exit("the view wrote no log")

    def row(frame):
        while frame not in rows and frame > known[0]:
            frame -= 1
        return rows.get(frame, rows[known[0]])

    series = [row(start + i * every + late) for i in range(count)]
    # An episode that starts again is a fall: the command's clock goes back
    falls, fallen = [], 0
    for i, r in enumerate(series):
        if i and r["time"] < series[i - 1]["time"] - 0.5:
            fallen += 1
        falls.append(fallen)
    xs, zs = [r["x"] for r in series], [r["z"] for r in series]
    cx, cz = 0.5 * (min(xs) + max(xs)), 0.5 * (min(zs) + max(zs))
    span = max(max(xs) - min(xs), max(zs) - min(zs), 4 * length) * 1.15

    slow, fast = ranges["speed"]
    tight, wide = ranges["radius"]
    f_title, f_text, f_small = font(17, True), font(16), font(12)
    ground = "level hard ground"
    title = f"{species}, the policy of {args.run}, on {ground}: the ground it was trained on"

    def chart_xy(box, speed, curvature):
        x0, y0, x1, y1 = box
        u = 0.5 - 0.5 * curvature / (1.15 / tight)          # left turns on the left
        v = (speed - (bottom - 0.1)) / ((top + 0.1) - (bottom - 0.1))
        return x0 + u * (x1 - x0), y1 - v * (y1 - y0)

    trail = []
    for i, r in enumerate(series):
        image = Image.open(rendered[i]).convert("RGBA")
        over = Image.new("RGBA", image.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(over)
        w, h = image.size

        speed, turn = r["speed"], r["turn"]
        curvature = turn * length                               # 1 / radius in lengths, > 0 left
        running = runs and speed / walk > 0.5 * (fast + run * ranges["run_speed"][0])
        if speed < 0.05 * walk:
            way = ""
        elif abs(curvature) < 0.5 / (max(wide, ranges.get("run_radius", [0, 0])[1]) * 1.5):
            way = ", straight ahead"
        else:
            way = f", turning {'left' if turn > 0 else 'right'} on a radius of {1 / abs(curvature):.1f} lengths ({1 / abs(turn):.1f} m)"
        o = orders[int(r["order"])] if r["order"] >= 0 else None
        how = "drawn as in training, a new one every {:.0f} to {:.0f} s".format(*ranges["segment"]) if o and o[1] <= 0 else "given"
        did = math.hypot(r["vx"], r["vz"])
        d.rectangle([0, 0, w, 78], fill=(20, 24, 28, 150))
        d.text((12, 6), title, font=f_title, fill=(255, 255, 255, 255))
        if speed < 0.05 * walk:
            asked = "stand still"
        elif running:
            asked = f"run, {speed / (run * walk):.2f} of its run's speed ({speed:.2f} m/s)"
        else:
            asked = f"walk, {speed / walk:.2f} of its walk's speed ({speed:.2f} m/s)" if runs or bottom == 0. else f"{speed / walk:.2f} of its walk's speed ({speed:.2f} m/s)"
        d.text((12, 30), f"order ({how}): {asked}{way}", font=f_text, fill=(255, 226, 150, 255))
        d.text((12, 52), f"it does: {did:.2f} m/s, its trunk at {r['trunk']:.2f} of its standing height, falls so far: {falls[i]}"
               f"        t = {i / args.fps:5.1f} s", font=f_text, fill=(225, 235, 245, 255))

        # Where the order is in the trained ranges: speed against turn
        box = (12, h - 152, 232, h - 12)
        d.rectangle(box, fill=(20, 24, 28, 150))
        d.text((box[0] + 6, box[1] + 3), "the trained orders (speed, turn)", font=f_small, fill=(255, 255, 255, 255))
        inner = (box[0] + 30, box[1] + 24, box[2] - 10, box[3] - 20)
        for side in (1, -1):
            a = chart_xy(inner, fast, side / tight)
            b = chart_xy(inner, slow, side / wide)
            d.rectangle([min(a[0], b[0]), a[1], max(a[0], b[0]), b[1]], fill=(120, 170, 120, 150))
        a, b = chart_xy(inner, fast, 0), chart_xy(inner, slow, 0)
        d.line([a, b], fill=(120, 200, 120, 255), width=3)
        d.text((inner[0] - 26, a[1] - 7), f"{fast:.1f}", font=f_small, fill=(230, 230, 230, 255))
        d.text((inner[0] - 26, b[1] - 7), f"{slow:.1f}", font=f_small, fill=(230, 230, 230, 255))
        if runs:
            # The run: its own speeds and turns
            for side in (1, -1):
                ra = chart_xy(inner, top, side / ranges["run_radius"][0])
                rb = chart_xy(inner, run * ranges["run_speed"][0], side / ranges["run_radius"][1])
                d.rectangle([min(ra[0], rb[0]), ra[1], max(ra[0], rb[0]), rb[1]], fill=(120, 150, 190, 150))
            ra, rb = chart_xy(inner, top, 0), chart_xy(inner, run * ranges["run_speed"][0], 0)
            d.line([ra, rb], fill=(130, 170, 230, 255), width=3)
            d.text((inner[0] - 26, ra[1] - 7), f"{top:.1f}", font=f_small, fill=(230, 230, 230, 255))
        if bottom == 0.:
            z = chart_xy(inner, 0., 0)
            d.ellipse([z[0] - 5, z[1] - 5, z[0] + 5, z[1] + 5], fill=(200, 200, 200, 220))
            d.text((inner[0] - 26, z[1] - 7), "0", font=f_small, fill=(230, 230, 230, 255))
        d.text((inner[0], box[3] - 17), "left", font=f_small, fill=(230, 230, 230, 255))
        d.text((inner[2] - 26, box[3] - 17), "right", font=f_small, fill=(230, 230, 230, 255))
        d.text((0.5 * (inner[0] + inner[2]) - 20, box[3] - 17), "straight", font=f_small, fill=(230, 230, 230, 255))
        trail.append(chart_xy(inner, speed / walk, curvature))
        if len(trail) > 1:
            d.line(trail, fill=(255, 226, 150, 200), width=1)
        p = trail[-1]
        d.ellipse([p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4], fill=(255, 190, 60, 255), outline=(40, 30, 0, 255))

        # The path from above, -z up: where it went, where it is asked to go
        m = (w - 192, h - 192, w - 12, h - 12)
        d.rectangle(m, fill=(20, 24, 28, 150))
        d.text((m[0] + 6, m[1] + 3), f"its path from above ({span:.0f} m across)", font=f_small, fill=(255, 255, 255, 255))

        def on_map(x, z):
            return (0.5 * (m[0] + m[2]) + (x - cx) / span * (m[2] - m[0]), 0.5 * (m[1] + m[3]) + 10 + (z - cz) / span * (m[3] - m[1] - 20))

        if i:
            d.line([on_map(xs[k], zs[k]) for k in range(0, i + 1)], fill=(225, 235, 245, 255), width=2)
        here = on_map(r["x"], r["z"])
        yaw = r["yaw"]
        tip = (here[0] - math.sin(yaw) * 16, here[1] - math.cos(yaw) * 16)
        d.line([here, tip], fill=(255, 190, 60, 255), width=3)
        d.ellipse([here[0] - 3, here[1] - 3, here[0] + 3, here[1] + 3], fill=(255, 255, 255, 255))

        Image.alpha_composite(image, over).convert("RGB").save(rendered[i])
        if i % 600 == 0:
            print(f"frame {i} of {count}", flush=True)

    shutil.copyfile(log, out.with_suffix(".csv"))
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-framerate", str(args.fps), "-i", str(frames / "frame_%05d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21", "-preset", "slow", str(out)], check=True)
    if not args.keep_frames:
        keep = {rendered[int(count * k / 8)] for k in range(8)} | {rendered[-1]}
        for f in rendered:
            if f not in keep:
                f.unlink()
    did = [math.hypot(r["vx"], r["vz"]) for r in series]
    print(f"falls: {fallen}; mean speed {sum(did) / len(did):.2f} m/s; the path spans {span / 1.15:.0f} m")
    print(out)


if __name__ == "__main__":
    main()
