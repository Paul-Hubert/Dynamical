#!/usr/bin/env python3
"""Walker training runs through the fork's mlagents-learn, on the Unity build or on TrainingEnv.exe. Replaces start-run.ps1.

    build/venv/Scripts/python tools/setup_venv.py        # once: creates build/venv with the trainer
    build/venv/Scripts/python tools/train_walker.py unity     --run-id R --config training/unity_reference_walk136.yaml
    build/venv/Scripts/python tools/train_walker.py dynamical --run-id R --config training/walker.yaml
    build/venv/Scripts/python tools/train_walker.py --stop R

unity:      records the demo in the Editor, builds the player, trains. --scene is relative to Assets.
dynamical:  freezes TrainingEnv.exe and resources into <results>/<run>_env, records the demo with mode=record-demo
            (cached under <results>/demo_cache), trains.
            --demo-source track (default): the scene walker_demo plays a pose track; the cache key is the track, the
            body, the layout and the number of decisions.
            --demo-source gait: a scene of the kinematic walk (walker_demo_gait, walker_demo_terrain) records its
            routes; the cache key is the frozen TrainingEnv.exe (the gait's code), every scene and prefab file (routes,
            GaitParams), the body, the params, the layout, the scene's name, --demo-var and the number of decisions.
            --env-var NAME=VALUE sets a variable of the training scene.

Common to both:
- the run lives in <results> (default build/runs): <run>/ (the trainer's), <run>.yaml, <run>_demos/, <run>.console.txt;
- wandb stays offline; the machine stays awake for the length of the run;
- --stop drops <run>.stop, and the launcher sends CTRL_C: the trainer saves and exports, the environments exit;
- to outlive the terminal, start it with pythonw.exe.
"""

import argparse
import ctypes
import datetime
import hashlib
import os
import pathlib
import random
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent


def venv_scripts():
    """build/venv of this checkout, else of the main checkout (a git worktree has no venv of its own)."""
    local = ROOT / "build" / "venv" / "Scripts"
    if local.exists():
        return local
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return pathlib.Path(common).parent / "build" / "venv" / "Scripts" if common else local


VENV = venv_scripts()
BUILD = ROOT / "build" / "RelWithDebInfo"
UNITY_PROJECT = pathlib.Path("C:/Users/Paul/Desktop/Dynamical-noToes")
STRIDE = 65


def log(out, text):
    line = f"[{datetime.datetime.now().strftime('%H:%M:%S')}] {text}"
    print(line, flush=True)
    out.write(line + "\n")
    out.flush()


def keep_awake():
    if sys.platform == "win32":
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000001)


def send_ctrl_c(pid):
    k = ctypes.windll.kernel32
    k.FreeConsole()
    if not k.AttachConsole(pid):
        return False
    k.SetConsoleCtrlHandler(None, True)
    return bool(k.GenerateConsoleCtrlEvent(0, 0))


def excluded_ports():
    text = subprocess.run(["netsh", "int", "ipv4", "show", "excludedportrange", "protocol=tcp"], capture_output=True, text=True).stdout
    return [(int(a), int(b)) for a, b in re.findall(r"^\s*(\d+)\s+(\d+)", text, flags=re.M)]


def free_base_port():
    """A window of STRIDE ports that Windows has not reserved and nothing listens on."""
    excluded = excluded_ports()
    for _ in range(300):
        port = random.randint(54600, 64400)
        if any(port <= hi and port + STRIDE - 1 >= lo for lo, hi in excluded):
            continue
        try:
            with socket.socket() as s:
                s.bind(("localhost", port))
            return port
        except OSError:
            continue
    sys.exit("no free port window in 54600-64400")


def unity_exe():
    script = UNITY_PROJECT / "Scripts" / "Get-UnityExe.ps1"
    result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)], capture_output=True, text=True)
    path = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else ""
    if result.returncode or not pathlib.Path(path).is_file():
        sys.exit(f"no Unity Editor: {result.stderr}")
    return path


def run_unity(exe, args, logfile, out):
    log(out, f"unity {' '.join(args)}")
    # Waits on the process, not its tree: a batchmode Unity can leave its licensing client behind
    code = subprocess.run([exe, "-batchmode", "-projectPath", str(UNITY_PROJECT), "-logFile", str(logfile), *args]).returncode
    if code:
        sys.exit(f"Unity exited with {code}: see {logfile}")


def prepare_unity(args, results, out):
    exe = unity_exe()
    demos = UNITY_PROJECT / "Demonstrations"
    if demos.exists() and any(demos.iterdir()):
        keep = results / "displaced_unity_demos" / datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        keep.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(demos), str(keep))
        log(out, f"previous demos kept in {keep}")
    demos.mkdir(exist_ok=True)
    run_unity(exe, ["-executeMethod", "AutoDemonstrationRecorder.Record", "--scene", args.scene], results / f"{args.run_id}.demo.log", out)
    found = list(demos.glob("*.demo"))
    if not found:
        sys.exit("the Unity recording produced no .demo")
    target = results / f"{args.run_id}_demos"
    target.mkdir(exist_ok=True)
    for f in found:
        shutil.copy2(f, target / f.name)

    build_dir = UNITY_PROJECT / "Builds" / "Runs" / pathlib.PurePosixPath(args.scene).stem
    build_dir.mkdir(parents=True, exist_ok=True)
    env = build_dir / "TrainingEnv.exe"
    run_unity(exe, ["-quit", "-buildTarget", "Standalone", "-executeMethod", "Builder.Build", "--scene", args.scene, "--outputPath", str(env)],
              results / f"{args.run_id}.build.log", out)
    if not env.is_file():
        sys.exit(f"the Unity build produced no {env}")
    sha = git(UNITY_PROJECT, "rev-parse", "--short", "HEAD") + ("+dirty" if git(UNITY_PROJECT, "status", "--porcelain") else "")
    return env, target, [f"-envParamsOut={results / args.run_id / 'envparams.txt'}", f"-gitSha={sha}"], sha


def git(repo, *args):
    return subprocess.run(["git", *args], cwd=repo, capture_output=True, text=True).stdout.strip()


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def freeze_engine(dst):
    if dst.exists():
        return
    dst.mkdir(parents=True)
    for f in BUILD.iterdir():
        if f.name == "TrainingEnv.exe" or f.suffix.lower() == ".dll":
            shutil.copy2(f, dst / f.name)
    for sub in ("scenes", "prefabs", "agents"):
        if (ROOT / "resources" / sub).exists():
            shutil.copytree(ROOT / "resources" / sub, dst / "resources" / sub)
    # The marching cubes table: read from the working directory, ROOT for every process started here; kept with the run
    if (ROOT / "resources" / "mclookup.bin").exists():
        shutil.copy2(ROOT / "resources" / "mclookup.bin", dst / "resources" / "mclookup.bin")


def prepare_engine(args, results, out):
    env_dir = results / f"{args.run_id}_env"
    freeze_engine(env_dir)
    exe = env_dir / "TrainingEnv.exe"
    key = hashlib.sha256()
    body = ROOT / "resources" / "agents" / "bodies" / f"{args.body}.body.json"
    demo_args = [f"scene={args.demo_scene}", "mode=record-demo", f"var.body=agents/bodies/{args.body}.body.json",
                 f"var.params=agents/params/{args.params}.params.json", f"var.decisions={args.demo_decisions}"]
    if args.demo_source == "track":
        track = ROOT / "resources" / "agents" / "tracks" / f"{args.track}.track.json"
        for f in (track, track.with_suffix("").with_suffix(".track.bin"), body):
            key.update(sha256(f).encode())
        key.update(args.layout.encode())
        key.update(str(args.demo_decisions).encode())
        demo_args.append(f"var.track=agents/tracks/{args.track}.track.json")
    else:
        # What a gait demo is made of: the code of the walk, the scenes (world, routes, GaitParams), the body and the
        # params, as frozen for this run
        frozen = env_dir / "resources"
        files = [exe, frozen / "agents" / "bodies" / f"{args.body}.body.json", frozen / "agents" / "params" / f"{args.params}.params.json"]
        files += sorted(p for sub in ("scenes", "prefabs") for p in (frozen / sub).rglob("*") if p.is_file())
        for f in files:
            key.update(f.relative_to(env_dir).as_posix().encode())
            key.update(sha256(f).encode())
        for text in ("gait", args.layout, args.demo_scene, str(args.demo_decisions), *sorted(args.demo_var)):
            key.update(text.encode() + b"\0")
        demo_args += [f"var.{v}" for v in args.demo_var]
    cached = results / "demo_cache" / f"{key.hexdigest()[:24]}.demo"
    if not cached.exists() or cached.stat().st_size == 0:
        cached.parent.mkdir(parents=True, exist_ok=True)
        log(out, f"recording {cached.name}: {' '.join(demo_args)}")
        partial = cached.with_suffix(".partial")
        subprocess.run([str(exe), *demo_args, f"var.out={partial}"], check=True, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
        partial.replace(cached)
        cached.with_suffix(".txt").write_text(" ".join(demo_args) + "\n")
    else:
        log(out, f"demo {cached.name} from the cache")
    target = results / f"{args.run_id}_demos"
    target.mkdir(exist_ok=True)
    shutil.copy2(cached, target / cached.name)
    sha = git(ROOT, "rev-parse", "--short", "HEAD") + ("+dirty" if git(ROOT, "status", "--porcelain") else "")
    return exe, target, [f"scene={args.scene}", *[f"var.{v}" for v in args.env_var], "mode=train", f"threads={args.threads}", f"-envParamsOut={results / args.run_id / 'envparams.txt'}", f"-gitSha={sha}"], sha


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("engine", nargs="?", choices=["unity", "dynamical"])
    parser.add_argument("--run-id")
    parser.add_argument("--config")
    parser.add_argument("--scene", help="unity: scene under Assets; dynamical: scene name")
    parser.add_argument("--num-envs", type=int, default=20)
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--stop", metavar="RUN_ID")
    parser.add_argument("--threads", type=int, default=1, help="dynamical: workers per process")
    parser.add_argument("--track", default="heidelbot_notoes__BasicMotions@Walk")
    parser.add_argument("--body", default="heidelbot_notoes")
    parser.add_argument("--params", default="walker", help="dynamical: resources/agents/params/<name>.params.json (for the demo)")
    parser.add_argument("--layout", default="heidelbot_notoes_2")
    parser.add_argument("--demo-source", choices=["track", "gait"], default="track", help="dynamical: a pose track, or the kinematic walk")
    parser.add_argument("--demo-scene", default=None, help="dynamical: walker_demo (track), walker_demo_gait or walker_demo_terrain (gait)")
    parser.add_argument("--demo-var", action="append", default=[], metavar="NAME=VALUE", help="dynamical: a variable of the demo scene, repeatable")
    parser.add_argument("--env-var", action="append", default=[], metavar="NAME=VALUE", help="dynamical: a variable of the training scene, repeatable")
    parser.add_argument("--demo-decisions", type=int, default=3000)
    args = parser.parse_args()

    results = pathlib.Path(args.results)
    if args.stop:
        (results / f"{args.stop}.stop").write_text("stop\n")
        print(f"{results / args.stop}.stop written: the launcher stops the run within 5 s")
        return
    if not (args.engine and args.run_id and args.config):
        parser.error("needs an engine, --run-id and --config")
    if args.scene is None:
        args.scene = "AI/HeidelBot/Scenes/Training.unity" if args.engine == "unity" else "walker_train"
    if args.demo_scene is None:
        args.demo_scene = "walker_demo" if args.demo_source == "track" else "walker_demo_gait"
    if args.demo_source == "track" and args.demo_var:
        parser.error("--demo-var needs --demo-source gait")

    run_dir = results / args.run_id
    if run_dir.exists() and not args.resume:
        sys.exit(f"{run_dir} exists: pick another --run-id or pass --resume")
    results.mkdir(parents=True, exist_ok=True)
    keep_awake()

    with open(results / f"{args.run_id}.console.txt", "a", encoding="utf-8") as out:
        env, demos, env_args, sha = prepare_unity(args, results, out) if args.engine == "unity" else prepare_engine(args, results, out)
        (results / f"{args.run_id}.sha").write_text(sha + "\n")

        config = results / f"{args.run_id}.yaml"
        if not (args.resume and config.exists()):
            text = pathlib.Path(args.config).read_text()
            config.write_text(re.sub(r"demo_path: \S+", f"demo_path: {demos.as_posix()}", text))

        port = free_base_port()
        cmd = [str(VENV / "mlagents-learn.exe"), str(config), f"--env={env}", f"--num-envs={args.num_envs}", "--no-graphics",
               f"--run-id={args.run_id}", f"--results-dir={results}", f"--base-port={port}", *(["--resume"] if args.resume else []),
               "--env-args", *env_args]
        wandb_dir = results / f"{args.run_id}_wandb"
        wandb_dir.mkdir(exist_ok=True)
        environment = dict(os.environ, WANDB_MODE="offline", WANDB_SILENT="true", WANDB_DIR=str(wandb_dir),
                           PYTHONPATH=str(ROOT / "tools" / "wandb_offline"), PYTHONIOENCODING="utf-8")
        log(out, "+ " + " ".join(cmd))
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
                log(out, f"stop requested: CTRL_C {'sent' if send_ctrl_c(proc.pid) else 'could not be sent'}")
        stop_file.unlink(missing_ok=True)
        log(out, f"mlagents-learn exited with {proc.returncode}")
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
