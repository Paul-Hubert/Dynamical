#!/usr/bin/env python3
"""Builds the visual sign-off report of a phase: build/reports/P<n>/index.html (and summary.json).

    tools/phase_report.py P<n> [--no-tests] [--labels unit,scenario,protocol,gpu] [--build build] [--config RelWithDebInfo]

- runs CTest per label (JUnit output kept next to the report), unless --no-tests reuses the last results;
- lists the phase's test IDs from docs/physical-agents/phases.md with their CTest result;
- shows every capture under build/artifacts/P<n>/ next to its golden (tests/golden/images) and diff heatmap;
- shows the plots (build/artifacts/P<n>/plots/*.png) and Unity images (build/artifacts/P<n>/unity/*.png);
- copies the phase's visual checklist from phases.md, one checkbox per item.
"""

import argparse
import datetime
import html
import json
import pathlib
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
PHASES = ROOT / "docs" / "physical-agents" / "phases.md"
GOLDEN = ROOT / "tests" / "golden" / "images"


def phase_section(phase):
    text = PHASES.read_text(encoding="utf-8")
    match = re.search(rf"^## {phase} [^\n]*\n(.*?)(?=^## |\Z)", text, re.S | re.M)
    if not match:
        sys.exit(f"phase {phase} not found in {PHASES}")
    return match.group(1)


def test_table(section):
    block = re.search(r"\*\*Automated tests\.\*\*(.*?)(?=\*\*Visual confirmation|\Z)", section, re.S)
    tests = []
    for line in (block.group(1) if block else "").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 3 and re.match(r"^[A-Z]\d+\.\d+$", cells[0]):
            tests.append({"id": cells[0], "test": cells[1], "threshold": cells[2]})
    return tests


def checklist(section):
    block = re.search(r"\*\*Visual confirmation\.\*\*(.*?)(?=\*\*Exit gate|\Z)", section, re.S)
    if not block:
        return []
    items = []
    for line in block.group(1).splitlines():
        m = re.match(r"^(\s*)- (.*)$", line)
        if m:
            items.append({"depth": len(m.group(1)) // 2, "text": m.group(2).strip()})
        elif items and line.strip() and not line.strip().startswith("**"):
            items[-1]["text"] += " " + line.strip()
        elif line.strip() and not items:
            items.append({"depth": 0, "text": line.strip()})
    return items


def run_ctest(build, config, label, out):
    junit = out / f"ctest_{label}.xml"
    cmd = ["ctest", "--test-dir", str(build), "-C", config, "-L", f"^{label}$", "--output-junit", str(junit.resolve())]
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT)
    return junit


def read_junit(junit):
    if not junit.exists():
        return []
    results = []
    for case in ET.parse(junit).getroot().iter("testcase"):
        status = case.get("status", "run")
        if case.find("failure") is not None or status == "fail":
            status = "failed"
        elif case.find("skipped") is not None or status in ("notrun", "disabled"):
            status = "skipped"
        else:
            status = "passed"
        results.append({"name": case.get("name"), "status": status, "time": float(case.get("time", 0) or 0)})
    return results


def copy(src, dst_dir):
    dst_dir.mkdir(parents=True, exist_ok=True)
    dst = dst_dir / src.name
    shutil.copyfile(src, dst)
    return dst


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("phase")
    parser.add_argument("--no-tests", action="store_true")
    parser.add_argument("--labels", default="unit,scenario,protocol,gpu")
    parser.add_argument("--build", default=str(ROOT / "build"))
    parser.add_argument("--config", default="RelWithDebInfo")
    args = parser.parse_args()

    phase = args.phase.upper()
    build = pathlib.Path(args.build)
    out = build / "reports" / phase
    img = out / "img"
    if img.exists():
        shutil.rmtree(img)
    out.mkdir(parents=True, exist_ok=True)

    section = phase_section(phase)
    title = re.search(rf"^## {phase} [^\n]*", PHASES.read_text(encoding="utf-8"), re.M).group(0)[3:]

    labels = [l for l in args.labels.split(",") if l]
    results = {}
    for label in labels:
        junit = out / f"ctest_{label}.xml" if args.no_tests else run_ctest(build, args.config, label, out)
        results[label] = read_junit(junit)

    ids = test_table(section)
    for t in ids:
        t["ctest"] = [{"label": label, **r} for label, rs in results.items() for r in rs if r["name"].split(" ")[0] == t["id"]]

    artifacts = build / "artifacts" / phase
    captures, plots, unity = [], [], []
    if artifacts.exists():
        for png in sorted(artifacts.rglob("*.png")):
            rel = png.relative_to(artifacts)
            if png.stem.endswith("_diff") or rel.parts[0] == "try":
                continue
            if rel.parts[0] == "plots":
                plots.append(png)
            elif rel.parts[0] == "unity":
                unity.append(png)
            else:
                captures.append(png)

    rows = []
    for i, png in enumerate(captures):
        sub = img / f"c{i}"
        entry = {"capture": png.relative_to(build).as_posix(), "name": png.stem,
                 "src": copy(png, sub).relative_to(out).as_posix()}
        golden = GOLDEN / f"{png.stem}.png"
        if golden.exists():
            entry["golden"] = golden.relative_to(ROOT).as_posix()
            entry["golden_src"] = copy(golden, sub / "golden").relative_to(out).as_posix()
        diff = png.with_name(f"{png.stem}_diff.png")
        if diff.exists():
            entry["diff"] = diff.relative_to(build).as_posix()
            entry["diff_src"] = copy(diff, sub).relative_to(out).as_posix()
        rows.append(entry)
    plot_entries = [{"file": p.relative_to(build).as_posix(), "src": copy(p, img / "plots").relative_to(out).as_posix()} for p in plots]
    unity_entries = [{"file": p.relative_to(build).as_posix(), "src": copy(p, img / "unity").relative_to(out).as_posix()} for p in unity]

    items = checklist(section)
    sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()

    summary = {"phase": phase, "title": title, "commit": sha, "date": datetime.datetime.now().isoformat(timespec="seconds"),
               "labels": {l: {"passed": sum(r["status"] == "passed" for r in rs), "failed": sum(r["status"] == "failed" for r in rs),
                              "skipped": sum(r["status"] == "skipped" for r in rs), "tests": rs} for l, rs in results.items()},
               "ids": ids, "captures": rows, "plots": plot_entries, "unity": unity_entries, "checklist": items}
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    e = html.escape
    parts = [f"<!doctype html><meta charset=utf-8><title>{e(phase)} report</title>",
             "<style>body{font:14px sans-serif;margin:24px;max-width:1500px}table{border-collapse:collapse}"
             "td,th{border:1px solid #ccc;padding:4px 8px;vertical-align:top}.passed{color:#080}.failed{color:#c00;font-weight:bold}"
             ".skipped{color:#888}img{max-width:480px;border:1px solid #888}.row{display:flex;gap:8px;margin-bottom:16px}"
             ".row div{text-align:center}</style>",
             f"<h1>{e(title)}</h1><p>Commit {e(sha)} &middot; {e(summary['date'])}</p>",
             "<h2>Tests by label</h2><table><tr><th>Label</th><th>Passed</th><th>Failed</th><th>Skipped</th></tr>"]
    for label, s in summary["labels"].items():
        parts.append(f"<tr><td>{e(label)}</td><td class=passed>{s['passed']}</td><td class=failed>{s['failed'] or ''}</td><td>{s['skipped'] or ''}</td></tr>")
    parts.append("</table><h2>Phase test IDs</h2><table><tr><th>ID</th><th>Test</th><th>Threshold</th><th>CTest</th></tr>")
    for t in ids:
        status = ", ".join(f"<span class={r['status']}>{e(r['label'])}: {e(r['status'])}</span>" for r in t["ctest"]) or "<span class=skipped>not in CTest</span>"
        parts.append(f"<tr><td>{e(t['id'])}</td><td>{e(t['test'])}</td><td>{e(t['threshold'])}</td><td>{status}</td></tr>")
    parts.append("</table><h2>All tests</h2><table><tr><th>Label</th><th>Test</th><th>Status</th><th>Time (s)</th></tr>")
    for label, s in summary["labels"].items():
        for r in s["tests"]:
            parts.append(f"<tr><td>{e(label)}</td><td>{e(r['name'])}</td><td class={r['status']}>{r['status']}</td><td>{r['time']:.2f}</td></tr>")
    parts.append("</table><h2>Captures (capture, golden, diff)</h2>")
    for r in rows:
        parts.append(f"<h3>{e(r['name'])} <small>{e(r['capture'])}</small></h3><div class=row>")
        parts.append(f"<div><img src='{r['src']}'><br>capture</div>")
        if "golden_src" in r:
            parts.append(f"<div><img src='{r['golden_src']}'><br>golden {e(r['golden'])}</div>")
        if "diff_src" in r:
            parts.append(f"<div><img src='{r['diff_src']}'><br>diff heatmap</div>")
        parts.append("</div>")
    if plot_entries:
        parts.append("<h2>Plots</h2>" + "".join(f"<div><img src='{p['src']}' style='max-width:900px'><br>{e(p['file'])}</div>" for p in plot_entries))
    if unity_entries:
        parts.append("<h2>Unity</h2>" + "".join(f"<div><img src='{p['src']}'><br>{e(p['file'])}</div>" for p in unity_entries))
    parts.append("<h2>Visual checklist</h2><ul style='list-style:none'>")
    for item in items:
        parts.append(f"<li style='margin-left:{item['depth'] * 24}px'><label><input type=checkbox> {e(item['text'])}</label></li>")
    parts.append("</ul>")
    (out / "index.html").write_text("\n".join(parts), encoding="utf-8")

    for label, s in summary["labels"].items():
        print(f"{label}: {s['passed']} passed, {s['failed']} failed, {s['skipped']} skipped")
    print(f"{len(rows)} captures, {len(plot_entries)} plots -> {(out / 'index.html').as_posix()}")


if __name__ == "__main__":
    main()
