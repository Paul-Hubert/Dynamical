#!/usr/bin/env python3
"""The slope band of an animal's procedural walk on its physical body: on planes tilted along and across its heading,
where the limits hold, nothing jumps or flips, the fall test stays quiet and the speed is the commanded one
(docs/physical-agents/reports/animal-agents.md; the terrain walker's report did this for the man).

    build/venv/Scripts/python tools/animal_slope_band.py --species deer_red [--out build/runs/animal_slopes]

Records the walk (scene animal_demo, mode=record-demo) at the clip's speed straight up or down each plane, with the
checks of every recording, and writes <out>/<species>.json and .md.
"""

import argparse
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "RelWithDebInfo" / "TrainingEnv.exe"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--species", required=True)
    parser.add_argument("--pitch", default="-20,-15,-10,-7,-5,0,5,10,15,20", help="degrees: positive climbs")
    parser.add_argument("--roll", default="0,8,12,15", help="degrees across the heading")
    parser.add_argument("--decisions", type=int, default=1500)
    parser.add_argument("--out", default=str(ROOT / "build" / "runs" / "animal_slopes"))
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for roll in [float(v) for v in args.roll.split(",")]:
        for pitch in [float(v) for v in args.pitch.split(",")]:
            checks = out / f"{args.species}_p{pitch:+.0f}_r{roll:.0f}.checks.json"
            demo = checks.with_suffix("").with_suffix(".demo")
            subprocess.run([str(EXE), "scene=animal_demo", "mode=record-demo", f"var.body=animals/bodies/{args.species}.body.json", f"var.pitch={pitch}",
                            f"var.roll={roll}", "var.heading=0", "var.speed=1", f"var.decisions={args.decisions}", f"var.out={demo}", f"var.checks={checks}"],
                           cwd=ROOT, capture_output=True, text=True)
            demo.unlink(missing_ok=True)
            if not checks.exists():
                rows.append({"pitch": pitch, "roll": roll, "error": "no recording"})
                continue
            c = json.loads(checks.read_text())
            ok = c["limit_ticks"] == 0 and c["falls"] == 0 and c["heading_flips"] == 0 and c.get("axis_jumps", 0) == 0 and abs(c["speed_ratio"] - 1) < 0.03
            rows.append({"pitch": pitch, "roll": roll, "ok": ok, "limit_excess_deg": c["limit_excess_deg"], "limit_ticks": c["limit_ticks"],
                         "limit_joint": c.get("limit_joint_name", ""), "falls": c["falls"], "fall_segment": c.get("fall_segment_name", ""),
                         "axis_jump": c["axis_jump"], "axis_jumps": c.get("axis_jumps", 0), "heading_flips": c["heading_flips"],
                         "lowest_over_height": c["lowest"] / c["height"], "speed_ratio": c["speed_ratio"], "ticks": c["ticks"]})
            checks.unlink()
    (out / f"{args.species}.json").write_text(json.dumps(rows, indent=1))
    lines = [f"Slope band of {args.species}: the procedural walk at the clip's speed, straight up (+) or down (-) a plane, {args.decisions} decisions each.", "",
             "| Across | Along | In the band | Limits (deg over, ticks, joint) | Fall test (ticks, body) | Largest axis change (jumps over 0.6) | Lowest point / height | Speed / command |",
             "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        if "error" in r:
            lines.append(f"| {r['roll']:.0f} | {r['pitch']:+.0f} | {r['error']} | | | | | |")
            continue
        lines.append(f"| {r['roll']:.0f} | {r['pitch']:+.0f} | {'yes' if r['ok'] else 'no'} | {r['limit_excess_deg']:.1f}, {r['limit_ticks']}, {r['limit_joint']} | {r['falls']}, {r['fall_segment']} | "
                     f"{r['axis_jump']:.2f} ({r['axis_jumps']}) | {r['lowest_over_height']:+.3f} | {r['speed_ratio']:.3f} |")
    (out / f"{args.species}.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
