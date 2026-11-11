#!/usr/bin/env python3
"""Trains a physical animal of the library: body, demonstration, then the walker's trainer (the fork's mlagents-learn
on TrainingEnv.exe, PPO with the extrinsic reward and GAIL). docs/physical-agents/reports/animal-agents.md.

    build/venv/Scripts/python tools/train_animal.py --species deer_red --ranges walk_basic --max-steps 20000000
    build/venv/Scripts/python tools/train_animal.py --species all --ranges walk_basic --max-steps 20000000
    build/venv/Scripts/python tools/train_animal.py --stop animal_deer_red_walk_basic

Per species, in this order:
- the body (resources/animals/bodies/<id>.body.json) is generated again by animal_body.exe when it is missing or older
  than the library's files of the species, the overrides (resources/agents/animal_overrides.json) or the generator;
- TrainingEnv.exe, its DLLs, resources/{scenes,prefabs,agents} and the body are frozen into <results>/<run>_env;
- the demonstration (the procedural walk played on the body, scene animal_demo, mode=record-demo) is recorded with the
  frozen executable and cached under <results>/demo_cache by a key that covers the frozen executable, the body, the
  ranges, the params, every scene and prefab file, the species' library files, the gait and the number of decisions:
  a change of any of them records again. Its checks (limits, jumps, flips, ground, speed) are next to it (.checks.json);
- mlagents-learn runs training/animal_ppo.yaml on the scene animal_train (--terrain: animal_terrain_train, with the
  demonstration recorded on the terrain's routes by animal_demo_terrain).

--species all queues every animal that has a body, one run after the other, each as <prefix>_<species>_<ranges>;
a run that exists is skipped unless --resume. --only-demo stops after the demonstration, --no-train after the freeze.
The run lives in <results> (default build/runs) like the walker's: <run>/, <run>.yaml, <run>_demos/, <run>.console.txt.
wandb stays offline. To outlive the terminal, start it with pythonw.exe.
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import train_walker as tw  # noqa: E402

ROOT = tw.ROOT
BUILD = tw.BUILD
LIBRARY = ROOT / "resources" / "animals"
OVERRIDES = ROOT / "resources" / "agents" / "animal_overrides.json"


def free_ports(count):
    """A base port whose `count` ports (one per environment) are all free: with several trainers on the machine the
    walker's check of the first one alone lets a run die on its fourth environment."""
    import socket
    for _ in range(50):
        base = tw.free_base_port()
        try:
            for port in range(base, base + count):
                with socket.socket() as s:
                    s.bind(("localhost", port))
            return base
        except OSError:
            continue
    sys.exit("no free port window")


def species_files(species):
    return [LIBRARY / "species" / f"{species}{ext}" for ext in (".json", ".bin", ".anim")]


def body_file(species):
    return LIBRARY / "bodies" / f"{species}.body.json"


def ensure_body(species, out):
    """The body file, generated again when stale. False when the generator makes none (the reason is logged)."""
    body = body_file(species)
    generator = BUILD / "animal_body.exe"
    sources = [f for f in species_files(species) if f.exists()] + [OVERRIDES] + ([generator] if generator.exists() else [])
    stale = not body.exists() or any(f.stat().st_mtime > body.stat().st_mtime for f in sources)
    if stale:
        if not generator.exists():
            sys.exit(f"{generator} is missing: cmake -DDYNAMICAL_BUILD_TOOLS=ON build, then build the target animal_body")
        tw.log(out, f"generating the body of {species}")
        summary = body.parent / f"{species}.summary.json"
        result = subprocess.run([str(generator), "--species", species, "--overrides", str(OVERRIDES), "--summary", str(summary)],
                                cwd=ROOT, capture_output=True, text=True)
        out.write(result.stdout + result.stderr)
        summary.unlink(missing_ok=True)
        if result.returncode or not body.exists():
            tw.log(out, f"{species}: no body ({result.stdout.strip().splitlines()[0] if result.stdout.strip() else result.stderr.strip()})")
            return False
    return True


def freeze(env_dir, species):
    tw.freeze_engine(env_dir)
    frozen = env_dir / "resources" / "animals" / "bodies" / f"{species}.body.json"
    if not frozen.exists():
        frozen.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(body_file(species), frozen)
    return frozen


def demo_key(env_dir, exe, body, species, args):
    key = hashlib.sha256()
    frozen = env_dir / "resources"
    files = [exe, body, frozen / "agents" / "ranges" / f"{args.ranges}.json", frozen / "agents" / "params" / "animal.params.json"]
    files += sorted(p for sub in ("scenes", "prefabs") for p in (frozen / sub).rglob("*") if p.is_file())
    for f in files:
        key.update(f.relative_to(env_dir).as_posix().encode())
        key.update(tw.sha256(f).encode())
    for f in species_files(species):
        key.update(f.name.encode())
        key.update(tw.sha256(f).encode())
    for text in ("animal", species, args.demo_scene, args.gait, str(args.demo_decisions), *sorted(args.demo_var)):
        key.update(text.encode() + b"\0")
    return key.hexdigest()[:24]


def record_demo(env_dir, exe, body, species, args, results, out):
    cached = results / "demo_cache" / f"{species}_{args.ranges}_{demo_key(env_dir, exe, body, species, args)}.demo"
    checks = cached.with_suffix(".checks.json")
    if not cached.exists() or cached.stat().st_size == 0:
        cached.parent.mkdir(parents=True, exist_ok=True)
        partial = cached.with_suffix(".partial")
        frozen = env_dir / "resources" / "agents"
        demo_args = [f"scene={args.demo_scene}", "mode=record-demo", f"var.body={body}", f"var.ranges={frozen / 'ranges' / (args.ranges + '.json')}",
                     f"var.params={frozen / 'params' / 'animal.params.json'}",
                     f"var.gait={args.gait}", f"var.decisions={args.demo_decisions}", *[f"var.{v}" for v in args.demo_var]]
        tw.log(out, f"recording {cached.name}: {' '.join(demo_args)}")
        subprocess.run([str(exe), *demo_args, f"var.out={partial}", f"var.checks={checks}"], check=True, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
        partial.replace(cached)
        cached.with_suffix(".txt").write_text(" ".join(demo_args) + "\n")
    else:
        tw.log(out, f"demo {cached.name} from the cache")
    if checks.exists():
        c = json.loads(checks.read_text())
        tw.log(out, f"demo checks: limits {c['limit_excess_deg']:.2f} deg over on {c['limit_ticks']} ticks, {c['falls']} ticks the fall test would end, "
                    f"{c['heading_flips']} heading flips, axis jump {c['axis_jump']:.2f}, lowest point {c['lowest']:.3f} m, speed {c['speed_ratio']:.3f} of the command")
    return cached


def train(species, args, results):
    run_id = args.run_id if args.run_id and args.species != "all" else f"{args.prefix}_{species}_{args.ranges}"
    run_dir = results / run_id
    if run_dir.exists() and not args.resume:
        if args.species == "all":
            print(f"{run_id} exists: skipped")
            return 0
        sys.exit(f"{run_dir} exists: pick another --run-id or pass --resume")
    with open(results / f"{run_id}.console.txt", "a", encoding="utf-8") as out:
        if not ensure_body(species, out):
            return 1
        # A queue of every animal shares one frozen engine (the bodies are added to it one by one)
        env_dir = results / (f"{args.prefix}_{args.ranges}_env" if args.species == "all" else f"{run_id}_env")
        body = freeze(env_dir, species)
        exe = env_dir / "TrainingEnv.exe"
        if args.no_train:
            return 0
        demo = record_demo(env_dir, exe, body, species, args, results, out)
        if args.only_demo:
            return 0
        demos = results / f"{run_id}_demos"
        demos.mkdir(exist_ok=True)
        shutil.copy2(demo, demos / demo.name)
        if demo.with_suffix(".checks.json").exists():
            shutil.copy2(demo.with_suffix(".checks.json"), results / f"{run_id}.demo_checks.json")

        sha = tw.git(ROOT, "rev-parse", "--short", "HEAD") + ("+dirty" if tw.git(ROOT, "status", "--porcelain") else "")
        (results / f"{run_id}.sha").write_text(sha + "\n")
        config = results / f"{run_id}.yaml"
        if not (args.resume and config.exists()):
            text = pathlib.Path(args.config).read_text()
            if args.max_steps:
                text = re.sub(r"max_steps: \S+", f"max_steps: {args.max_steps}", text)
                # A short run still writes summaries and a checkpoint
                text = re.sub(r"summary_freq: \S+", f"summary_freq: {max(min(100000, args.max_steps // 10), 1000)}", text)
                text = re.sub(r"checkpoint_interval: \S+", f"checkpoint_interval: {min(5000000, args.max_steps)}", text)
            if args.seed is not None:
                text = re.sub(r"seed: \S+", f"seed: {args.seed}", text)
            config.write_text(re.sub(r"demo_path: \S+", f"demo_path: {demos.as_posix()}", text))

        scene = "animal_terrain_train" if args.terrain else "animal_train"
        frozen = env_dir / "resources" / "agents"
        env_args = [f"scene={scene}", f"var.body={body}", f"var.ranges={frozen / 'ranges' / (args.ranges + '.json')}",
                    f"var.params={frozen / 'params' / 'animal.params.json'}", f"var.num_worlds={args.num_worlds}",
                    *[f"var.{v}" for v in args.env_var], "mode=train", f"threads={args.threads}",
                    f"-envParamsOut={results / run_id / 'envparams.txt'}", f"-gitSha={sha}"]
        port = free_ports(args.num_envs)
        cmd = [str(tw.VENV / "mlagents-learn.exe"), str(config), f"--env={exe}", f"--num-envs={args.num_envs}", "--no-graphics",
               f"--run-id={run_id}", f"--results-dir={results}", f"--base-port={port}", *(["--resume"] if args.resume else []),
               "--env-args", *env_args]
        wandb_dir = results / f"{run_id}_wandb"
        wandb_dir.mkdir(exist_ok=True)
        environment = dict(os.environ, WANDB_MODE="offline", WANDB_SILENT="true", WANDB_DIR=str(wandb_dir),
                           PYTHONPATH=str(ROOT / "tools" / "wandb_offline"), PYTHONIOENCODING="utf-8")
        tw.log(out, "+ " + " ".join(cmd))
        started = time.time()
        proc = subprocess.Popen(cmd, cwd=ROOT, env=environment, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        (results / f"{run_id}.pid").write_text(str(proc.pid))
        stop_file = results / f"{run_id}.stop"
        stop_file.unlink(missing_ok=True)
        stopping = False
        while proc.poll() is None:
            time.sleep(5)
            if not stopping and stop_file.exists():
                stopping = True
                tw.log(out, f"stop requested: CTRL_C {'sent' if tw.send_ctrl_c(proc.pid) else 'could not be sent'}")
        stop_file.unlink(missing_ok=True)
        tw.log(out, f"mlagents-learn exited with {proc.returncode} after {time.time() - started:.0f} s")
        return proc.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--species", help="an id of the library, or all")
    parser.add_argument("--ranges", default="walk_basic", help="resources/agents/ranges/<name>.json: the commands, and the styles of the demonstrations")
    parser.add_argument("--max-steps", type=int, help="replaces the config's max_steps")
    parser.add_argument("--run-id", help="default: <prefix>_<species>_<ranges>")
    parser.add_argument("--prefix", default="animal")
    parser.add_argument("--config", default=str(ROOT / "training" / "animal_ppo.yaml"))
    parser.add_argument("--num-envs", type=int, default=8)
    parser.add_argument("--num-worlds", type=int, default=25, help="worlds per environment")
    parser.add_argument("--threads", type=int, default=2, help="workers per environment")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--terrain", action="store_true", help="train on the marching cubes terrain (animal_terrain_train)")
    parser.add_argument("--gait", default="walk", choices=["walk", "trot", "run"], help="the clip the demonstrations are made from")
    parser.add_argument("--demo-scene", help="default: animal_demo, animal_demo_terrain with --terrain")
    parser.add_argument("--demo-decisions", type=int, default=9000)
    parser.add_argument("--demo-var", action="append", default=[], metavar="NAME=VALUE", help="a variable of the demo scene, repeatable")
    parser.add_argument("--env-var", action="append", default=[], metavar="NAME=VALUE", help="a variable of the training scene, repeatable")
    parser.add_argument("--only-demo", action="store_true", help="body, freeze and demonstration, no training")
    parser.add_argument("--no-train", action="store_true", help="body and freeze only")
    parser.add_argument("--stop", metavar="RUN_ID")
    args = parser.parse_args()

    if not args.demo_scene:
        args.demo_scene = "animal_demo_terrain" if args.terrain else "animal_demo"
    results = pathlib.Path(args.results)
    if args.stop:
        (results / f"{args.stop}.stop").write_text("stop\n")
        print(f"{results / args.stop}.stop written: the launcher stops the run within 5 s")
        return
    if not args.species:
        parser.error("needs --species")
    if not (LIBRARY / "library.json").exists():
        sys.exit(f"no animal library in {LIBRARY} (tools/animal_import.py builds it)")
    results.mkdir(parents=True, exist_ok=True)
    tw.keep_awake()

    if args.species == "all":
        library = json.loads((LIBRARY / "library.json").read_text())
        ids = [entry["id"] for entry in library["species"] if entry.get("walk")]
    else:
        ids = [args.species]
    worst = 0
    for species in ids:
        worst = max(worst, train(species, args, results) or 0)
    sys.exit(worst)


if __name__ == "__main__":
    main()
