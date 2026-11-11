#!/usr/bin/env python3
"""The readiness table of the physical animals (docs/physical-agents/reports/animal-agents.md): for every animal of
the library, what it has of the chain that trains it.

    build/venv/Scripts/python tools/animal_status.py [--prefix animal] [--ranges walk_basic] [--results build/runs]
                                                     [--out build/runs/animal_readiness]

Reads, and runs nothing:
- resources/animals/library.json and bodies/summary.json (animal_body.exe --species all --check: legs, bodies, mass,
  the standing and the limp test);
- the checks of the newest cached demonstration of each animal on the ranges (<results>/demo_cache/*.checks.json,
  written by tools/train_animal.py);
- the short training <results>/<prefix>_<species>_<ranges> (tools/train_animal.py --species all --max-steps 100000
  --prefix <prefix>): did mlagents-learn end with 0, how far it got, is a value NaN.
Writes <out>.json and <out>.md.
"""

import argparse
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "resources" / "animals"


def newest(paths):
    paths = list(paths)
    return max(paths, key=lambda p: p.stat().st_mtime) if paths else None


def training(results, run):
    console = results / f"{run}.console.txt"
    if not console.exists():
        return None
    text = console.read_text(encoding="utf-8", errors="replace")
    steps = [int(m.group(1)) for m in re.finditer(r"Step: (\d+)\.", text)]
    rewards = [float(m.group(1)) for m in re.finditer(r"Mean Reward: (-?\d+(?:\.\d+)?|nan)", text)]
    exits = re.findall(r"mlagents-learn exited with (-?\d+) after (\d+) s", text)
    onnx = list((results / run).glob("*.onnx")) if (results / run).exists() else []
    return {
        "steps": max(steps) if steps else 0,
        "exit": int(exits[-1][0]) if exits else None,
        "seconds": int(exits[-1][1]) if exits else None,
        "nan": any(r != r for r in rewards) or bool(re.search(r"nan", text, flags=re.I)),
        "onnx": bool(onnx),
        "last_reward": rewards[-1] if rewards else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prefix", default="animal")
    parser.add_argument("--ranges", default="walk_basic")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    parser.add_argument("--out", default=str(ROOT / "build" / "runs" / "animal_readiness"))
    args = parser.parse_args()
    results = pathlib.Path(args.results)

    library = json.loads((LIBRARY / "library.json").read_text())["species"]
    summary = {row["species"]: row for row in json.loads((LIBRARY / "bodies" / "summary.json").read_text())}
    rows = []
    for entry in library:
        species = entry["id"]
        row = {"species": species, "height": entry["height"], "length": entry["length"], "walk": bool(entry.get("walk"))}
        body_row = summary.get(species, {})
        body_path = LIBRARY / "bodies" / f"{species}.body.json"
        missing = []
        if not entry.get("walk"):
            row["reason"] = "no walk clip: no agent"
        elif "error" in body_row or not body_path.exists():
            row["reason"] = body_row.get("error", "no body")
        else:
            body = json.loads(body_path.read_text())
            segments = len(body["segments"])
            row.update({
                "legs": len(body["legs"]), "bodies": segments, "joints": len(body["joints"]), "mass": body["total_mass"],
                "observations": 2 * (7 + 9 * segments), "actions": 8 * len(body["joints"]), "substeps": body["substeps"],
                "velocity_steps": body["velocity_steps"], "stand_clip": body["stand_clip"] or "bind pose",
                "hinges": sum(1 for j in body["joints"] if j["hinge"]),
            })
            stand, limp = body_row.get("stand"), body_row.get("limp")
            if stand:
                row["stands"] = bool(stand["stands"])
                row["stand"] = {k: stand[k] for k in ("trunk_drop", "drift", "tilt", "max_speed", "fell")}
                row["limp_falls_and_rests"] = bool(limp["fell"] and limp["rests"])
                if not stand["stands"]:
                    missing.append("stands (settles %.1f deg, %.0f mm)" % (stand["tilt"], 1000 * stand["trunk_drop"]) if not stand["fell"] else "falls standing")
                if not row["limp_falls_and_rests"]:
                    missing.append("limp test")
            else:
                missing.append("standing test not run")
            checks_file = newest((results / "demo_cache").glob(f"{species}_{args.ranges}_*.checks.json"))
            if checks_file:
                c = json.loads(checks_file.read_text())
                h = c["height"]
                demo = {
                    "decisions": c["decisions"], "limit_excess_deg": c["limit_excess_deg"], "limit_ticks": c["limit_ticks"],
                    "falls": c["falls"], "heading_flips": c["heading_flips"], "axis_jump": c["axis_jump"], "axis_jumps": c.get("axis_jumps", 0),
                    "velocity_jump": c["velocity_jump"], "lowest_over_height": c["lowest"] / h, "speed_ratio": c["speed_ratio"],
                    "speed_error": c["speed_error"], "stretch_over_height": c["stretch"] / h, "foot_slide": c["foot_slide"],
                }
                failed = []
                if c["limit_ticks"]:
                    failed.append("limits %.0f deg over on %d ticks (%s)" % (c["limit_excess_deg"], c["limit_ticks"], c.get("limit_joint_name", "")))
                if c["falls"]:
                    failed.append("the fall test would end it on %d ticks (%s)" % (c["falls"], c.get("fall_segment_name", "") or "trunk low"))
                if c["heading_flips"]:
                    failed.append("%d heading flips" % c["heading_flips"])
                if c.get("axis_jumps", 0):
                    failed.append("%d jumps of an axis (%s)" % (c["axis_jumps"], c.get("axis_segment_name", "")))
                if c["lowest"] / h < -0.03:
                    failed.append("a body %.0f %% of the height under the ground (%s)" % (-100 * c["lowest"] / h, c.get("lowest_segment_name", "")))
                if abs(c["speed_ratio"] - 1) > 0.03:
                    failed.append("speed %.2f of the command" % c["speed_ratio"])
                demo["checked"] = not failed
                demo["failed"] = failed
                row["demo"] = demo
                if failed:
                    missing.append("demo: " + "; ".join(failed))
            else:
                missing.append("no demonstration recorded")
            run = training(results, f"{args.prefix}_{species}_{args.ranges}")
            if run:
                row["training"] = run
                if not (run["exit"] == 0 and run["onnx"] and not run["nan"]):
                    missing.append("short training: exit %s, %d steps%s" % (run["exit"], run["steps"], ", NaN" if run["nan"] else ""))
            else:
                missing.append("no short training")
            row["missing"] = missing
            # Trains: the whole chain runs. Ready: and every check of the body and of the demonstration passes
            row["trains"] = "demo" in row and bool(run) and run["exit"] == 0 and run["onnx"] and not run["nan"]
            row["ready"] = row["trains"] and bool(row.get("stands")) and row.get("limp_falls_and_rests", False) and row["demo"]["checked"]
        rows.append(row)

    out = pathlib.Path(args.out)
    out.with_suffix(".json").write_text(json.dumps(rows, indent=1))
    lines = ["| Animal | Legs | Bodies / joints | Mass (kg) | Obs / actions | Stands 10 s | Limp falls, rests | Demo recorded, checked | 100k training | Missing |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    ready = trains = 0
    for row in rows:
        if "bodies" not in row:
            lines.append(f"| {row['species']} | | | | | | | | | {row['reason']} |")
            continue
        ready += row["ready"]
        trains += row["trains"]
        demo = row.get("demo")
        run = row.get("training")
        mass = f"{row['mass']:.0f}" if row["mass"] >= 10 else f"{row['mass']:.2f}"
        demo_missing = [m for m in row["missing"] if m.startswith("demo: ")]
        other_missing = [m for m in row["missing"] if not m.startswith("demo: ")]
        lines.append("| {s} | {legs} | {b} / {j} | {m} | {o} / {a} | {st} | {lp} | {dm} | {tr} | {miss} |".format(
            s=row["species"], legs=row["legs"], b=row["bodies"], j=row["joints"], m=mass, o=row["observations"], a=row["actions"],
            st="yes" if row.get("stands") else "no", lp="yes" if row.get("limp_falls_and_rests") else "no",
            dm=("yes, yes" if demo["checked"] else "yes, " + demo_missing[0][6:]) if demo else "no",
            tr=(f"ran, {run['steps'] // 1000}k in {run['seconds']} s" if run["exit"] == 0 else f"failed ({run['exit']})") if run else "not run",
            miss="; ".join(other_missing)))
    lines.append("")
    lines.append(f"{trains} animals train with one command, {ready} of them pass every check of the body and of the demonstration; "
                 f"{sum(1 for r in rows if 'bodies' in r)} have a body, {len(rows)} are in the library.")
    out.with_suffix(".md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[-1:]))
    print(out.with_suffix(".md"))


if __name__ == "__main__":
    main()
