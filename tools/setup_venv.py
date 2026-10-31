#!/usr/bin/env python3
"""Creates build/venv with the fork's ML-Agents trainer (side-channel captures, train_smoke.py, plots).

    <python 3.10.1-3.10.12> tools/setup_venv.py [--fork C:/Users/Paul/Desktop/Dynamical-noToes/ml-agents]

The fork's ml-agents requires Python 3.10.1-3.10.12. Its packages are installed from a copy of the fork's
committed tree (git archive) so nothing is ever written into the fork. Versions are pinned to the ones of the
training environment the Unity runs use (wandb 0.18.5 pulls a protobuf newer than the trainer accepts, and
setuptools 81+ drops the pkg_resources the trainer imports).
"""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tarfile
import io

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENV = ROOT / "build" / "venv"
PINS = ["setuptools==80.10.2", "wandb==0.18.5", "protobuf==3.20.3", "grpcio==1.53.2", "onnx==1.17.0",
        "numpy>=1.23.5,<1.24", "matplotlib<3.9"]


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fork", default="C:/Users/Paul/Desktop/Dynamical-noToes/ml-agents")
    args = parser.parse_args()

    if not ((3, 10, 1) <= sys.version_info[:3] <= (3, 10, 12)):
        sys.exit(f"needs Python 3.10.1-3.10.12, this is {sys.version.split()[0]}")

    if not VENV.exists():
        run([sys.executable, "-m", "venv", VENV])
    python = VENV / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")

    src = ROOT / "build" / "mlagents_src"
    shutil.rmtree(src, ignore_errors=True)
    src.mkdir(parents=True)
    archive = subprocess.run(["git", "-C", args.fork, "archive", "HEAD", "ml-agents-envs", "ml-agents"], check=True, capture_output=True).stdout
    tarfile.open(fileobj=io.BytesIO(archive)).extractall(src)

    run([python, "-m", "pip", "install", "--upgrade", "pip"])
    run([python, "-m", "pip", "install", src / "ml-agents-envs", src / "ml-agents"])
    run([python, "-m", "pip", "install", *PINS])
    run([python, VENV / "Scripts" / "mlagents-learn.exe" if sys.platform == "win32" else VENV / "bin" / "mlagents-learn", "--help"])


if __name__ == "__main__":
    main()
