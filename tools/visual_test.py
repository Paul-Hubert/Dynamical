#!/usr/bin/env python3
"""Drives the visual and probe tests (CTest label gpu): runs dynamical, then compares the result.

    visual_test.py golden  --exe E --imgdiff I --script S --out DIR [--golden-dir G] [--expect-fail]
        capture the script's shots, imgdiff each against its golden. --expect-fail inverts the verdict
        (the altered-capture check: imgdiff must see the change)
    visual_test.py shots   --exe E --script S --out DIR
        capture the script's shots (no golden: checked by eye in the phase report)
    visual_test.py repeat  --exe E --imgdiff I --script S --out DIR [--mae 0.1]
        capture twice, imgdiff the two runs with only a mean-error threshold (determinism)
    visual_test.py probe   --exe E --out DIR [--runs 2] [--golden CSV] [--tol 1e-6] -- <dynamical arguments>
        run the probe, compare runs with each other (and with a golden), max position difference <= tol
"""

import argparse
import csv
import json
import pathlib
import subprocess
import sys


def run(cmd, timeout=600):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    result = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=timeout)
    if result.stdout:
        print(result.stdout, end="", flush=True)
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr, flush=True)
    return result.returncode


def shot_names(script):
    return [shot["name"] for shot in json.loads(pathlib.Path(script).read_text())["shots"]]


def capture(args, out):
    out.mkdir(parents=True, exist_ok=True)
    for name in shot_names(args.script):
        (out / f"{name}.png").unlink(missing_ok=True)
    cmd = [args.exe, f"capture={pathlib.Path(args.script).resolve()}", f"capture_dir={out.resolve()}"]
    # Goldens are compared on any GPU: without the ray traced lighting, whose pictures depend on it (docs/lighting.md)
    if not getattr(args, "rays", True):
        cmd.append("ray_tracing=false")
    code = run(cmd)
    if code != 0:
        sys.exit(f"capture failed with exit code {code}")


def golden(args):
    out = pathlib.Path(args.out)
    capture(args, out)
    failed = []
    for name in shot_names(args.script):
        code = run([args.imgdiff, out / f"{name}.png", pathlib.Path(args.golden_dir) / f"{name}.png"])
        if code == 2:
            sys.exit(f"imgdiff could not compare {name}")
        if (code != 0) != args.expect_fail:
            failed.append(name)
    if failed:
        verdict = "did not detect the change in" if args.expect_fail else "failed"
        sys.exit(f"imgdiff {verdict}: {', '.join(failed)}")


def shots(args):
    out = pathlib.Path(args.out)
    capture(args, out)
    missing = [name for name in shot_names(args.script) if not (out / f"{name}.png").exists()]
    if missing:
        sys.exit(f"missing captures: {', '.join(missing)}")


def repeat(args):
    out = pathlib.Path(args.out)
    capture(args, out / "a")
    capture(args, out / "b")
    failed = []
    for name in shot_names(args.script):
        code = run([args.imgdiff, out / "b" / f"{name}.png", out / "a" / f"{name}.png", "--mae", str(args.mae),
                    "--fraction", "1", "--always-heatmap"])
        if code != 0:
            failed.append(name)
    if failed:
        sys.exit(f"captures differ between runs: {', '.join(failed)}")


def read_probe(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return {(row["frame"], row["index"]): row for row in rows}


def compare_probes(a, b, tol, label):
    pa, pb = read_probe(a), read_probe(b)
    if pa.keys() != pb.keys():
        missing = sorted(set(pa) ^ set(pb))[:5]
        print(f"{label}: rows differ, e.g. (frame, index) {missing}")
        return False
    worst, where = 0.0, None
    for key in pa:
        for axis in ("x", "y", "z"):
            d = abs(float(pa[key][axis]) - float(pb[key][axis]))
            if d > worst:
                worst, where = d, key
    print(f"{label}: {len(pa)} rows, max position difference {worst:.3e} m at (frame, index) {where}, tolerance {tol:g}")
    return worst <= tol


def probe(args):
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    files = []
    for i in range(args.runs):
        path = (out / f"probe_{i}.csv").resolve()
        path.unlink(missing_ok=True)
        code = run([args.exe, f"probe={path}", *args.rest])
        if code != 0 or not path.exists():
            sys.exit(f"probe run {i} failed with exit code {code}")
        files.append(path)
    ok = all(compare_probes(files[0], f, args.tol, f"run 0 vs run {i}") for i, f in enumerate(files[1:], 1))
    if args.golden:
        ok = compare_probes(args.golden, files[0], args.tol, "golden vs run 0") and ok
    if not ok:
        sys.exit("probe mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    g = sub.add_parser("golden")
    g.add_argument("--exe", required=True)
    g.add_argument("--imgdiff", required=True)
    g.add_argument("--script", required=True)
    g.add_argument("--out", required=True)
    g.add_argument("--golden-dir", default=str(pathlib.Path(__file__).resolve().parent.parent / "tests" / "golden" / "images"))
    g.add_argument("--expect-fail", action="store_true")
    g.add_argument("--rays", action="store_true", help="with the ray traced lighting where the GPU has it")

    s = sub.add_parser("shots")
    s.add_argument("--exe", required=True)
    s.add_argument("--script", required=True)
    s.add_argument("--out", required=True)

    r = sub.add_parser("repeat")
    r.add_argument("--exe", required=True)
    r.add_argument("--imgdiff", required=True)
    r.add_argument("--script", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--mae", type=float, default=0.1)
    r.add_argument("--rays", action="store_true", help="with the ray traced lighting where the GPU has it")

    p = sub.add_parser("probe")
    p.add_argument("--exe", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--runs", type=int, default=2)
    p.add_argument("--golden")
    p.add_argument("--tol", type=float, default=1e-6)
    p.add_argument("rest", nargs=argparse.REMAINDER)

    args = parser.parse_args()
    if getattr(args, "rest", None) and args.rest[0] == "--":
        args.rest = args.rest[1:]
    {"golden": golden, "shots": shots, "repeat": repeat, "probe": probe}[args.command](args)


if __name__ == "__main__":
    main()
