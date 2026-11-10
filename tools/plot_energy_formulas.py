"""The two energy-formula graphs of a long rollout, against DeVita et al. 2007 on flat ground.

    build/venv/Scripts/python.exe tools/plot_energy_formulas.py build/runs/eval/devita_walk150_20m.formulas.json \
        --out docs/physical-agents/reports/figures

The input is the summary of `mode=eval` with `var.out`; see docs/physical-agents/energy-formulas.md.
"""
import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

DEVITA_POS, DEVITA_NEG = 50.5, 34.3  # J per step, level walking at 1.50 m/s, J Exp Biol 210:3361 (2007)

SURFACE, INK, INK_2, MUTED, GRID, BASELINE = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
POSITIVE, NEGATIVE = "#2a78d6", "#eb6834"  # categorical slots 1 and 2


def chart(groups, title, subtitle, note, path):
    """groups: (label, pos, neg, gross_label, is_reference), J per step."""
    plt.rcParams.update({"font.family": ["Segoe UI", "DejaVu Sans"], "font.size": 11})
    fig, ax = plt.subplots(figsize=(9.2, 5.6), dpi=200)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    width = 0.42
    top = max(g[1] for g in groups)
    bottom = max(g[2] for g in groups)
    for x, (label, pos, neg, summary, reference) in enumerate(groups):
        hatch = "////" if reference else None
        ax.bar(x, pos, width, color=POSITIVE, hatch=hatch, edgecolor=SURFACE, linewidth=1.2, zorder=3)
        ax.bar(x, -neg, width, color=NEGATIVE, hatch=hatch, edgecolor=SURFACE, linewidth=1.2, zorder=3)
        ax.text(x, pos + top * 0.025, f"+{pos:.1f}", ha="center", va="bottom", color=INK, fontsize=11, zorder=4)
        ax.text(x, -neg - top * 0.025, f"−{neg:.1f}", ha="center", va="top", color=INK, fontsize=11, zorder=4)
        ax.text(x, -bottom * 1.34, summary, ha="center", va="top", color=INK_2, fontsize=10, linespacing=1.3)

    ax.axhline(0, color=BASELINE, linewidth=1.2, zorder=2)
    ax.set_xticks(range(len(groups)), [g[0] for g in groups], color=INK, fontsize=11)
    ax.tick_params(axis="x", length=0, pad=8)
    ax.xaxis.set_ticks_position("top")
    ax.spines[:].set_visible(False)
    ax.tick_params(axis="y", length=0, colors=MUTED, labelsize=10)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8, zorder=1)
    ax.set_axisbelow(True)
    ax.set_ylabel("Work per step (J)", color=INK_2, fontsize=11)
    ax.set_ylim(-bottom * 1.28, top * 1.16)
    ax.set_xlim(-0.55, len(groups) - 0.45)

    fig.text(0.015, 0.975, title, ha="left", va="top", fontsize=14.5, fontweight="semibold", color=INK)
    fig.text(0.015, 0.925, subtitle, ha="left", va="top", fontsize=10.5, color=INK_2)
    handles = [Patch(color=POSITIVE, label="Positive work"), Patch(color=NEGATIVE, label="Negative work"),
               Patch(facecolor=SURFACE, edgecolor=MUTED, hatch="////", label="Measured in humans")]
    fig.legend(handles=handles, loc="upper left", bbox_to_anchor=(0.006, 0.895), ncols=3, frameon=False,
               fontsize=10, labelcolor=INK_2, handlelength=1.1, columnspacing=1.4)
    fig.text(0.015, 0.02, note, ha="left", va="bottom", fontsize=9.3, color=MUTED, linespacing=1.4)
    fig.subplots_adjust(left=0.085, right=0.985, top=0.745, bottom=0.235)
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--out", type=Path, default=Path("docs/physical-agents/reports/figures"))
    parser.add_argument("--name", help="policy name, default the summary's file name without .formulas.json")
    args = parser.parse_args()
    name = args.name or args.summary.name.split(".")[0]
    data = json.loads(args.summary.read_text())
    f = data["energy_formulas"]
    n = f["stances"]
    full, stance = f["full_body"], f["devita_stance"]
    per = lambda w: (w["pos"] / n, w["neg"] / n)
    ratio = lambda p, q: f"neg/pos {q / p:.2f}"
    gross = lambda p, q: f"{p + q:.1f} J gross"

    lag = per(full["internal_L"])
    lag_abs = full["internal_abs_delta_L"] / n
    rne, drive = per(full["rne"]), per(full["drive"])
    rne_s, drive_s = per(stance["rne_sagittal"]), per(stance["drive_sagittal"])
    step = f["metres"] / n
    context = (f"{data['episodes']} episodes of 33 s, {f['metres'] / 1000:.1f} km, {n:,} stances, policy {name}. "
               f"Mean speed {f['metres'] / f['seconds']:.2f} m/s, {n / f['seconds']:.2f} steps/s, {step:.2f} m per step.")
    args.out.mkdir(parents=True, exist_ok=True)

    chart([
        ("Internal work\nΣ|Δ(T − V)|", lag[0], lag[1], f"{lag_abs:.1f} J total\n(split by sign of ΔL)", False),
        ("Newton–Euler\n3D, all joints", rne[0], rne[1], gross(*rne) + "\n" + ratio(*rne), False),
        ("Drive torque\n3D, all joints", drive[0], drive[1], gross(*drive) + "\n" + ratio(*drive), False),
        ("DeVita 2007\nsagittal, leg, stance", DEVITA_POS, DEVITA_NEG, gross(DEVITA_POS, DEVITA_NEG) + "\n" + ratio(DEVITA_POS, DEVITA_NEG), True),
    ], f"Full body: internal work is {lag_abs / (rne[0] + rne[1]):.1f}× the joint work",
        "Work per step over the whole gait cycle, all 15 joints, walking on flat ground",
        context + "\nDeVita covers only one leg's hip, knee and ankle in stance, so it is a narrower measure than the simulated bars.",
        args.out / f"{name}_energy_full_body.png")

    chart([
        ("Newton–Euler\nsagittal, leg, stance", rne_s[0], rne_s[1], gross(*rne_s) + "\n" + ratio(*rne_s), False),
        ("Drive torque\nsagittal, leg, stance", drive_s[0], drive_s[1], gross(*drive_s) + "\n" + ratio(*drive_s), False),
        ("DeVita 2007\nflat ground, 1.50 m/s", DEVITA_POS, DEVITA_NEG, gross(DEVITA_POS, DEVITA_NEG) + "\n" + ratio(DEVITA_POS, DEVITA_NEG), True),
    ], f"DeVita protocol: {rne_s[1] / rne_s[0]:.2f} neg/pos against 0.68 in humans",
        "Sagittal work of hip, knee and ankle during stance, per stance, on flat ground",
        context + "\nDeVita's ramp trials at 1.5 m/s had steps of 0.66–0.74 m, about twice these, so J per step is not like for like; the ratio is.",
        args.out / f"{name}_energy_stance.png")

    print(json.dumps({"full_body": {"internal_abs": lag_abs, "internal": lag, "rne": rne, "drive": drive},
                      "devita": {"rne": rne_s, "drive": drive_s}, "step_m": step, "steps_per_s": n / f["seconds"]}, indent=1))


if __name__ == "__main__":
    main()
