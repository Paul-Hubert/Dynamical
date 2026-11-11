#!/usr/bin/env python3
"""Evaluates a trained animal on what it was trained on (docs/physical-agents/reports/animal-agents.md): the frozen
TrainingEnv.exe, body, params and ranges of its run, the policy's mean action, episodes truncated as in training.

    build/venv/Scripts/python tools/animal_eval.py --run animal_deer_red_20m [--episodes 500] [--checkpoint 5000000]
    build/venv/Scripts/python tools/animal_eval.py --run R --species deer_red --stochastic

A body generated again since the run is not the body the policy knows: the run's own (<run>_env) is always used.
--terrain evaluates on the marching cubes terrain (animal_terrain_eval). --var NAME=VALUE sets a scene variable
(speed, soft_depth, route_up, ...). Writes build/runs/eval/<run>/<name>.json and prints it: falls (and what touched
the ground), episode length, the speed along the commanded heading against the command, the heading error.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run", required=True)
    parser.add_argument("--species", help="default: the one body frozen with the run")
    parser.add_argument("--checkpoint", type=int, help="the checkpoint nearest this step; default: the final policy")
    parser.add_argument("--episodes", type=int, default=300)
    parser.add_argument("--worlds", type=int, default=16)
    parser.add_argument("--stochastic", action="store_true", help="sample the actions as in training")
    parser.add_argument("--terrain", action="store_true")
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--name")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    args = parser.parse_args()

    results = pathlib.Path(args.results)
    env = results / f"{args.run}_env"
    bodies = sorted((env / "resources" / "animals" / "bodies").glob("*.body.json"))
    if args.species:
        bodies = [b for b in bodies if b.name == f"{args.species}.body.json"]
    if len(bodies) != 1:
        sys.exit(f"{env}: {len(bodies)} bodies frozen with the run, name one with --species")
    body = bodies[0]
    species = body.name[:-len(".body.json")]
    behavior = f"Animal_{species}"
    if args.checkpoint:
        found = []
        for f in (results / args.run / behavior).glob(f"{behavior}-*.onnx"):
            m = re.match(rf"{re.escape(behavior)}-(\d+)\.onnx$", f.name)
            if m:
                found.append((abs(int(m.group(1)) - args.checkpoint), f))
        if not found:
            sys.exit(f"no checkpoint of {behavior} in {results / args.run}")
        model = min(found)[1]
    else:
        model = results / args.run / f"{behavior}.onnx"
    if not model.exists():
        sys.exit(f"{model} is missing")
    ranges = sorted((env / "resources" / "agents" / "ranges").glob("*.json"))
    config = (results / f"{args.run}.console.txt").read_text(encoding="utf-8", errors="replace") if (results / f"{args.run}.console.txt").exists() else ""
    used = re.search(r"var\.ranges=(\S+)", config)
    ranges_file = pathlib.Path(used.group(1)) if used else ranges[0]

    name = args.name or (model.stem + ("_terrain" if args.terrain else "") + ("_stochastic" if args.stochastic else ""))
    out = results / "eval" / args.run / f"{name}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    scene = "animal_terrain_eval" if args.terrain else "animal_eval"
    cmd = [str(env / "TrainingEnv.exe"), f"scene={scene}", "mode=eval", f"var.body={body}", f"var.ranges={ranges_file}",
           f"var.params={env / 'resources' / 'agents' / 'params' / 'animal.params.json'}", f"var.model={model}", f"var.episodes={args.episodes}",
           f"var.num_worlds={args.worlds}", f"var.stochastic={1 if args.stochastic else 0}", f"var.out={out}", *[f"var.{v}" for v in args.var]]
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode or not out.exists():
        print((result.stdout + result.stderr)[-2000:])
        sys.exit(1)
    summary = json.loads(out.read_text())
    summary["run"] = args.run
    summary["body"] = str(body)
    out.write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{args.run} {model.name}: {summary['episodes']} episodes, falls {100 * summary['fall_rate']:.1f} % "
          f"({summary['falls_trunk_low']:.0f} trunk low, {summary['falls_body_touch']:.0f} a body on the ground {summary['touched']}), "
          f"{summary['mean_length_seconds']:.1f} s of {summary['episode_seconds']:.1f}, "
          f"speed along the command {summary['progress_speed']:.3f} m/s of {summary['commanded_speed']:.3f} commanded ({summary['speed_ratio']:.2f}), "
          f"heading error {summary['mean_heading_error_deg']:.1f} deg")
    print(out)


if __name__ == "__main__":
    main()
