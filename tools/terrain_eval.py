#!/usr/bin/env python3
"""Deterministic evaluation of a walker policy on the marching cubes terrain and on flat ground, split by slope.

    build/venv/Scripts/python tools/terrain_eval.py --model FILE.onnx --name NAME [--episodes 500] [--worlds 32]
                                                    [--out build/runs/eval] [--var NAME=VALUE ...] [--skip-flat]

Runs TrainingEnv.exe mode=eval on walker_terrain_eval and on walker_eval (the policy's mean action, episodes truncated
as in training), then reads the stretches each episode is cut in: one per target draw (400 frames at 60 Hz, 6.7 s),
with the ground the walker crossed on it. A stretch is

    level    its mean slope along the way under 3 degrees, and under 4 across;
    up       3 degrees or more up along the way (gentle to 10, steep beyond);
    down     3 degrees or more down;
    across   under 3 along, 4 or more across.

Per band: stretches, falls (stretches that ended in a kill), falls per 100 m walked, and over the stretches walked to
their end: speed along the commanded direction (what the reward measures), cadence (foot strikes per second, both
feet) and step length (path length per strike). Writes <out>/<name>.terrain.{json,legs.csv}, <name>.flat.* and
<name>.bands.{md,json}.
"""

import argparse
import csv
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "RelWithDebInfo" / "TrainingEnv.exe"
BANDS = ["level", "up gentle (3 to 10)", "up steep (10 to 20)", "down (3 to 7)", "across (4 to 12)"]


def band(row):
    pitch, across = float(row["pitch_deg"]), abs(float(row["mean_across_deg"]))
    if pitch >= 10.:
        return "up steep (10 to 20)"
    if pitch >= 3.:
        return "up gentle (3 to 10)"
    if pitch <= -3.:
        return "down (3 to 7)"
    return "across (4 to 12)" if across >= 4. else "level"


def run_eval(scene, model, episodes, worlds, prefix, extra, seed):
    summary, legs, log = (pathlib.Path(f"{prefix}{suffix}") for suffix in (".json", ".legs.csv", ".log"))
    cmd = [str(EXE), f"scene={scene}", "mode=eval", f"var.model={model}", f"var.episodes={episodes}", f"var.num_worlds={worlds}",
           f"var.out={summary}", f"var.legs={legs}", f"seed={seed}", *[f"var.{v}" for v in extra]]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    log.write_text(proc.stdout + proc.stderr)
    if proc.returncode != 0 or not summary.exists():
        sys.exit(f"eval of {scene} failed: see {log}")
    with open(legs, newline="") as f:
        rows = list(csv.DictReader(f))
    return json.loads(summary.read_text()), rows


def aggregate(rows):
    out = {}
    for name in BANDS + ["all"]:
        mine = [r for r in rows if name == "all" or band(r) == name]
        whole = [r for r in mine if r["fell"] == "0" and r["route_lost"] == "0" and float(r["seconds"]) > 6.]
        seconds = sum(float(r["seconds"]) for r in whole)
        distance = sum(float(r["distance"]) for r in whole)
        strikes = sum(int(r["strikes"]) for r in whole)
        walked = sum(float(r["distance"]) for r in mine)
        falls = sum(r["fell"] == "1" for r in mine)
        out[name] = {
            "stretches": len(mine), "falls": falls, "fall_rate": falls / len(mine) if mine else None,
            "falls_per_100m": 100. * falls / walked if walked > 0 else None, "metres": walked,
            "whole_stretches": len(whole),
            "speed": sum(float(r["progress"]) for r in whole) / seconds if seconds > 0 else None,
            "path_speed": distance / seconds if seconds > 0 else None,
            "cadence": strikes / seconds if seconds > 0 else None,
            "step_length": distance / strikes if strikes > 0 else None,
            "mean_pitch": sum(float(r["pitch_deg"]) for r in mine) / len(mine) if mine else None,
            "mean_across": sum(abs(float(r["mean_across_deg"])) for r in mine) / len(mine) if mine else None,
        }
    return out


def fmt(v, digits=2):
    return "" if v is None else f"{v:.{digits}f}"


def table(title, bands):
    lines = [f"### {title}", "", "| Band | stretches | falls | falls per stretch | falls per 100 m | speed (m/s) | cadence (steps/s) | step length (m) | mean slope along | across |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for name in BANDS + ["all"]:
        b = bands[name]
        if not b["stretches"]:
            continue
        lines.append(f"| {name} | {b['stretches']} | {b['falls']} | {fmt(b['fall_rate'], 3)} | {fmt(b['falls_per_100m'])} | {fmt(b['speed'])} | {fmt(b['cadence'])} | "
                     f"{fmt(b['step_length'])} | {fmt(b['mean_pitch'], 1)} | {fmt(b['mean_across'], 1)} |")
    return lines + [""]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--episodes", type=int, default=500)
    parser.add_argument("--worlds", type=int, default=32)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", default=str(ROOT / "build" / "runs" / "eval"))
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a variable of walker_terrain_eval, repeatable")
    parser.add_argument("--skip-flat", action="store_true")
    args = parser.parse_args()

    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    model = str(pathlib.Path(args.model).resolve())
    result = {"model": model, "episodes": args.episodes}
    lines = [f"## {args.name}", "", f"`{model}`, {args.episodes} episodes per ground, deterministic actions.", ""]
    for ground, scene, extra in (("terrain", "walker_terrain_eval", args.var), ("flat", "walker_eval", [])):
        if ground == "flat" and args.skip_flat:
            continue
        summary, rows = run_eval(scene, model, args.episodes, args.worlds, out / f"{args.name}.{ground}", extra, args.seed)
        bands = aggregate(rows)
        lost = sum(r["route_lost"] == "1" for r in rows)
        result[ground] = {"summary": {k: summary[k] for k in ("episodes", "dead_rate", "mean_length_ticks", "mean_speed", "steps_per_second", "step_length") if k in summary},
                          "episodes_ended_without_a_route": lost, "bands": bands}
        lines += table(f"{ground}: {summary['episodes']} episodes, {100 * summary['dead_rate']:.1f} % ended in a fall, mean length {summary['mean_length_ticks'] / 60:.1f} s, "
                       f"{lost} ended with no direction in the band", bands)
    (out / f"{args.name}.bands.json").write_text(json.dumps(result, indent=2))
    (out / f"{args.name}.bands.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
