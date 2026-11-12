#!/usr/bin/env python3
"""A profile of a run, as text: starts dynamical.exe (or TrainingEnv.exe) with profile=true, records it with Tracy's
tracy-capture, then writes what tracy-csvexport makes of the trace (docs/profiling.md).

    python tools/profile_run.py [--name base] [--exe build/RelWithDebInfo/dynamical.exe] [--cwd build]
                            [--seconds 20] [--between FIRST LAST] [--frame-set Frame] [--top 40]
                            [--out build/profiles/NAME] [--again] -- scene=forest ticks=1200

    # the whole walk over the land (docs/tautavel-walk.md), and only the walk: not the loading before it
    python tools/profile_run.py --name walk --between "CameraWalk : started" "CameraWalk : finished" --frame-set Walk -- 
        scene=tautavel_walk var.exit=true gpu_times=true window_hidden=true fullscreen=false window_width=1920 window_height=1080 vr_mode=0

    # 600 frames of the walk over the land, without a window
    python tools/profile_run.py --name walk -- scene=tautavel_walk ticks=600 window_hidden=true
    # a capture script: the frames it waits for and its shots
    python tools/profile_run.py --name base -- capture=../tests/capture/base.capture.json
    # ten seconds of a training world, headless
    python tools/profile_run.py --name sandbox --exe build/RelWithDebInfo/TrainingEnv.exe --seconds 10 -- scene=sandbox_heuristic mode=bench

Everything after -- goes to the executable. The capture lasts until the process ends (ticks=N, a capture script,
var.exit=true on a scene with a CameraWalk) or --seconds, after which the process is stopped.
--between keeps the zones that begin between two messages of the trace (profile::message; the CameraWalk says when
it starts and when it is done): the statistics are then gathered here from every zone of the trace. --frame-set is
the set of frames the "per frame" column divides by ("Walk": the frames of the CameraWalk).
gpu_times=true has the window present without waiting for the monitor: the frames last what their work takes. In <out>:

    trace.tracy      the trace, opens in the Tracy profiler (lib/tracy/profiler)
    zones.csv        every CPU zone: calls, total, mean, min, max (the systems by their names in the scene)
    zones_self.csv   the same without the time of the zones inside
    gpu.csv          every GPU zone: calls, total, mean, median, 99th percentile, max (ms)
    gpu_events.csv   each GPU zone of each frame
    frames.csv       the frames (and "Headset", the frames of the headset)
    summary.txt      the tables below, printed as well

tracy-capture and tracy-csvexport are built from the submodule the first time (build/tracy_tools; --tools elsewhere).
"""

import argparse
import csv
import os
import pathlib
import statistics
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
TRACY = ROOT / "lib" / "tracy"


def build_tools(tools):
    """tracy-capture and tracy-csvexport of the submodule's version (the protocol must match the client's)."""
    exes = {}
    for name in ("capture", "csvexport"):
        exe = tools / name / "Release" / f"tracy-{name}.exe"
        if not exe.exists():
            print(f"Building tracy-{name} in {tools / name} ...", flush=True)
            subprocess.run(["cmake", "-S", str(TRACY / name), "-B", str(tools / name)], check=True, stdout=subprocess.DEVNULL)
            subprocess.run(["cmake", "--build", str(tools / name), "--config", "Release", "--target", f"tracy-{name}",
                            "--", "-m", "-p:UseMultiToolTask=true", "-v:minimal"], check=True, stdout=subprocess.DEVNULL)
        exes[name] = exe
    return exes


def export(csvexport, trace, path, *flags):
    with open(path, "w", newline="") as out:
        subprocess.run([str(csvexport), *flags, str(trace)], check=True, stdout=out, stderr=subprocess.DEVNULL)
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    if not rows or len(rows[0]) < 2:
        return []       # "There are currently no frames!"
    return [dict(zip(rows[0], row)) for row in rows[1:]]


def gather(csvexport, trace, path, low, high, *flags):
    """The statistics of csvexport, over the zones that begin in [low, high) ns of the trace."""
    process = subprocess.Popen([str(csvexport), "-u", *flags, str(trace)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               text=True, errors="replace")
    reader = csv.reader(process.stdout)
    next(reader, None)
    found = {}
    for row in reader:
        try:
            if not low <= int(row[3]) < high:
                continue
            time = int(row[4])
        except (IndexError, ValueError):    # a name with a comma in it
            continue
        entry = found.get((row[0], row[1], row[2]))
        if entry is None:
            found[(row[0], row[1], row[2])] = [1, time, time, time]
        else:
            entry[0] += 1
            entry[1] += time
            entry[2] = min(entry[2], time)
            entry[3] = max(entry[3], time)
    process.wait()
    zones = [{"name": key[0], "src_file": key[1], "src_line": key[2], "total_ns": total, "counts": count,
              "mean_ns": total / count, "min_ns": low_time, "max_ns": high_time}
             for key, (count, total, low_time, high_time) in found.items()]
    zones.sort(key=lambda z: -z["total_ns"])
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(zones[0].keys()) if zones else ["name"])
        writer.writeheader()
        writer.writerows(zones)
    return zones


def table(title, header, rows):
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    lines = [title, "  ".join(str(h).ljust(w) if i == 0 else str(h).rjust(w) for i, (h, w) in enumerate(zip(header, widths)))]
    for row in rows:
        lines.append("  ".join(str(c).ljust(w) if i == 0 else str(c).rjust(w) for i, (c, w) in enumerate(zip(row, widths))))
    return "\n".join(lines) + "\n"


def ms(ns):
    return f"{float(ns) * 1e-6:.3f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", default="run")
    parser.add_argument("--exe", type=pathlib.Path, default=BUILD / "RelWithDebInfo" / "dynamical.exe")
    parser.add_argument("--cwd", type=pathlib.Path, default=BUILD, help="where the executable runs (its resources/)")
    parser.add_argument("--seconds", type=float, default=0., help="stop after this long; 0: until the process ends")
    parser.add_argument("--between", nargs=2, metavar=("FIRST", "LAST"), help="only what lies between these two messages of the trace")
    parser.add_argument("--frame-set", default="Frame", help="the frames the per frame column divides by")
    parser.add_argument("--top", type=int, default=40, help="zones in the tables of the summary")
    parser.add_argument("--again", action="store_true", help="the tables of the trace that is there, without running")
    parser.add_argument("--out", type=pathlib.Path)
    parser.add_argument("--tools", type=pathlib.Path, default=BUILD / "tracy_tools")
    parser.add_argument("--port", type=int, default=8086, help="the first profiled process listens there, the next ones after it")
    parser.add_argument("args", nargs="*", help="arguments of the executable, after --")
    args = parser.parse_args()

    out = (args.out or BUILD / "profiles" / args.name).resolve()
    out.mkdir(parents=True, exist_ok=True)
    trace = out / "trace.tracy"
    tools = build_tools(args.tools.resolve())

    # The process keeps its events from its first instruction until the capture connects: nothing of the start is lost
    # and TRACY_NO_EXIT has it wait at its end for the capture to have everything, however short the run
    command = [str(args.exe.resolve()), "profile=true", *args.args]
    if not args.again:
        print(" ".join(command), flush=True)
        run(args, command, tools, trace, out)
    if not trace.exists():
        sys.exit(f"No trace: the profiler never connected (see {out / 'run.log'}; is another profiled process on port {args.port}?)")
    analyse(args, tools, trace, out)


def run(args, command, tools, trace, out):
    if trace.exists():
        trace.unlink()
    with open(out / "run.log", "w") as log:
        process = subprocess.Popen(command, cwd=args.cwd, stdout=log, stderr=subprocess.STDOUT,
                                   env={**os.environ, "TRACY_NO_EXIT": "1"})
        capture = [str(tools["capture"]), "-f", "-o", str(trace), "-p", str(args.port)]
        if args.seconds > 0:
            capture += ["-s", str(args.seconds)]
        capturing = subprocess.Popen(capture, stdout=subprocess.DEVNULL)
        try:
            # A process that died before the profiler started leaves the capture waiting for nobody
            gone = None
            while capturing.poll() is None:
                time.sleep(0.2)
                if process.poll() is None:
                    continue
                gone = gone or time.monotonic()
                if time.monotonic() - gone > 15.:
                    capturing.kill()
        finally:
            if capturing.poll() is None:
                capturing.kill()
            if args.seconds > 0 and process.poll() is None:
                process.terminate()
            try:
                code = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait()
    if code != 0 and args.seconds <= 0:
        print(f"The process ended with {code}, see {out / 'run.log'}", file=sys.stderr)


def analyse(args, tools, trace, out):
    frames = export(tools["csvexport"], trace, out / "frames.csv", "-F")
    gpu_events = export(tools["csvexport"], trace, out / "gpu_events.csv", "-g")
    summary = ""
    if args.between:
        # Zones one by one, gathered here over the part of the trace that was asked for
        export(tools["csvexport"], trace, out / "messages.csv", "-m")
        times = {}
        for line in reversed((out / "messages.csv").read_text(errors="replace").splitlines()[1:]):
            text, _, at = line.rpartition(",")     # Tracy's own messages have commas
            if at.strip().isdigit():
                times[text] = int(at)       # the first of a message said twice
        missing = [m for m in args.between if m not in times]
        if missing:
            sys.exit(f"No message {missing} in the trace (messages.csv)")
        low, high = times[args.between[0]], times[args.between[1]]
        summary += f"Between \"{args.between[0]}\" ({low * 1e-9:.2f} s) and \"{args.between[1]}\" ({high * 1e-9:.2f} s)\n\n"
        zones = gather(tools["csvexport"], trace, out / "zones.csv", low, high)
        zones_self = gather(tools["csvexport"], trace, out / "zones_self.csv", low, high, "-e")
        gpu_events = [e for e in gpu_events if low <= int(e["Time from start of program"]) < high]
    else:
        zones = export(tools["csvexport"], trace, out / "zones.csv")
        zones_self = export(tools["csvexport"], trace, out / "zones_self.csv", "-e")

    # The frames of the main loop are the set without a name ("Frame")
    main_frames = 0
    if frames:
        rows = []
        for f in frames:
            rows.append([f["frame_set"], f["frames"], ms(f["mean_ns"]), ms(f["median_ns"]), ms(f["p99_ns"]), ms(f["max_ns"])])
            if f["frame_set"] == args.frame_set:
                main_frames = int(f["frames"])
        summary += table("Frames (ms)", ["set", "frames", "mean", "median", "p99", "max"], rows) + "\n"
    per_frame = (lambda total: f"{total / main_frames:.3f}") if main_frames else (lambda total: "")

    # GPU zones: one row per event, gathered by name
    by_name = {}
    for event in gpu_events:
        by_name.setdefault(event["name"], []).append(float(event["GPU execution time"]) * 1e-6)
    gpu = sorted(by_name.items(), key=lambda item: -sum(item[1]))
    with open(out / "gpu.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["name", "counts", "total_ms", "mean_ms", "median_ms", "p99_ms", "max_ms", "ms_per_frame"])
        rows = []
        for name, times in gpu:
            times.sort()
            row = [name, len(times), f"{sum(times):.3f}", f"{statistics.fmean(times):.4f}", f"{statistics.median(times):.4f}",
                   f"{times[min(len(times) - 1, int(len(times) * 0.99))]:.4f}", f"{times[-1]:.4f}", per_frame(sum(times))]
            writer.writerow(row)
            rows.append(row)
    if rows:
        summary += table("GPU zones (ms; a zone holds the ones drawn inside it)",
                         ["name", "calls", "total", "mean", "median", "p99", "max", "per frame"], rows[:args.top]) + "\n"

    def cpu_rows(zones):
        rows = []
        for z in sorted(zones, key=lambda z: -float(z["total_ns"]))[:args.top]:
            total = float(z["total_ns"]) * 1e-6
            where = f"{pathlib.PurePath(z['src_file']).name}:{z['src_line']}"
            rows.append([z["name"], z["counts"], f"{total:.2f}", ms(z["mean_ns"]), ms(z["max_ns"]), per_frame(total), where])
        return rows

    header = ["name", "calls", "total", "mean", "max", "per frame", "where"]
    if zones_self:
        summary += table("CPU zones by their own time (ms, without the zones inside)", header, cpu_rows(zones_self)) + "\n"
    if zones:
        summary += table("CPU zones by their whole time (ms)", header, cpu_rows(zones)) + "\n"

    (out / "summary.txt").write_text(summary)
    print(summary)
    print(f"-> {out}")


if __name__ == "__main__":
    main()
