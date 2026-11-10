#!/usr/bin/env python3
"""Runs the Unity exporter (Dynamical-noToes: Assets/Editor/JoltExport.cs) in batch mode into resources/agents.

    build/venv/Scripts/python tools/unity_export.py [--jobs params,body,tracks,limits,drives,rollouts]
"""

import argparse
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from train_walker import ROOT, UNITY_PROJECT, git, unity_exe  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", default="params,body,tracks,limits,drives,rollouts")
    parser.add_argument("--out", default=str(ROOT / "resources" / "agents"))
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    logfile = ROOT / "build" / "runs" / "unity_export.log"
    logfile.parent.mkdir(parents=True, exist_ok=True)
    sha = git(UNITY_PROJECT, "rev-parse", "--short", "HEAD")
    code = subprocess.run([unity_exe(), "-batchmode", "-projectPath", str(UNITY_PROJECT), "-logFile", str(logfile), "-executeMethod", "JoltExport.Run",
                           "--jolt-out", str(out), "--jolt-jobs", args.jobs, "--jolt-sha", sha]).returncode
    text = logfile.read_text(errors="replace")
    print("\n".join(l for l in text.splitlines() if "JOLTEXPORT|" in l or ": error CS" in l or "Exception" in l))
    sys.exit(code)


if __name__ == "__main__":
    main()
