#!/usr/bin/env python3
"""P3 visual: a TensorBoard screenshot of a run (scalars present), plus the same scalars plotted from the event file.

    build/venv/Scripts/python tools/tensorboard_shot.py [--logdir build/artifacts/P3/smoke] [--out build/artifacts/P3/plots]

Starts TensorBoard on 127.0.0.1 only, screenshots it with headless Edge (or Chrome), stops it. The plot is drawn
from the TensorBoard event files with matplotlib, so it can be regenerated and diffed.
"""

import argparse
import pathlib
import shutil
import subprocess
import sys
import time
import urllib.request

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from tensorboard.backend.event_processing.event_accumulator import EventAccumulator

ROOT = pathlib.Path(__file__).resolve().parent.parent
BROWSERS = [r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe", r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Google\Chrome\Application\chrome.exe"]


def plot_scalars(logdir, out):
    events = sorted(pathlib.Path(logdir).rglob("events.out.tfevents.*"))
    if not events:
        sys.exit(f"no event file under {logdir}")
    acc = EventAccumulator(str(events[-1].parent))
    acc.Reload()
    tags = acc.Tags()["scalars"]
    cols = 3
    rows = (len(tags) + cols - 1) // cols
    fig, axes = plt.subplots(rows, cols, figsize=(4 * cols, 2.6 * rows), dpi=100, squeeze=False)
    for ax, tag in zip(axes.flat, tags):
        values = acc.Scalars(tag)
        ax.plot([v.step for v in values], [v.value for v in values], marker="o", ms=3)
        ax.set_title(tag, fontsize=8)
        ax.tick_params(labelsize=7)
        ax.grid(alpha=0.3)
    for ax in list(axes.flat)[len(tags):]:
        ax.axis("off")
    fig.suptitle(f"TensorBoard scalars of {events[-1].relative_to(ROOT).as_posix()}", fontsize=9)
    fig.tight_layout()
    fig.savefig(out / "smoke_scalars.png")
    print(f"{len(tags)} scalar tags: {', '.join(tags)}")


def screenshot(logdir, out):
    browser = next((b for b in BROWSERS if pathlib.Path(b).exists()), None)
    if not browser:
        print("no Edge or Chrome found: screenshot skipped")
        return
    tb = ROOT / "build" / "venv" / "Scripts" / "tensorboard.exe"
    port = 6117
    server = subprocess.Popen([str(tb), "--logdir", str(logdir), "--host", "127.0.0.1", "--port", str(port)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/data/runs", timeout=1)
                break
            except OSError:
                time.sleep(1)
        profile = ROOT / "build" / "artifacts" / "P3" / "browser_profile"
        shutil.rmtree(profile, ignore_errors=True)
        subprocess.run([browser, "--headless=new", "--disable-gpu", f"--user-data-dir={profile}", "--window-size=1500,1000",
                        "--virtual-time-budget=15000", f"--screenshot={out / 'tensorboard_smoke.png'}",
                        f"http://127.0.0.1:{port}/?darkMode=false#scalars"], timeout=120)
        shutil.rmtree(profile, ignore_errors=True)
        print(out / "tensorboard_smoke.png")
    finally:
        server.terminate()
        server.wait(timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--logdir", default=str(ROOT / "build" / "artifacts" / "P3" / "smoke"))
    parser.add_argument("--out", default=str(ROOT / "build" / "artifacts" / "P3" / "plots"))
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    plot_scalars(args.logdir, out)
    screenshot(args.logdir, out)


if __name__ == "__main__":
    main()
