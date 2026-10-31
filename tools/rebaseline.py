#!/usr/bin/env python3
"""Replace a golden on purpose and record why.

    tools/rebaseline.py <name> --reason "<text>" [--from <file>]

<name> is an image name (base_spawn -> tests/golden/images/base_spawn.png) or a file name with an extension
(base_probe.csv -> tests/golden/base_probe.csv). Without --from, the newest build/artifacts/**/<name>.png
(or <name> itself for other files) is used. The reason is appended to tests/golden/CHANGELOG.md.
"""

import argparse
import datetime
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden"


def newest(pattern):
    found = [p for p in (ROOT / "build" / "artifacts").rglob(pattern) if not p.stem.endswith("_diff")]
    return max(found, key=lambda p: p.stat().st_mtime) if found else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name")
    parser.add_argument("--reason", required=True)
    parser.add_argument("--from", dest="source")
    args = parser.parse_args()

    is_image = "." not in args.name
    file_name = args.name + ".png" if is_image else args.name
    target = GOLDEN / "images" / file_name if is_image else GOLDEN / file_name

    source = pathlib.Path(args.source) if args.source else newest(file_name)
    if source is None or not source.exists():
        sys.exit(f"rebaseline: no capture found for {args.name}, pass --from")

    target.parent.mkdir(parents=True, exist_ok=True)
    action = "replaced" if target.exists() else "added"
    shutil.copyfile(source, target)

    try:
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    except OSError:
        sha = "unknown"

    changelog = GOLDEN / "CHANGELOG.md"
    if not changelog.exists():
        with changelog.open("w", encoding="utf-8", newline="\n") as f:
            f.write("# Golden changes\n\nEvery golden added or replaced, with the reason. Newest last.\n\n"
                    "| Date | Golden | Change | Base commit | Reason |\n|---|---|---|---|---|\n")
    with changelog.open("a", encoding="utf-8", newline="\n") as f:
        f.write(f"| {datetime.date.today().isoformat()} | `{target.relative_to(GOLDEN).as_posix()}` | {action} | {sha} | {args.reason} |\n")

    print(f"{action} {target.relative_to(ROOT).as_posix()} from {source}")


if __name__ == "__main__":
    main()
