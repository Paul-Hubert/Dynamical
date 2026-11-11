#!/usr/bin/env python3
"""Sphere roller PPO run: the fork's mlagents-learn against TrainingEnv.exe scene=<scene> mode=train.

    build/venv/Scripts/python tools/train_sphere.py [--scene sphere_roll|wheel_roll] [--run-id <scene>_<date>]
                                                    [--num-envs 16] [--max-steps 200000000] [--init-path FILE.pt]

- --scene picks the body: sphere_roll (0.25 m, 10 kg, behaviour SphereRoller, training/sphere_roller_ppo.yaml) or
  wheel_roll (the walker's hip height, 69 kg, behaviour WheelRoller, training/wheel_roller_ppo.yaml).
  pursuit is not a sphere: chasers and runners on a field, two behaviours in one run (training/pursuit_ppo.yaml,
  docs/action-agents.md).
- --init-path starts the network (and its normaliser) from a checkpoint .pt instead of from scratch, e.g. the small
  sphere's build/runs/sphere_mc_night_20261031/SphereRoller/checkpoint.pt: it sets the behaviour's init_path.

- The environment is frozen first: TrainingEnv.exe, its DLLs and resources/{scenes,prefabs} are copied to
  <results>/<run-id>_env, so rebuilding the engine during the run does not touch it.
- wandb runs offline (tools/wandb_offline/sitecustomize.py), its files under the run directory.
- The machine is kept awake while the run lasts (SetThreadExecutionState, released when this process exits).
- Console: <results>/<run-id>.console.txt. TensorBoard: build/venv/Scripts/tensorboard --logdir <results>
- Stop early: build/venv/Scripts/python tools/train_sphere.py --stop <run-id>. It drops <run-id>.stop, on which
  this launcher sends CTRL_C to the run's console: mlagents-learn saves and exports the model, the environments exit.
- Detached (survives the terminal): start it with pythonw.exe; mlagents-learn and the environments then run on a
  console of their own without a window.
"""

import argparse
import ctypes
import datetime
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENV = ROOT / "build" / "venv" / "Scripts"
BUILD = ROOT / "build" / "RelWithDebInfo"
CONFIGS = {"sphere_roll": "sphere_roller_ppo.yaml", "wheel_roll": "wheel_roller_ppo.yaml", "pursuit": "pursuit_ppo.yaml"}


def freeze_env(dst):
    dst.mkdir(parents=True)
    for f in BUILD.iterdir():
        if f.name == "TrainingEnv.exe" or f.suffix.lower() == ".dll":
            shutil.copy2(f, dst / f.name)
    for sub in ("scenes", "prefabs"):
        shutil.copytree(ROOT / "resources" / sub, dst / "resources" / sub)


def keep_awake():
    if sys.platform == "win32":
        ES_CONTINUOUS, ES_SYSTEM_REQUIRED = 0x80000000, 0x00000001
        ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)


def send_ctrl_c(pid):
    """CTRL_C to every process on the console of `pid` (mlagents-learn and its environments), not to this one."""
    k = ctypes.windll.kernel32
    k.FreeConsole()
    if not k.AttachConsole(pid):
        return False
    k.SetConsoleCtrlHandler(None, True)
    return bool(k.GenerateConsoleCtrlEvent(0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", default="sphere_roll", choices=sorted(CONFIGS))
    parser.add_argument("--run-id", help="default <scene>_<date>")
    parser.add_argument("--num-envs", type=int, default=16)
    parser.add_argument("--max-steps", type=int, default=200_000_000)
    parser.add_argument("--threads", type=int, default=1, help="TrainingEnv workers per process")
    parser.add_argument("--config", help="default: the scene's, training/" + ", training/".join(CONFIGS.values()))
    parser.add_argument("--init-path", help="a checkpoint .pt to start from")
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable (var.NAME=VALUE), repeatable")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    parser.add_argument("--base-port", type=int, default=5305)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--stop", metavar="RUN_ID", help="ask a running run to save and stop")
    args = parser.parse_args()
    if not args.run_id and not args.stop:
        args.run_id = args.scene.split("_")[0] + "_" + datetime.datetime.now().strftime("%Y%m%d_%H%M")
    if not args.config:
        args.config = str(ROOT / "training" / CONFIGS[args.scene])
    if args.init_path and not pathlib.Path(args.init_path).is_file():
        sys.exit(f"--init-path {args.init_path}: no such file")

    results = pathlib.Path(args.results)
    if args.stop:
        (results / f"{args.stop}.stop").write_text("stop\n")
        print(f"{results / args.stop}.stop written: the launcher stops the run within 5 s")
        return

    run_dir = results / args.run_id
    env_dir = results / f"{args.run_id}_env"
    if run_dir.exists() and not args.resume:
        sys.exit(f"{run_dir} exists: pick another --run-id or pass --resume")
    if not env_dir.exists():
        freeze_env(env_dir)

    results.mkdir(parents=True, exist_ok=True)
    config = results / f"{args.run_id}.yaml"
    if not (args.resume and config.exists()):
        text = re.sub(r"max_steps: \d+", f"max_steps: {args.max_steps}", pathlib.Path(args.config).read_text())
        if args.init_path:
            # The behaviour's first setting: its indentation is the trainer_type line's
            init = pathlib.Path(args.init_path).resolve().as_posix()
            text = re.sub(r"^( +)trainer_type:", lambda m: f"{m.group(1)}init_path: {init}\n{m.group(1)}trainer_type:", text, count=1, flags=re.M)
        config.write_text(text)
    sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    (results / f"{args.run_id}.sha").write_text(sha + ("+dirty" if dirty else "") + "\n")

    cmd = [str(VENV / "mlagents-learn.exe"), str(config), f"--env={env_dir / 'TrainingEnv'}", f"--num-envs={args.num_envs}",
           "--no-graphics", f"--run-id={args.run_id}", f"--results-dir={results}", f"--base-port={args.base_port}",
           "--resume" if args.resume else "--seed=-1",
           "--env-args", f"scene={args.scene}", "mode=train", f"threads={args.threads}"] + [f"var.{v}" for v in args.var]
    wandb_dir = results / f"{args.run_id}_wandb"
    wandb_dir.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, WANDB_MODE="offline", WANDB_SILENT="true", WANDB_DIR=str(wandb_dir),
                       PYTHONPATH=str(ROOT / "tools" / "wandb_offline"))

    keep_awake()
    console = results / f"{args.run_id}.console.txt"
    with open(console, "a", encoding="utf-8") as out:
        out.write("+ " + " ".join(cmd) + "\n")
        out.flush()
        # A console of its own, without a window: CTRL_C can reach it (and its environments) and nothing else
        proc = subprocess.Popen(cmd, cwd=ROOT, env=environment, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        (results / f"{args.run_id}.pid").write_text(str(proc.pid))
        stop_file = results / f"{args.run_id}.stop"
        stop_file.unlink(missing_ok=True)
        stopping = False
        while proc.poll() is None:
            time.sleep(5)
            if not stopping and stop_file.exists():
                stopping = True
                out.write(f"stop requested at {datetime.datetime.now().isoformat(timespec='seconds')}: CTRL_C "
                          f"{'sent' if send_ctrl_c(proc.pid) else 'could not be sent'}\n")
                out.flush()
        code = proc.returncode
        stop_file.unlink(missing_ok=True)
        out.write(f"mlagents-learn exited with {code} at {datetime.datetime.now().isoformat(timespec='seconds')}\n")
    sys.exit(code)


if __name__ == "__main__":
    main()
