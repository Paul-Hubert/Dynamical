#!/usr/bin/env python3
"""A round of the sandbox (docs/action-agents.md): the species' minds, and their walkers, trained together for a
number of steps with the reward files as they stand, then a summary of what the animals did.

    build/venv/Scripts/python tools/sandbox_round.py --name sandbox --round 1 --steps 2000000
    build/venv/Scripts/python tools/sandbox_round.py --name puppets --round 1 --steps 1000000 --puppet
    build/venv/Scripts/python tools/sandbox_round.py --name sandbox --round 2 --steps 2000000      (from round 1's checkpoints)
    build/venv/Scripts/python tools/sandbox_round.py --summary sandbox_r1
    build/venv/Scripts/python tools/sandbox_round.py --stop sandbox_r1

A round <name>_r<k>:
- freezes TrainingEnv.exe, its DLLs, resources/{scenes,prefabs,agents} (the life params and the reward files of the
  round with them) and the species' bodies into <results>/<name>_r<k>_env;
- writes <results>/<name>_r<k>.yaml from training/sandbox_ppo.yaml: a behaviour Action_<species> per species and,
  unless --puppet, a behaviour Animal_<species> with the demonstrations of the walker's own run (--walkers <prefix>:
  <results>/<prefix>_<species>_sandbox, as tools/train_animal.py --prefix made it);
- starts from round k-1 of the same name when there is one (mlagents-learn --initialize-from); in round 1 the walkers
  start from their own runs' checkpoints, and the minds from scratch or, with --minds-from, from another run's (a round
  of puppets);
- runs mlagents-learn on the scene sandbox_train (sandbox_puppet with --puppet) until every mind has taken --steps
  decisions (walkers: six times as many);
- writes <results>/<name>_r<k>.round.json: per species, the lives (lifetime, causes of death, births, kills, stores),
  the modes, the terms of the reward file, the walkers; over the last quarter of the round and over its first.
The next round's reward files are written from that summary (resources/agents/rewards/<species>.rewards.json).
--puppet is a check of the rules and of the minds with no body to learn: kinematic animals slid along their commands.
wandb stays offline. To outlive the terminal, start it with pythonw.exe.
"""

import argparse
import glob
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import train_walker as tw  # noqa: E402

ROOT = tw.ROOT
TEMPLATE = ROOT / "training" / "sandbox_ppo.yaml"
MIND_EVERY = 6


def species_of(life_params):
    """The species of a life params file, with the part of the largest one's animals each starts with."""
    life = json.loads(pathlib.Path(life_params).read_text(encoding="utf-8"))
    defaults = life.get("defaults", {})
    starts = {s: block.get("start", defaults.get("start", 12)) for s, block in life["species"].items()}
    most = max(max(starts.values()), 1)
    return {s: max(n / most, 0.05) for s, n in starts.items()}


def free_ports(count):
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


def load_scalars(run_dir, behavior):
    from tensorboard.backend.event_processing.event_accumulator import EventAccumulator
    data = {}
    for f in sorted(glob.glob(str(run_dir / behavior / "events.out.tfevents.*"))):
        acc = EventAccumulator(f, size_guidance={"scalars": 0})
        acc.Reload()
        for tag in acc.Tags()["scalars"]:
            for e in acc.Scalars(tag):
                data.setdefault(tag, {})[e.step] = e.value
    return data


def part_mean(series, lo, hi):
    """Mean of the values whose step is in the part [lo, hi] of the run."""
    if not series:
        return None
    steps = sorted(series)
    first, last = steps[0], steps[-1]
    span = max(last - first, 1)
    values = [series[s] for s in steps if lo <= (s - first) / span <= hi]
    return sum(values) / len(values) if values else None


def summarize(results, run_id):
    run_dir = results / run_id
    if not run_dir.exists():
        sys.exit(f"no run {run_dir}")
    out = {"run": run_id, "behaviors": {}, "species": {}}
    env = results / f"{run_id}_env" / "resources" / "agents"
    rewards = {}
    for f in sorted((env / "rewards").glob("*.rewards.json")) if (env / "rewards").exists() else []:
        rewards[f.name.replace(".rewards.json", "")] = json.loads(f.read_text(encoding="utf-8"))
    out["reward_files"] = rewards
    for behavior_dir in sorted(p for p in run_dir.iterdir() if p.is_dir() and glob.glob(str(p / "events.out.tfevents.*"))):
        behavior = behavior_dir.name
        data = load_scalars(run_dir, behavior)
        steps = max((max(s) for s in data.values() if s), default=0)
        entry = {"steps": steps}
        for tag, name in (("Environment/Cumulative Reward", "return"), ("Environment/Episode Length", "episode_length"),
                          ("Policy/Entropy", "entropy"), ("Policy/Extrinsic Reward", "extrinsic"), ("Policy/Gail Reward", "gail"),
                          ("Losses/Value Loss", "value_loss")):
            if tag in data:
                entry[name] = {"first_quarter": part_mean(data[tag], 0., 0.25), "last_quarter": part_mean(data[tag], 0.75, 1.)}
        out["behaviors"][behavior] = entry
        # The stats the sandbox sends: <Group>/<species>/<name>. Every behaviour's events hold all of them: taken
        # from the species' own mind, or from whichever behaviour has them
        for tag, series in data.items():
            parts = tag.split("/")
            if len(parts) != 3 or parts[0] not in ("Life", "Rewards", "Mode", "Walker"):
                continue
            group, species, name = parts
            own = behavior == f"Action_{species}"
            slot = out["species"].setdefault(species, {}).setdefault(group, {})
            if name in slot and not own:
                continue
            slot[name] = {"first_quarter": part_mean(series, 0., 0.25), "last_quarter": part_mean(series, 0.75, 1.), "points": len(series)}
        for tag in ("Sandbox/Grass standing", "Sandbox/Life scale"):
            if tag in data and tag not in out:
                out[tag] = {"first_quarter": part_mean(data[tag], 0., 0.25), "last_quarter": part_mean(data[tag], 0.75, 1.)}
    path = results / f"{run_id}.round.json"
    path.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    return out, path


def print_summary(summary):
    def fmt(v):
        return "     -" if v is None else f"{v:6.3f}" if abs(v) < 100 else f"{v:6.0f}"
    print(f"round {summary['run']}")
    for behavior, entry in summary["behaviors"].items():
        ret = entry.get("return", {})
        print(f"  {behavior:24s} {entry['steps']:>9d} steps, return {fmt(ret.get('first_quarter'))} -> {fmt(ret.get('last_quarter'))}"
              + (f", entropy {fmt(entry['entropy']['first_quarter'])} -> {fmt(entry['entropy']['last_quarter'])}" if "entropy" in entry else ""))
    for species, groups in summary["species"].items():
        print(f"  {species}")
        for group in ("Life", "Mode", "Rewards", "Walker"):
            for name, v in sorted(groups.get(group, {}).items()):
                print(f"    {group + '/' + name:28s} {fmt(v['first_quarter'])} -> {fmt(v['last_quarter'])}")
    for tag in ("Sandbox/Grass standing",):
        if tag in summary:
            print(f"  {tag:30s} {fmt(summary[tag]['first_quarter'])} -> {fmt(summary[tag]['last_quarter'])}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", default="sandbox", help="rounds are <name>_r<k>")
    parser.add_argument("--round", type=int, default=1)
    parser.add_argument("--steps", type=int, default=2_000_000, help="decisions of the minds of the largest species in this round (the others: in proportion to their animals)")
    parser.add_argument("--puppet", action="store_true", help="kinematic animals, minds only: a check, not the sandbox")
    parser.add_argument("--walkers", default="orders", help="prefix of the walkers' own runs (<prefix>_<species>_sandbox)")
    parser.add_argument("--no-walker-init", action="store_true", help="round 1: walkers from scratch")
    parser.add_argument("--minds-from", metavar="RUN_ID", help="round 1: the minds start from this run's checkpoints (a round of puppets: "
                        "what they learned of the rules without a body) instead of from scratch")
    parser.add_argument("--num-envs", type=int, default=4)
    parser.add_argument("--num-worlds", type=int, default=4, help="worlds per environment")
    parser.add_argument("--threads", type=int, default=2, help="workers per environment")
    parser.add_argument("--life-scale", type=float)
    parser.add_argument("--walker-share", type=float)
    parser.add_argument("--reward-scale", type=float)
    parser.add_argument("--life", default="agents/params/life.params.json", help="the life params, under resources/ (life_hunters.params.json: with men)")
    parser.add_argument("--var", action="append", default=[], metavar="NAME=VALUE", help="a scene variable, repeatable")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--summary", metavar="RUN_ID", help="only write and print the summary of a round")
    parser.add_argument("--stop", metavar="RUN_ID")
    args = parser.parse_args()

    results = pathlib.Path(args.results)
    if args.stop:
        (results / f"{args.stop}.stop").write_text("stop\n")
        print(f"{results / args.stop}.stop written: the launcher stops the run within 5 s")
        return
    if args.summary:
        summary, path = summarize(results, args.summary)
        print_summary(summary)
        print(f"written {path}")
        return

    import yaml
    run_id = f"{args.name}_r{args.round}"
    run_dir = results / run_id
    env_dir = results / f"{run_id}_env"
    if run_dir.exists() and not args.resume:
        sys.exit(f"{run_dir} exists: another --round or --name, or --resume")
    results.mkdir(parents=True, exist_ok=True)
    tw.keep_awake()

    with open(results / f"{run_id}.console.txt", "a", encoding="utf-8") as out:
        tw.freeze_engine(env_dir)
        frozen = env_dir / "resources"
        species = species_of(frozen / args.life)
        for s in species:
            body = ROOT / "resources" / "animals" / "bodies" / f"{s}.body.json"
            if not body.exists():
                sys.exit(f"no body {body}: tools/train_animal.py --species {s} --no-train makes it")
            target = frozen / "animals" / "bodies" / body.name
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(body, target)
        exe = env_dir / "TrainingEnv.exe"

        template = yaml.safe_load(TEMPLATE.read_text(encoding="utf-8"))
        config_path = results / f"{run_id}.yaml"
        previous = f"{args.name}_r{args.round - 1}"
        from_previous = args.round > 1 and (results / previous).exists()
        if args.round > 1 and not from_previous:
            sys.exit(f"no round {results / previous} to start from")
        if not (args.resume and config_path.exists()):
            behaviors = {}
            for s, part in species.items():
                steps = max(int(args.steps * part), 1000)
                action = json.loads(json.dumps(template["action"]))
                action["max_steps"] = steps
                action["summary_freq"] = max(min(action["summary_freq"], steps // 20), 1000)
                action["checkpoint_interval"] = min(action["checkpoint_interval"], steps)
                if args.minds_from and not from_previous:
                    checkpoint = results / args.minds_from / f"Action_{s}" / "checkpoint.pt"
                    if not checkpoint.exists():
                        sys.exit(f"no mind checkpoint {checkpoint}")
                    action["init_path"] = checkpoint.as_posix()
                behaviors[f"Action_{s}"] = action
                if args.puppet:
                    continue
                walker = json.loads(json.dumps(template["walker"]))
                walker["max_steps"] = steps * MIND_EVERY
                walker["summary_freq"] = max(min(walker["summary_freq"], steps * MIND_EVERY // 20), 1000)
                walker["checkpoint_interval"] = min(walker["checkpoint_interval"], steps * MIND_EVERY)
                own = results / f"{args.walkers}_{s}_sandbox"
                demos = results / f"{args.walkers}_{s}_sandbox_demos"
                if not demos.exists() or not list(demos.glob("*.demo")):
                    sys.exit(f"no demonstrations in {demos}: tools/train_animal.py --species {s} --ranges sandbox --scene animal_orders_train "
                             f"--demo-var orders=true --config training/animal_orders_ppo.yaml --prefix {args.walkers}")
                walker["reward_signals"]["gail"]["demo_path"] = demos.as_posix()
                if not from_previous and not args.no_walker_init:
                    checkpoint = own / f"Animal_{s}" / "checkpoint.pt"
                    if not checkpoint.exists():
                        sys.exit(f"no walker checkpoint {checkpoint} (--no-walker-init starts the walkers from scratch)")
                    walker["init_path"] = checkpoint.as_posix()
                behaviors[f"Animal_{s}"] = walker
            params = dict(template.get("environment_parameters", {}))
            if args.life_scale is not None:
                params["lifeScale"] = args.life_scale
            if args.walker_share is not None:
                params["walkerShare"] = args.walker_share
            if args.reward_scale is not None:
                params["rewardScale"] = args.reward_scale
            config = {"behaviors": behaviors, "environment_parameters": params, "env_settings": {"seed": 42 + args.round}}
            config_path.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")

        sha = tw.git(ROOT, "rev-parse", "--short", "HEAD") + ("+dirty" if tw.git(ROOT, "status", "--porcelain") else "")
        (results / f"{run_id}.sha").write_text(sha + "\n")
        scene = "sandbox_puppet" if args.puppet else "sandbox_train"
        env_args = [f"scene={scene}", f"var.num_worlds={args.num_worlds}", f"var.life={args.life}", *[f"var.{v}" for v in args.var], "mode=train", f"threads={args.threads}"]
        port = free_ports(args.num_envs)
        cmd = [str(tw.VENV / "mlagents-learn.exe"), str(config_path), f"--env={exe}", f"--num-envs={args.num_envs}", "--no-graphics",
               f"--run-id={run_id}", f"--results-dir={results}", f"--base-port={port}",
               *(["--resume"] if args.resume else []), *([f"--initialize-from={previous}"] if from_previous and not args.resume else []),
               "--env-args", *env_args]
        wandb_dir = results / f"{run_id}_wandb"
        wandb_dir.mkdir(exist_ok=True)
        environment = dict(os.environ, WANDB_MODE="offline", WANDB_SILENT="true", WANDB_DIR=str(wandb_dir),
                           PYTHONPATH=str(ROOT / "tools" / "wandb_offline"), PYTHONIOENCODING="utf-8")
        tw.log(out, "+ " + " ".join(cmd))
        started = time.time()
        # The frozen folder is the working directory: the environments read its resources, not the source tree's
        proc = subprocess.Popen(cmd, cwd=env_dir, env=environment, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        (results / f"{run_id}.pid").write_text(str(proc.pid))
        stop_file = results / f"{run_id}.stop"
        stop_file.unlink(missing_ok=True)
        stopping = False
        while proc.poll() is None:
            time.sleep(5)
            if not stopping and stop_file.exists():
                stopping = True
                sent = tw.send_ctrl_c(proc.pid)
                tw.log(out, f"stop requested: CTRL_C {'sent' if sent else 'could not be sent'}")
                if not sent:
                    # A launcher without a console (pythonw) cannot reach the trainer's: end its process tree
                    subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True)
        stop_file.unlink(missing_ok=True)
        tw.log(out, f"mlagents-learn exited with {proc.returncode} after {time.time() - started:.0f} s")
        try:
            summary, path = summarize(results, run_id)
            print_summary(summary)
            tw.log(out, f"summary {path}")
        except Exception as e:  # a round that died early has nothing to summarize
            tw.log(out, f"no summary: {e}")
        sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
