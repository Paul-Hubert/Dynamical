#!/usr/bin/env python3
"""Re-derives energyScale for the exact Jolt channel: the median of EnergyProvider/Cost of Transport over the last part
of a run, so exp(-CoT / scale) sits near 1/e for a typical policy (the rule the Unity configs were calibrated by).

    build/venv/Scripts/python tools/derive_energy_scale.py --run devita_cal
"""

import argparse
import glob
import pathlib
import statistics

from tensorboard.backend.event_processing.event_accumulator import EventAccumulator

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True)
    parser.add_argument("--behavior", default="AnySpeedBotBehavior")
    parser.add_argument("--results", default=str(ROOT / "build" / "runs"))
    args = parser.parse_args()
    values = []
    for f in glob.glob(str(pathlib.Path(args.results) / args.run / args.behavior / "events.out.tfevents.*")):
        acc = EventAccumulator(f, size_guidance={"scalars": 0})
        acc.Reload()
        for key in ("EnergyProvider/Cost of Transport",):
            if key in acc.Tags()["scalars"]:
                values.extend(e.value for e in acc.Scalars(key))
    if not values:
        raise SystemExit("no EnergyProvider/Cost of Transport in that run")
    tail = values[len(values) // 2:]
    print(f"{len(values)} samples; last-half median CoT = {statistics.median(tail):.1f} J/m -> energyScale {statistics.median(tail):.1f}")


if __name__ == "__main__":
    main()
