"""Builds the engine's vegetation library from SpeedTree 8 files (.st, the Unity export of the Tautavel project).

    python tools/speedtree_import.py --source <Tautavel repo> [--lfs <lfs objects>] [--out resources/vegetation]

--source is either a checkout (a folder holding Assets/...) or a bare git repository (then --ref picks the commit).
Textures tracked by Git LFS are read from --lfs when the source only has the pointers (a bare repository, or a
checkout made without LFS): the folder holding the objects as <oid[0:2]>/<oid[2:4]>/<rest> (Gitea) or
<oid[0:2]>/<oid[2:4]>/<oid> (.git/lfs/objects).

Output, read by src/logic/vegetation and src/renderer/vegetation:
    library.json                 every species: id, name, category, size
    species/<id>.json            materials, levels of detail, draw calls
    species/<id>.bin             vertices (28 bytes each) then indices (uint32), all levels of detail
    textures/<name>.png          shared between species

What is kept from the .st: geometry of every level of detail (billboard cards included), normals, tangents,
texture coordinates, ambient occlusion, materials and their maps. Wind data, bones, collision objects and lightmap
coordinates are dropped. docs/vegetation.md describes the file format.
"""

import argparse
import hashlib
import io
import json
import os
import re
import struct
import subprocess
import sys
import unicodedata

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

FORMAT_VERSION = 1
VERTEX_FLOATS = 35          # SpeedTree 8 vertex : 140 bytes
MAP_NAMES = ["color", "normal", "extra", "subsurface"]
LFS_HEADER = b"version https://git-lfs.github.com/spec/v1"

# Folders searched first when the same file exists in several places (the first one names the species)
ROOT_ORDER = ["Assets/Tautavel/_Flore/", "Assets/Tautavel/Plants/", "Assets/Tautavel/_Flore et Mineraux/", "Assets/Tautavel/CG/"]


# ---------------------------------------------------------------------------------------------------------------------
# Sources : a checkout or a bare repository

class Source:
    def list(self):
        """[(path, content id)] of every file, with forward slashes"""
        raise NotImplementedError

    def read(self, path):
        raise NotImplementedError


class FolderSource(Source):
    def __init__(self, root):
        self.root = root

    def list(self):
        out = []
        for folder, _, files in os.walk(os.path.join(self.root, "Assets")):
            for name in files:
                full = os.path.join(folder, name)
                rel = os.path.relpath(full, self.root).replace("\\", "/")
                out.append((rel, None))
        return out

    def read(self, path):
        full = os.path.join(self.root, path)
        if not os.path.isfile(full):
            return None
        with open(full, "rb") as f:
            return f.read()


class GitSource(Source):
    def __init__(self, git_dir, ref):
        self.git_dir = git_dir
        self.ref = ref
        self.blobs = {}

    def git(self, *args):
        return subprocess.run(["git", "--git-dir", self.git_dir, *args], check=True, capture_output=True).stdout

    def list(self):
        out = []
        for entry in self.git("ls-tree", "-r", "-z", self.ref).split(b"\0"):
            if not entry:
                continue
            meta, path = entry.split(b"\t", 1)
            sha = meta.split()[2].decode()
            path = path.decode("utf-8", "replace")
            self.blobs[path] = sha
            out.append((path, sha))
        return out

    def read(self, path):
        sha = self.blobs.get(path)
        if sha is None:
            return None
        return self.git("cat-file", "blob", sha)


def lfs_resolve(data, lfs_dir):
    """Content of an LFS pointer, or data itself when it is not one. Returns (bytes or None, oid or None)"""
    if not data.startswith(LFS_HEADER):
        return data, None
    match = re.search(rb"oid sha256:([0-9a-f]{64})", data)
    if not match or not lfs_dir:
        return None, None
    oid = match.group(1).decode()
    for candidate in (os.path.join(lfs_dir, oid[0:2], oid[2:4], oid[4:]), os.path.join(lfs_dir, oid[0:2], oid[2:4], oid)):
        if os.path.isfile(candidate):
            with open(candidate, "rb") as f:
                return f.read(), oid
    return None, oid


# ---------------------------------------------------------------------------------------------------------------------
# SpeedTree 8 .st : "SpeedTree___" then nested tables (count, offsets relative to the table) down to raw values

class StError(Exception):
    pass


class StFile:
    def __init__(self, data):
        if data[:12] != b"SpeedTree___":
            raise StError("not a SpeedTree 8 file")
        self.data = data

    def table(self, base):
        count = struct.unpack_from("<I", self.data, base)[0]
        if count > 1 << 20 or base + 4 + 4 * count > len(self.data):
            raise StError("bad table at %d" % base)
        return [base + o for o in struct.unpack_from("<%dI" % count, self.data, base + 4)]

    def u32(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def u8(self, at):
        return self.data[at]

    def string(self, at):
        length = self.u32(at)
        return self.data[at + 4:at + 4 + length].split(b"\0")[0].decode("utf-8", "replace")

    def floats(self, at, count):
        return list(struct.unpack_from("<%df" % count, self.data, at))

    def parse(self):
        root = self.table(12)
        if len(root) < 6:
            raise StError("unexpected root table (%d entries)" % len(root))
        version = self.u32(root[0])
        if version != 8:
            raise StError("unsupported version %d" % version)
        last_lod_is_billboard = self.u8(root[2]) != 0

        lods = []
        for base in self.table(root[3]):
            v, i, d = self.table(base)[:3]
            vertex_count = self.u32(v)
            vertices = np.frombuffer(self.data, dtype="<f4", count=vertex_count * VERTEX_FLOATS, offset=v + 4).reshape(vertex_count, VERTEX_FLOATS)
            index_count = self.u32(i)
            indices = np.frombuffer(self.data, dtype="<u4", count=index_count, offset=i + 4)
            draw_count = self.u32(d)
            # material, geometry hint (0 branch, 2 leaves, 4 billboard), first index, index count
            draws = np.frombuffer(self.data, dtype="<u4", count=draw_count * 4, offset=d + 4).reshape(draw_count, 4)
            lods.append((vertices, indices, draws))

        materials = []
        for base in self.table(root[5]):
            name, two_sided, billboard, maps = self.table(base)[:4]
            material = { "name": self.string(name), "two_sided": self.u8(two_sided) != 0, "billboard": self.u8(billboard) != 0, "maps": [] }
            for m in self.table(maps):
                used, path, color = self.table(m)[:3]
                material["maps"].append({ "used": self.u8(used) != 0, "path": self.string(path) if self.u8(used) else "", "color": self.floats(color, 4) })
            materials.append(material)

        return { "last_lod_is_billboard": last_lod_is_billboard, "lods": lods, "materials": materials }


def parse_meta(text):
    """The few values of Unity's .st.meta the engine uses"""
    meta = { "scale": 0.3048, "alpha_cutoff": 0.33, "lod_heights": [] }
    if not text:
        return meta
    if m := re.search(r"^\s*scaleFactor:\s*([0-9.eE+-]+)", text, re.M):
        meta["scale"] = float(m.group(1))
    if m := re.search(r"^\s*alphaTestRef:\s*([0-9.eE+-]+)", text, re.M):
        meta["alpha_cutoff"] = float(m.group(1))
    meta["lod_heights"] = [float(h) for h in re.findall(r"^\s*-\s*height:\s*([0-9.eE+-]+)", text, re.M)]
    return meta


# ---------------------------------------------------------------------------------------------------------------------
# Conversion

def slug(text):
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def snorm8(values):
    return np.clip(np.rint(np.nan_to_num(values) * 127.0), -127, 127).astype(np.int8)


def convert_lod(vertices, indices, draws, scale):
    """Engine vertices (structured array), indices with the winding of the engine, bounds"""
    v = np.nan_to_num(vertices.astype(np.float64), nan=0.0, posinf=0.0, neginf=0.0)

    # Positions are stored relative to the anchor (the pivot of the leaf or of the branch)
    position = (v[:, 0:3] + v[:, 3:6]) * scale
    normal, tangent, binormal = v[:, 9:12], v[:, 12:15], v[:, 15:18]

    def normalized(a, fallback):
        length = np.linalg.norm(a, axis=1, keepdims=True)
        return np.where(length > 1e-6, a / np.maximum(length, 1e-6), fallback)

    normal = normalized(normal, np.array([0.0, 1.0, 0.0]))
    tangent = normalized(tangent, np.array([1.0, 0.0, 0.0]))
    # binormal = sign * cross(normal, tangent)
    sign = np.where(np.einsum("ij,ij->i", np.cross(normal, tangent), binormal) < 0.0, -1.0, 1.0)

    out = np.zeros(len(v), dtype=[("position", "<f4", 3), ("normal", "i1", 4), ("tangent", "i1", 4), ("uv", "<f4", 2)])
    out["position"] = position
    out["normal"][:, 0:3] = snorm8(normal)
    out["normal"][:, 3] = snorm8(sign)
    out["tangent"][:, 0:3] = snorm8(tangent)
    out["tangent"][:, 3] = snorm8(np.clip(v[:, 31], 0.0, 1.0))  # ambient occlusion
    # SpeedTree's v goes up, images are stored top row first
    out["uv"][:, 0] = v[:, 18]
    out["uv"][:, 1] = 1.0 - v[:, 19]

    # Counter clockwise front faces : flip the draw calls wound the other way
    idx = indices.astype(np.uint32).copy()
    flipped = 0
    for material, hint, first, count in draws:
        tri = idx[first:first + count].reshape(-1, 3)
        if len(tri) == 0:
            continue
        geometric = np.cross(position[tri[:, 1]] - position[tri[:, 0]], position[tri[:, 2]] - position[tri[:, 0]])
        vertex = normal[tri[:, 0]] + normal[tri[:, 1]] + normal[tri[:, 2]]
        if np.mean(np.einsum("ij,ij->i", geometric, vertex) > 0.0) < 0.5:
            idx[first:first + count] = tri[:, ::-1].reshape(-1)
            flipped += 1

    used = np.unique(idx) if len(idx) else np.arange(0)
    bounds = (position[used].min(0), position[used].max(0)) if len(used) else (np.zeros(3), np.zeros(3))
    return out, idx, bounds, flipped


class TextureStore:
    """Writes each source texture once, whatever the number of species using it"""

    def __init__(self, out_dir, max_size, source, lfs_dir):
        self.out_dir = os.path.join(out_dir, "textures")
        self.max_size = max_size
        self.source = source
        self.lfs_dir = lfs_dir
        self.by_key = {}        # content key -> file name
        self.names = set()
        self.cache_path = os.path.join(self.out_dir, "cache.json")
        self.cache = {}
        if os.path.isfile(self.cache_path):
            with open(self.cache_path, "r", encoding="utf-8") as f:
                self.cache = json.load(f)
        self.written = 0
        self.reused = 0
        self.missing = []

    def get(self, path, kind):
        raw = self.source.read(path)
        if raw is None:
            self.missing.append(path)
            return None
        data, oid = lfs_resolve(raw, self.lfs_dir)
        if data is None:
            self.missing.append(path + " (LFS object not found)")
            return None
        key = (oid or hashlib.sha256(data).hexdigest()) + ":" + kind + ":" + str(self.max_size)
        if key in self.by_key:
            return "textures/" + self.by_key[key]

        base = slug(os.path.splitext(os.path.basename(path))[0])
        name = base + ".png"
        n = 2
        while name in self.names:
            name = "%s_%d.png" % (base, n)
            n += 1
        self.names.add(name)
        self.by_key[key] = name

        target = os.path.join(self.out_dir, name)
        if self.cache.get(name) == key and os.path.isfile(target):
            self.reused += 1
            return "textures/" + name

        os.makedirs(self.out_dir, exist_ok=True)
        image = Image.open(io.BytesIO(data))
        image = image.convert("RGBA")
        if max(image.size) > self.max_size:
            ratio = self.max_size / max(image.size)
            size = (max(1, round(image.width * ratio)), max(1, round(image.height * ratio)))
            if kind == "color":
                # Premultiplied resize, so the color of transparent texels does not bleed into the leaves
                rgba = np.asarray(image).astype(np.float32) / 255.0
                premultiplied = Image.fromarray((np.concatenate([rgba[:, :, :3] * rgba[:, :, 3:], rgba[:, :, 3:]], axis=2) * 255.0 + 0.5).astype(np.uint8), "RGBA")
                small = np.asarray(premultiplied.resize(size, Image.LANCZOS)).astype(np.float32) / 255.0
                alpha = small[:, :, 3:]
                color = np.where(alpha > 1e-3, small[:, :, :3] / np.maximum(alpha, 1e-3), np.asarray(image.resize(size, Image.BILINEAR)).astype(np.float32)[:, :, :3] / 255.0)
                image = Image.fromarray((np.clip(np.concatenate([color, alpha], axis=2), 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGBA")
            else:
                image = image.resize(size, Image.LANCZOS)
        image.save(target, compress_level=6)
        self.cache[name] = key
        self.written += 1
        return "textures/" + name

    def finish(self):
        if self.written:
            with open(self.cache_path, "w", encoding="utf-8") as f:
                json.dump(self.cache, f, indent=1, sort_keys=True)


def category_of(height):
    if height < 1.0:
        return "ground"
    if height < 4.0:
        return "shrub"
    return "tree"


def convert_species(source, path, species_id, textures, out_dir):
    data = source.read(path)
    data, _ = lfs_resolve(data, textures.lfs_dir)
    if data is None:
        raise StError("LFS object not found")
    st = StFile(data).parse()
    meta_raw = source.read(path + ".meta")
    meta = parse_meta(meta_raw.decode("utf-8", "replace") if meta_raw else "")
    folder = os.path.dirname(path)

    materials = []
    for m in st["materials"]:
        material = { "name": m["name"], "two_sided": m["two_sided"], "billboard": m["billboard"] }
        for index, kind in enumerate(MAP_NAMES):
            entry = m["maps"][index] if index < len(m["maps"]) else None
            material[kind] = textures.get(folder + "/" + entry["path"], kind) if entry and entry["used"] and entry["path"] else None
        if m["maps"]:
            material["tint"] = [round(c, 4) for c in m["maps"][0]["color"]]
        materials.append(material)

    if not any(m["color"] for m in materials):
        raise StError("none of its textures are in the source")

    lods = []
    vertex_chunks, index_chunks = [], []
    first_vertex = first_index = 0
    bounds = None
    flipped = 0
    lod_count = len(st["lods"])
    for level, (vertices, indices, draws) in enumerate(st["lods"]):
        out, idx, lod_bounds, f = convert_lod(vertices, indices, draws, meta["scale"])
        flipped += f
        billboard = st["last_lod_is_billboard"] and level == lod_count - 1 and lod_count > 1
        if bounds is None:
            bounds = lod_bounds
        heights = meta["lod_heights"]
        lod = {
            "first_vertex": first_vertex,
            "vertex_count": len(out),
            # Unity's LODGroup : this level is drawn while the plant is taller than this fraction of the screen
            "screen_height": heights[level] if len(heights) == lod_count else 0.5 ** (level + 1),
            "billboard": bool(billboard),
            "draws": [{ "material": int(mat), "first_index": first_index + int(first), "index_count": int(count) } for mat, _, first, count in draws if count > 0],
        }
        for draw in lod["draws"]:
            if draw["material"] >= len(materials):
                raise StError("draw call with material %d of %d" % (draw["material"], len(materials)))
        lods.append(lod)
        vertex_chunks.append(out)
        index_chunks.append(idx)
        first_vertex += len(out)
        first_index += len(idx)

    if not lods or first_index == 0:
        raise StError("no geometry")

    os.makedirs(os.path.join(out_dir, "species"), exist_ok=True)
    with open(os.path.join(out_dir, "species", species_id + ".bin"), "wb") as f:
        for chunk in vertex_chunks:
            f.write(chunk.tobytes())
        for chunk in index_chunks:
            f.write(chunk.astype("<u4").tobytes())

    size = bounds[1] - bounds[0]
    height = float(bounds[1][1])
    radius = float(max(abs(bounds[0][0]), abs(bounds[1][0]), abs(bounds[0][2]), abs(bounds[1][2])))
    species = {
        "format_version": FORMAT_VERSION,
        "id": species_id,
        "name": os.path.splitext(os.path.basename(path))[0],
        "source": path,
        "category": category_of(height),
        "height": round(height, 4),
        "radius": round(radius, 4),
        "bounds": { "min": [round(float(x), 4) for x in bounds[0]], "max": [round(float(x), 4) for x in bounds[1]] },
        "alpha_cutoff": meta["alpha_cutoff"],
        "vertex_count": first_vertex,
        "index_count": first_index,
        "materials": materials,
        "lods": lods,
    }
    with open(os.path.join(out_dir, "species", species_id + ".json"), "w", encoding="utf-8") as f:
        json.dump(species, f, indent=1)

    return species, flipped


def find_species(source):
    """Unique .st files, [(path, id)], the first of ROOT_ORDER naming duplicates"""
    def order(path):
        for rank, root in enumerate(ROOT_ORDER):
            if path.startswith(root):
                return (rank, path.lower())
        return (len(ROOT_ORDER), path.lower())

    files = sorted([(p, key) for p, key in source.list() if p.lower().endswith(".st")], key=lambda e: order(e[0]))
    seen_content = set()
    ids = {}
    out = []
    for path, key in files:
        if key is None:
            key = hashlib.sha1(source.read(path)).hexdigest()
        if key in seen_content:
            continue
        seen_content.add(key)
        species_id = slug(os.path.splitext(os.path.basename(path))[0])
        if species_id in ids:
            species_id = slug(os.path.basename(os.path.dirname(path))) + "_" + species_id
        n = 2
        base = species_id
        while species_id in ids:
            species_id = "%s_%d" % (base, n)
            n += 1
        ids[species_id] = path
        out.append((path, species_id))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, help="Tautavel checkout, or bare git repository")
    parser.add_argument("--ref", default="HEAD", help="commit read from a bare repository")
    parser.add_argument("--lfs", default=None, help="Git LFS object store, when the source only has pointers")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "resources", "vegetation"))
    parser.add_argument("--max-texture", type=int, default=2048, help="larger textures are downscaled")
    parser.add_argument("--only", default=None, help="regular expression on the species id or source path")
    parser.add_argument("--list", action="store_true", help="list the species without converting")
    args = parser.parse_args()

    if os.path.isdir(os.path.join(args.source, "Assets")):
        source = FolderSource(args.source)
        if args.lfs is None and os.path.isdir(os.path.join(args.source, ".git", "lfs", "objects")):
            args.lfs = os.path.join(args.source, ".git", "lfs", "objects")
    elif os.path.isfile(os.path.join(args.source, "HEAD")):
        source = GitSource(args.source, args.ref)
    else:
        sys.exit("--source is neither a checkout (Assets/) nor a bare git repository")

    found = find_species(source)
    if args.only:
        pattern = re.compile(args.only, re.I)
        found = [(p, i) for p, i in found if pattern.search(i) or pattern.search(p)]

    if args.list:
        for path, species_id in found:
            print("%-48s %s" % (species_id, path))
        print(len(found), "species")
        return

    textures = TextureStore(args.out, args.max_texture, source, args.lfs)
    entries = []
    failed = []
    for path, species_id in found:
        try:
            species, flipped = convert_species(source, path, species_id, textures, args.out)
        except (StError, struct.error, ValueError, OSError) as e:
            failed.append((path, str(e)))
            print("%-48s left out : %s" % (species_id, e))
            continue
        lod0 = species["lods"][0]
        triangles = sum(d["index_count"] for d in lod0["draws"]) // 3
        entries.append({ "id": species_id, "name": species["name"], "category": species["category"], "height": species["height"],
                         "radius": species["radius"], "triangles": triangles, "lods": len(species["lods"]) })
        print("%-48s %-6s %6.2f m  %7d triangles  %d lods%s" % (species_id, species["category"], species["height"], triangles, len(species["lods"]),
                                                             "  (%d draw calls rewound)" % flipped if flipped else ""))
    textures.finish()

    # A partial run (--only) updates its species and keeps the others
    library_path = os.path.join(args.out, "library.json")
    if args.only and os.path.isfile(library_path):
        with open(library_path, "r", encoding="utf-8") as f:
            previous = json.load(f).get("species", [])
        done = { e["id"] for e in entries }
        entries += [e for e in previous if e["id"] not in done]
    else:
        # A full run owns the folder : species that are gone from the source, or no longer convert, are removed
        kept = { e["id"] for e in entries }
        species_dir = os.path.join(args.out, "species")
        for name in os.listdir(species_dir) if os.path.isdir(species_dir) else []:
            if os.path.splitext(name)[0] not in kept:
                os.remove(os.path.join(species_dir, name))
    entries.sort(key=lambda e: e["id"])
    with open(library_path, "w", encoding="utf-8") as f:
        json.dump({ "format_version": FORMAT_VERSION, "species": entries }, f, indent=1)

    print("%d species, %d textures written, %d already there" % (len(entries), textures.written, textures.reused))
    if textures.missing:
        print("%d missing textures :" % len(textures.missing))
        for m in sorted(set(textures.missing)):
            print("  " + m)
    if failed:
        print("%d files left out :" % len(failed))
        for path, reason in failed:
            print("  %s : %s" % (path, reason))


if __name__ == "__main__":
    main()
