#!/usr/bin/env python3
"""B3.1: trainer steps/s against TrainingEnv.exe mode=null, at several env counts, a few runs each.

    build/venv/Scripts/python tools/bench_null.py [--envs 6 20] [--runs 3] [--steps 60000]

Steps/s are the trainer's own numbers: the slope between the first and the last "Step: N. Time Elapsed: T s"
console lines of a run (start-up excluded). Results go to build/artifacts/P3/bench_null.csv; append the summary
to docs/physical-agents/benchmarks.md by hand or with --append.
"""

import argparse
import csv
import datetime
import pathlib
import re
import statistics
import subprocess

from train_smoke import ROOT, run_trainer

LINE = re.compile(r"Step: (\d+)\. Time Elapsed: ([\d.]+) s")


def rate(text):
    points = [(int(s), float(t)) for s, t in LINE.findall(text)]
    if len(points) < 2:
        return None, points
    (s0, t0), (s1, t1) = points[0], points[-1]
    return (s1 - s0) / (t1 - t0), points


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--envs", type=int, nargs="+", default=[6, 20])
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--steps", type=int, default=60000)
    parser.add_argument("--env", default=str(ROOT / "build" / "RelWithDebInfo" / "TrainingEnv"))
    parser.add_argument("--config", default=str(ROOT / "tests" / "data" / "null_ppo.yaml"))
    parser.add_argument("--tag", default="", help="suffix of the run ids and the CSV name")
    parser.add_argument("--append", action="store_true", help="append the summary to benchmarks.md")
    args = parser.parse_args()

    out = ROOT / "build" / "artifacts" / "P3"
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for n in args.envs:
        for r in range(args.runs):
            run_id = f"bench_null{args.tag}_{n}env_{r}"
            code, text, seconds = run_trainer(args.env, n, args.steps, out / "bench", run_id, 6005 + 100 * r + n,
                                              args.config, timeout=3600)
            (out / "bench" / f"{run_id}.console.txt").write_text(text)
            steps_per_s, points = rate(text)
            print(f"{n} envs, run {r}: exit {code}, {steps_per_s} steps/s over {len(points)} summaries, {seconds:.0f} s wall", flush=True)
            rows.append({"envs": n, "run": r, "exit": code, "steps_per_s": steps_per_s, "summaries": len(points), "wall_s": round(seconds, 1)})

    with open(out / f"bench_null{args.tag}.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    lines = [f"| Envs | steps/s (mean ± sd) | runs |", "|---|---|---|"]
    for n in args.envs:
        values = [r["steps_per_s"] for r in rows if r["envs"] == n and r["steps_per_s"]]
        if values:
            sd = statistics.stdev(values) if len(values) > 1 else 0.0
            lines.append(f"| {n} | {statistics.mean(values):.1f} ± {sd:.1f} | {', '.join(f'{v:.1f}' for v in values)} |")
    summary = "\n".join(lines)
    print(summary)
    if args.append:
        with open(ROOT / "docs" / "physical-agents" / "benchmarks.md", "a", encoding="utf-8", newline="\n") as f:
            f.write(f"\n## B3.1 — trainer steps/s against `mode=null` (P3)\n\n{datetime.date.today().isoformat()}, commit `{sha}`, "
                    f"`tools/bench_null.py --envs {' '.join(map(str, args.envs))} --runs {args.runs} --steps {args.steps}`.\n\n{summary}\n")


if __name__ == "__main__":
    main()
