#!/usr/bin/env python3
"""Writes the unit primitives WorldView scales: resources/models/primitives/{box,sphere}.gltf.

Box: a copy of resources/models/box.gltf (unit cube centered on its origin). Sphere: a UV sphere of diameter 1,
24 segments and 16 rings, normals and UVs, one white material. Kept out of resources/models itself so base's
ModelLineup (non-recursive) does not pick them up. Capsules are generated at runtime per (radius, half-height).
"""

import base64
import json
import math
import pathlib
import shutil
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources" / "models" / "primitives"


def sphere(radius=0.5, segments=24, rings=16):
    positions, normals, uvs, indices = [], [], [], []
    for r in range(rings + 1):
        t = math.pi * r / rings
        y, ring = radius * math.cos(t), radius * math.sin(t)
        for s in range(segments + 1):
            a = 2 * math.pi * s / segments
            p = (ring * math.cos(a), y, -ring * math.sin(a))
            positions.append(p)
            n = math.sqrt(sum(c * c for c in p)) or 1.0
            normals.append(tuple(c / n for c in p) if n > 0 else (0.0, 1.0, 0.0))
            uvs.append((s / segments, r / rings))
    stride = segments + 1
    for r in range(rings):
        for s in range(segments):
            a = r * stride + s
            b, c = a + 1, a + stride
            d = c + 1
            indices += [a, c, b, b, c, d]
    return positions, normals, uvs, indices


def gltf(name, positions, normals, uvs, indices):
    data = b"".join(struct.pack("<3f", *p) for p in positions)
    data += b"".join(struct.pack("<3f", *n) for n in normals)
    data += b"".join(struct.pack("<2f", *u) for u in uvs)
    data += b"".join(struct.pack("<I", i) for i in indices)
    n = len(positions)
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    views = [(0, 12 * n, 34962), (12 * n, 12 * n, 34962), (24 * n, 8 * n, 34962), (32 * n, 4 * len(indices), 34963)]
    return {
        "asset": {"version": "2.0", "generator": "Dynamical tools/make_primitives.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": "White", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0, "roughnessFactor": 1}}],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
        "bufferViews": [{"buffer": 0, "byteOffset": o, "byteLength": l, "target": t} for o, l, t in views],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
    }


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "resources" / "models" / "box.gltf", OUT / "box.gltf")
    with open(OUT / "sphere.gltf", "w", newline="\n") as f:
        json.dump(gltf("Sphere", *sphere()), f, indent=1)
        f.write("\n")
    print("wrote", OUT / "box.gltf", OUT / "sphere.gltf")


if __name__ == "__main__":
    main()
