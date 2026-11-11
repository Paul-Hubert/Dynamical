"""Builds the engine's animal library from the animals of the Tautavel Unity project (FBX files, Unity materials).

    python tools/animal_import.py --source <Tautavel repo> [--lfs <lfs objects>] [--out resources/animals]

--source and --lfs are those of tools/speedtree_import.py : a checkout or a bare git repository, and the Git LFS
object store when the source only has pointers.

Three steps, each skipped for what is already done (--work, build/animals_work by default) :
    1. extract   the animal folders of the source are copied to <work>/src (LFS pointers resolved)
    2. dump      Blender (--blender, 4.5 or later : its FBX importer reads every FBX version) reads each FBX and
                 tools/animal_blender.py writes its skeleton, skinned meshes and animation takes to <work>/dump
    3. build     the dumps of an animal (its model files, its animation files) are merged, takes are cut in clips as
                 Unity's .meta says, materials are found through Unity's .mat files, textures converted

Output, read by src/logic/animals and src/renderer/passes/animal_pass :
    library.json                 every animal : id, name, size, clips
    species/<id>.json            skeleton, meshes, materials, clips
    species/<id>.bin             vertices (36 bytes each), indices (uint32), inverse bind matrices
    species/<id>.anim            the clips : per frame and bone, translation (3 float), rotation (4 int16), scale (float)
    textures/<name>.png          shared between animals

docs/animals.md describes the formats.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from speedtree_import import FolderSource, GitSource, lfs_resolve, slug  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Folders of the Unity project holding animals
ANIMAL_ROOTS = [
    "Assets/Tautavel/Faune/",
    "Assets/Tautavel/Realistic/",
    "Assets/Tautavel/_Flore et Mineraux/Faune/",
    "Assets/Tautavel/Animals/",
]
EXTRACTED = (".fbx", ".meta", ".mat", ".prefab", ".anim", ".controller", ".overridecontroller",
             ".png", ".tga", ".tif", ".tiff", ".jpg", ".jpeg", ".bmp", ".psd", ".exr", ".dds")


# ---------------------------------------------------------------------------------------------------------------------
# 1. Extract

def extract(source, lfs_dir, work):
    """Copies the animal folders to <work>/src. Returns [relative path] of what is there"""
    out_root = os.path.join(work, "src")
    state_path = os.path.join(work, "extract.json")
    state = {}
    if os.path.isfile(state_path):
        with open(state_path, "r", encoding="utf-8") as f:
            state = json.load(f)

    files = [(p, key) for p, key in source.list() if p.startswith(tuple(ANIMAL_ROOTS)) and p.lower().endswith(EXTRACTED)]
    present, missing, written = [], [], 0
    for path, key in files:
        target = os.path.join(out_root, path)
        if key is not None and state.get(path) == key and os.path.isfile(target):
            present.append(path)
            continue
        raw = source.read(path)
        data, _ = lfs_resolve(raw, lfs_dir) if raw is not None else (None, None)
        if data is None:
            missing.append(path)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(data)
        state[path] = key if key is not None else hashlib.sha1(data).hexdigest()
        present.append(path)
        written += 1
        if written % 200 == 0:
            print("  extracted %d files" % written, flush=True)

    with open(state_path, "w", encoding="utf-8") as f:
        json.dump(state, f)
    print("extract : %d files (%d new), %d missing" % (len(present), written, len(missing)))
    for path in missing:
        print("  missing : " + path)
    return present


# ---------------------------------------------------------------------------------------------------------------------
# 2. Dump : Blender reads the FBX files

DUMP_VERSION = 1


def find_blender(given):
    if given:
        return given
    candidates = []
    base = os.path.join(os.environ.get("ProgramFiles", "C:/Program Files"), "Blender Foundation")
    if os.path.isdir(base):
        for name in sorted(os.listdir(base), key=lambda n: [int(x) for x in re.findall(r"\d+", n)], reverse=True):
            candidates.append(os.path.join(base, name, "blender.exe"))
    candidates.append("blender")
    for candidate in candidates:
        if candidate == "blender" or os.path.isfile(candidate):
            return candidate
    return "blender"


def file_key(path):
    digest = hashlib.sha1()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def dump_all(work, files, blender):
    """Dumps every FBX not dumped yet. Returns { relative path: dump path without extension }, identical files share a dump"""
    src = os.path.join(work, "src")
    dump_dir = os.path.join(work, "dump")
    keys_path = os.path.join(work, "keys.json")
    keys = {}
    if os.path.isfile(keys_path):
        with open(keys_path, "r", encoding="utf-8") as f:
            keys = json.load(f)

    dumps, todo, queued = {}, [], set()
    for path in files:
        if not path.lower().endswith(".fbx"):
            continue
        full = os.path.join(src, path)
        stamp = "%d:%d" % (os.path.getsize(full), os.path.getmtime(full))
        if keys.get(path, [None])[0] != stamp:
            keys[path] = [stamp, file_key(full)]
        key = keys[path][1]
        out = os.path.join(dump_dir, key)
        dumps[path] = out
        done = False
        if os.path.isfile(out + ".json"):
            with open(out + ".json", "r", encoding="utf-8") as f:
                previous = json.load(f)
                done = previous.get("dump_version") == DUMP_VERSION and "error" not in previous
        if not done and key not in queued:
            queued.add(key)
            todo.append({ "fbx": full, "out": out, "dump_version": DUMP_VERSION })
    with open(keys_path, "w", encoding="utf-8") as f:
        json.dump(keys, f)

    print("dump : %d FBX files, %d different, %d to read" % (len(dumps), len(set(dumps.values())), len(todo)))
    if todo:
        job_path = os.path.join(work, "job.json")
        with open(job_path, "w", encoding="utf-8") as f:
            json.dump({ "files": todo }, f)
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "animal_blender.py")
        process = subprocess.Popen([blender, "--background", "--factory-startup", "--python", script, "--", job_path],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        for line in process.stdout:
            if "DUMP " in line or "Traceback" in line or "Error" in line:
                print("  " + line[max(line.find("DUMP "), 0):].rstrip(), flush=True)
        if process.wait() != 0:
            print("  Blender stopped with code %d" % process.returncode)
    return dumps


def open_source(args):
    if os.path.isdir(os.path.join(args.source, "Assets")):
        if args.lfs is None and os.path.isdir(os.path.join(args.source, ".git", "lfs", "objects")):
            args.lfs = os.path.join(args.source, ".git", "lfs", "objects")
        return FolderSource(args.source)
    if os.path.isfile(os.path.join(args.source, "HEAD")):
        return GitSource(args.source, args.ref)
    sys.exit("--source is neither a checkout (Assets/) nor a bare git repository")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, help="Tautavel checkout, or bare git repository")
    parser.add_argument("--ref", default="HEAD", help="commit read from a bare repository")
    parser.add_argument("--lfs", default=None, help="Git LFS object store, when the source only has pointers")
    parser.add_argument("--work", default=os.path.join(ROOT, "build", "animals_work"), help="extracted sources and Blender dumps")
    parser.add_argument("--out", default=os.path.join(ROOT, "resources", "animals"))
    parser.add_argument("--max-texture", type=int, default=2048, help="larger textures are downscaled")
    parser.add_argument("--only", default=None, help="regular expression on the animal id or source path : converts these, keeps the rest")
    parser.add_argument("--list", action="store_true", help="list the animals without converting")
    parser.add_argument("--blender", default=None, help="blender executable (the newest installed one by default)")
    args = parser.parse_args()

    source = open_source(args)
    os.makedirs(args.work, exist_ok=True)
    files = extract(source, args.lfs, args.work)
    dumps = dump_all(args.work, files, find_blender(args.blender))

    import animal_build
    animal_build.build(args.work, files, dumps, args.out, args.max_texture, args.only, args.list)


if __name__ == "__main__":
    main()
