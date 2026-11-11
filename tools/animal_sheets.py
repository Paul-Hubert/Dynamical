#!/usr/bin/env python3
"""Contact sheets of the animals' physical bodies: each body beside its mesh (docs/physical-agents/reports/animal-agents.md).

    build/venv/Scripts/python tools/animal_sheets.py [--species a,b] [--out build/runs/animal_bodies]

Reads resources/animals/bodies/<id>.body.json (tools/animal_body) and the library's species/<id>.json and .bin.
Writes <out>/<id>.png (the mesh and the shapes in the bind pose from the side, the front and above, and the standing
pose the episodes start from) and <out>/sheet_<n>.png (every animal: its mesh, and its body as it stands).
"""

import argparse
import json
import pathlib
import struct

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "resources" / "animals"


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def load_species(species):
    info = json.loads((LIBRARY / "species" / f"{species}.json").read_text())
    data = (LIBRARY / "species" / f"{species}.bin").read_bytes()
    count, indices, joints = info["vertex_count"], info["index_count"], len(info["joints"])
    vertices = np.frombuffer(data, dtype=np.dtype([("p", "<f4", 3), ("n", "i1", 4), ("uv", "<f4", 2), ("j", "<u2", 4), ("w", "u1", 4)]), count=count)
    index = np.frombuffer(data, dtype="<u4", count=indices, offset=count * 36)
    tail = np.frombuffer(data, dtype="<f4", count=joints * 22, offset=count * 36 + indices * 4)
    inverse_bind = tail[:joints * 12].reshape(joints, 3, 4)
    bind = np.zeros((joints, 3))
    for j in range(joints):
        m = np.eye(4)
        m[:3, :] = inverse_bind[j]
        bind[j] = np.linalg.inv(m)[:3, 3]
    lod = info["lods"][0]
    used = np.zeros(count, dtype=bool)
    for draw in lod["draws"]:
        used[index[draw["first_index"]:draw["first_index"] + draw["index_count"]]] = True
    return info, vertices, used, bind


def hull(points):
    points = sorted(set(map(tuple, np.round(points, 6))))
    if len(points) < 3:
        return np.array(points)

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in points:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(points):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return np.array(lower[:-1] + upper[:-1])


SPHERE = None


def sphere_points():
    global SPHERE
    if SPHERE is None:
        pts = []
        for a in np.linspace(0, np.pi, 9):
            for b in np.linspace(0, 2 * np.pi, 16, endpoint=False):
                pts.append([np.sin(a) * np.cos(b), np.cos(a), np.sin(a) * np.sin(b)])
        SPHERE = np.array(pts)
    return SPHERE


def shape_points(shape, position, rotation):
    """Points on the surface of a shape, world."""
    centre = np.array(shape["center"])
    local = quat_matrix(shape["rotation"])
    if shape["type"] == "box":
        h = np.array(shape["half_extents"])
        pts = np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]) * h
    elif shape["type"] == "sphere":
        pts = sphere_points() * shape["radius"]
    else:
        s = sphere_points() * shape["radius"]
        up = s.copy()
        up[:, 1] += shape["half_height"]
        down = s.copy()
        down[:, 1] -= shape["half_height"]
        pts = np.vstack([up, down])
    return position + (centre + pts @ local.T) @ rotation.T


VIEWS = {
    "side": (lambda p: np.stack([-p[:, 2], p[:, 1]], axis=1), "from its right"),
    "front": (lambda p: np.stack([p[:, 0], p[:, 1]], axis=1), "from the front"),
    "top": (lambda p: np.stack([-p[:, 2], p[:, 0]], axis=1), "from above"),
}


def draw_body(ax, body, view, pose, bind=None, alpha=0.35, labels=False):
    project = VIEWS[view][0]
    colors = plt.get_cmap("tab20")
    for i, seg in enumerate(body["segments"]):
        if pose == "bind":
            position, rotation = bind[seg["joint"]], np.eye(3)
        else:
            position, rotation = np.array(seg[f"{pose}_position"]), quat_matrix(seg[f"{pose}_rotation"])
        outline = hull(project(shape_points(seg["shape"], position, rotation)))
        color = colors(i % 20)
        if len(outline) >= 3:
            ax.fill(outline[:, 0], outline[:, 1], facecolor=color, edgecolor=color, alpha=alpha, linewidth=1.0)
            ax.plot(np.append(outline[:, 0], outline[0, 0]), np.append(outline[:, 1], outline[0, 1]), color=color, linewidth=1.0)
        origin = project(position[None, :])[0]
        ax.plot(origin[0], origin[1], "o", color="black", markersize=2.2)
        if labels:
            ax.annotate(seg["name"], origin, fontsize=4.5, color="black", alpha=0.8)


def draw_mesh(ax, vertices, used, view, size=0.25):
    project = VIEWS[view][0]
    p = vertices["p"][used]
    if len(p) > 12000:
        p = p[np.random.default_rng(1).choice(len(p), 12000, replace=False)]
    q = project(p)
    ax.scatter(q[:, 0], q[:, 1], s=size, c="#555555", alpha=0.5, linewidths=0)


def finish(ax, title=None, ground=None):
    ax.set_aspect("equal")
    ax.tick_params(labelsize=5)
    if ground is not None:
        ax.axhline(ground, color="#886633", linewidth=0.8)
    if title:
        ax.set_title(title, fontsize=7)


def species_figure(species, out):
    body = json.loads((LIBRARY / "bodies" / f"{species}.body.json").read_text())
    info, vertices, used, bind = load_species(species)
    fig, axes = plt.subplots(2, 3, figsize=(16, 9.5), dpi=110)
    for column, view in enumerate(["side", "front", "top"]):
        ax = axes[0][column]
        draw_mesh(ax, vertices, used, view)
        draw_body(ax, body, view, "bind", bind, labels=(view == "side"))
        finish(ax, f"bind pose, mesh and shapes, {VIEWS[view][1]}")
    ax = axes[1][0]
    draw_body(ax, body, "side", "spawn", alpha=0.5)
    finish(ax, "standing pose (episodes start here)", ground=0.0)
    ax = axes[1][1]
    draw_body(ax, body, "front", "spawn", alpha=0.5)
    finish(ax, "standing pose, from the front", ground=0.0)
    ax = axes[1][2]
    ax.axis("off")
    lines = [f"{species}: {len(body['segments'])} bodies, {len(body['joints'])} joints, {body['total_mass']:.1f} kg, "
             f"height {body['height']:.2f} m, centre of mass {body['com_height']:.2f} m, {body['substeps']} substeps",
             f"stand: {body['stand_clip'] or 'bind pose'}; walk {body['walk_clip']} {body['walk_speed']:.2f} m/s",
             "joint (parent): twist min max, swing y z (deg), hinge, stiffness N m/rad, mass of child kg"]
    for joint in body["joints"]:
        lim = joint["limits_deg"]
        child = body["segments"][joint["child"]]
        parent = body["segments"][joint["parent"]]
        lines.append(f"{joint['name'][:22]:22s} ({parent['name'][:14]:14s}) {lim[0]:6.0f} {lim[1]:5.0f}  {lim[2]:4.0f} {lim[3]:4.0f} "
                     f"{'H' if joint['hinge'] else ' '}{'S' if joint['support'] else ' '} {joint['stiffness']:9.1f} {child['mass']:7.2f} {child['role']}")
    ax.text(0.0, 1.0, "\n".join(lines), fontsize=5.6, family="monospace", va="top", transform=ax.transAxes)
    fig.tight_layout()
    fig.savefig(out / f"{species}.png")
    plt.close(fig)
    return body, (info, vertices, used, bind)


def sheet(entries, out, index, columns=4):
    rows = (len(entries) + columns - 1) // columns
    fig, axes = plt.subplots(rows, columns * 2, figsize=(columns * 5.2, rows * 2.6), dpi=120, squeeze=False)
    for ax in axes.flat:
        ax.axis("off")
    for n, (species, body, (info, vertices, used, bind)) in enumerate(entries):
        r, c = divmod(n, columns)
        left, right = axes[r][2 * c], axes[r][2 * c + 1]
        left.axis("on")
        right.axis("on")
        draw_mesh(left, vertices, used, "side", size=0.15)
        finish(left, f"{species[:28]}: mesh")
        draw_mesh(right, vertices, used, "side", size=0.08)
        draw_body(right, body, "side", "bind", bind, alpha=0.45)
        finish(right, f"{len(body['segments'])} bodies, {body['total_mass']:.0f} kg" if body['total_mass'] >= 10 else f"{len(body['segments'])} bodies, {body['total_mass']:.2f} kg")
        right.set_xlim(left.get_xlim())
        right.set_ylim(left.get_ylim())
    fig.tight_layout()
    path = out / f"sheet_{index}.png"
    fig.savefig(path)
    plt.close(fig)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--species", default="all")
    parser.add_argument("--out", default=str(ROOT / "build" / "runs" / "animal_bodies"))
    parser.add_argument("--per-sheet", type=int, default=12)
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if args.species == "all":
        ids = sorted(p.name[:-len(".body.json")] for p in (LIBRARY / "bodies").glob("*.body.json"))
    else:
        ids = args.species.split(",")
    entries = []
    for species in ids:
        body, loaded = species_figure(species, out)
        entries.append((species, body, loaded))
        print(species, flush=True)
    for n in range(0, len(entries), args.per_sheet):
        print(sheet(entries[n:n + args.per_sheet], out, n // args.per_sheet + 1))


if __name__ == "__main__":
    main()
