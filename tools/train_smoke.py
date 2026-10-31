#!/usr/bin/env python3
"""L3.1 [slow]: the real trainer (the fork's mlagents-learn, build/venv) against TrainingEnv.exe mode=null.

    build/venv/Scripts/python tools/train_smoke.py [--steps 2000] [--num-envs 1] [--env build/RelWithDebInfo/TrainingEnv]

Passes when mlagents-learn exits 0, the run directory holds TensorBoard events and run_logs/Player-0.log exists.
wandb runs offline, its files under the results directory (the fork always creates a wandb writer).
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENV = ROOT / "build" / "venv" / "Scripts"


def run_trainer(env, num_envs, steps, results, run_id, base_port, config, extra_env_args=(), timeout=1800):
    """Runs mlagents-learn once; returns (exit code, console text, seconds)."""
    cfg = pathlib.Path(tempfile.mkdtemp()) / "config.yaml"
    text = pathlib.Path(config).read_text()
    text = re.sub(r"max_steps: \d+", f"max_steps: {steps}", text)
    cfg.write_text(text)
    cmd = [str(VENV / "mlagents-learn.exe"), str(cfg), f"--env={env}", f"--num-envs={num_envs}", "--no-graphics",
           f"--run-id={run_id}", f"--results-dir={results}", f"--base-port={base_port}", "--force",
           "--env-args", "mode=null", *extra_env_args]
    print("+", " ".join(cmd), flush=True)
    # The fork always creates a wandb writer; offline keeps it local (disabled makes wandb.init raise, which drops
    # every default stats writer, TensorBoard included)
    (pathlib.Path(results) / "wandb").mkdir(parents=True, exist_ok=True)
    # WANDB_MODE alone is overridden by the fork (see tools/wandb_offline/sitecustomize.py)
    environment = dict(os.environ, WANDB_MODE="offline", WANDB_SILENT="true", WANDB_DIR=str(pathlib.Path(results) / "wandb"),
                       PYTHONPATH=str(ROOT / "tools" / "wandb_offline"))
    start = time.monotonic()
    proc = subprocess.run(cmd, cwd=ROOT, env=environment, capture_output=True, text=True, timeout=timeout)
    seconds = time.monotonic() - start
    shutil.rmtree(cfg.parent, ignore_errors=True)
    return proc.returncode, proc.stdout + proc.stderr, seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--steps", type=int, default=2000)
    parser.add_argument("--num-envs", type=int, default=1)
    parser.add_argument("--env", default=str(ROOT / "build" / "RelWithDebInfo" / "TrainingEnv"))
    parser.add_argument("--results", default=str(ROOT / "build" / "artifacts" / "P3" / "smoke"))
    parser.add_argument("--run-id", default="null_smoke")
    parser.add_argument("--base-port", type=int, default=5605)
    args = parser.parse_args()

    results = pathlib.Path(args.results)
    code, text, seconds = run_trainer(args.env, args.num_envs, args.steps, results, args.run_id, args.base_port,
                                      ROOT / "tests" / "data" / "null_ppo.yaml")
    (results / f"{args.run_id}.console.txt").parent.mkdir(parents=True, exist_ok=True)
    (results / f"{args.run_id}.console.txt").write_text(text)
    print(text[-3000:])

    run_dir = results / args.run_id
    events = list(run_dir.rglob("events.out.tfevents.*"))
    player_log = run_dir / "run_logs" / "Player-0.log"
    checks = {
        "exit code 0": code == 0,
        "TensorBoard events in the run directory": bool(events),
        "run_logs/Player-0.log exists": player_log.exists(),
    }
    for name, ok in checks.items():
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
    print(f"{seconds:.1f} s, events: {[str(e.relative_to(results)) for e in events]}")
    sys.exit(0 if all(checks.values()) else 1)


if __name__ == "__main__":
    main()
