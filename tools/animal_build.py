"""Third step of tools/animal_import.py : from the Blender dumps and the Unity files to the engine's animal library.

What belongs together is worked out from the files, not from a list :
    - FBX files of a folder whose names only differ by a quality or motion word (Cat_high / Cat_low, Deer_red_LOD /
      Deer_red_IP / Deer_red_RM, badger@walk / badger@run) and that share a skeleton are one animal
    - files without a mesh (Wolf@Walk.FBX) give their clips to the animals around them they fit : same bone names,
      and bones that stay attached when the clip is played on the animal's bind pose
    - an animal without clips takes the clips of one around it with the same skeleton (Cat_Fat, from Cat)
    - meshes found twice (the LOD0 of an animation file, the HighPoly file) are kept once
"""

import json
import os
import re
import struct

import numpy as np

from speedtree_import import FolderSource, TextureStore, slug, snorm8

FORMAT_VERSION = 1

# Blender's world (Z up, -Y forward) to the engine's (Y up) : a half turn about the vertical after the change of
# axes, so that an animal looking down +Z in its FBX (Unity's forward) looks down -Z, the engine's forward
TO_ENGINE = np.array([[-1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.0, 0.0]])

# Words of a file name that do not name the animal
NAME_NOISE = r"high ?poly|low ?poly|lods?|high|hi|low\d*|midle|middle|mid|full|ip|rm|mesh|sk|sv|mx|brm"
PACK_TAGS = [("Forest Animal", "forest"), ("Realistic", "realistic"), ("Domestic_Animals", "farm"), ("Fur_FBX", "fur")]
GENERIC_TAKES = { "take 001", "mixamo.com", "scene", "default take", "take001", "" }
TEXTURE_EXTENSIONS = (".png", ".tga", ".tif", ".tiff", ".jpg", ".jpeg", ".bmp", ".psd")


def to44(m):
    out = np.zeros(m.shape[:-2] + (4, 4))
    out[..., :3, :] = m
    out[..., 3, 3] = 1.0
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Unity side : .meta of the models (clips, scale, materials), .mat files

def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def parse_model_meta(text):
    meta = { "clips": [], "global_scale": 1.0, "use_file_scale": True, "materials": {} }
    if not text:
        return meta
    if m := re.search(r"^\s*globalScale:\s*([0-9.eE+-]+)", text, re.M):
        meta["global_scale"] = float(m.group(1))
    if m := re.search(r"^\s*useFileScale:\s*(\d)", text, re.M):
        meta["use_file_scale"] = m.group(1) != "0"

    for m in re.finditer(r"- first:\s*\n\s*type: UnityEngine:Material\s*\n\s*assembly: [^\n]*\n\s*name: ([^\n]*)\n\s*second: \{fileID: \d+, guid: ([0-9a-f]{32})", text):
        meta["materials"][m.group(1).strip()] = m.group(2)

    section = re.search(r"^    clipAnimations:\s*\n(.*?)(?=^    \w|^  \w)", text, re.M | re.S)
    if section:
        for block in re.split(r"^    - serializedVersion: \d+\s*\n", section.group(1), flags=re.M)[1:]:
            def field(name, default=""):
                found = re.search(r"^      %s:[ ]?(.*)$" % name, block, re.M)
                return found.group(1).strip() if found else default
            try:
                meta["clips"].append({
                    "name": field("name").strip("'\""),
                    "take": field("takeName").strip("'\""),
                    "first": int(round(float(field("firstFrame", "0")))),
                    "last": int(round(float(field("lastFrame", "0")))),
                    "loop": field("loop", "0") == "1" or field("loopTime", "0") == "1" or field("wrapMode", "0") == "2",
                })
            except ValueError:
                pass
    return meta


def fbx_unit_scale(path):
    """UnitScaleFactor of a binary FBX (centimetres per unit), 1 when it is not found"""
    with open(path, "rb") as f:
        data = f.read()
    at = data.find(b"UnitScaleFactor")
    if at < 0:
        return 1.0
    at += len("UnitScaleFactor")
    # P: "UnitScaleFactor", "double", "Number", "", value : each a type letter and its payload
    for _ in range(6):
        if at >= len(data) - 9:
            break
        kind = data[at:at + 1]
        if kind == b"S":
            at += 5 + struct.unpack_from("<I", data, at + 1)[0]
        elif kind == b"D":
            return struct.unpack_from("<d", data, at + 1)[0]
        elif kind == b"F":
            return struct.unpack_from("<f", data, at + 1)[0]
        else:
            break
    return 1.0


class UnityProject:
    """The extracted Unity files : assets by guid, materials by name, textures by file name"""

    def __init__(self, root, files):
        self.root = root
        self.by_guid = {}
        self.materials = {}     # lower name -> [paths]
        self.textures = {}      # lower base name -> [paths]
        self.parsed = {}
        present = set(files)
        for path in files:
            lower = path.lower()
            if lower.endswith(".meta"):
                asset = path[:-5]
                if asset in present:
                    with open(os.path.join(root, path), "r", encoding="utf-8", errors="replace") as f:
                        head = f.read(400)
                    if m := re.search(r"^guid:\s*([0-9a-f]{32})", head, re.M):
                        self.by_guid[m.group(1)] = asset
            elif lower.endswith(".mat"):
                self.materials.setdefault(os.path.splitext(os.path.basename(lower))[0], []).append(path)
            elif lower.endswith(TEXTURE_EXTENSIONS):
                self.textures.setdefault(os.path.splitext(os.path.basename(lower))[0], []).append(path)

    @staticmethod
    def nearest(candidates, near):
        """The candidate sharing the longest folder prefix with `near`"""
        parts = near.split("/")
        def score(path):
            n = 0
            for a, b in zip(path.split("/"), parts):
                if a != b:
                    break
                n += 1
            return (-n, len(path), path)
        return min(candidates, key=score) if candidates else None

    def material(self, path):
        if path in self.parsed:
            return self.parsed[path]
        text = read_text(os.path.join(self.root, path))
        out = { "path": path, "textures": {}, "colors": {}, "floats": {} }
        for m in re.finditer(r"^\s*- (\w+):\s*\n\s*m_Texture: \{fileID: \d+(?:, guid: ([0-9a-f]{32}))?", text, re.M):
            if m.group(2) and m.group(2) in self.by_guid:
                out["textures"][m.group(1)] = self.by_guid[m.group(2)]
        for m in re.finditer(r"^\s*- (\w+): \{r: ([0-9.eE+-]+), g: ([0-9.eE+-]+), b: ([0-9.eE+-]+), a: ([0-9.eE+-]+)\}", text, re.M):
            out["colors"][m.group(1)] = [float(m.group(i)) for i in range(2, 6)]
        for m in re.finditer(r"^\s*- (\w+): ([0-9.eE+-]+)\s*$", text, re.M):
            out["floats"][m.group(1)] = float(m.group(2))
        floats = out["floats"]
        keywords = text
        cutout = "_ALPHATEST_ON" in keywords or floats.get("_AlphaClip", 0.0) > 0.5 or "RenderType: TransparentCutout" in text
        blend = floats.get("_Surface", 0.0) > 0.5 or "RenderType: Transparent\n" in text
        if "_AlphaClip" not in floats and "_Surface" not in floats and floats.get("_Mode", 0.0) > 0.5:
            cutout = True   # the built-in Standard shader : cutout, fade or transparent
        out["alpha_cutoff"] = float(np.clip(floats.get("_Cutoff", 0.5), 0.05, 0.9)) if cutout else (0.3 if blend else 0.0)
        out["two_sided"] = floats.get("_Cull", 2.0) < 0.5 or floats.get("_CullMode", 2.0) < 0.5
        self.parsed[path] = out
        return out

    def find_texture(self, file_name, near):
        if not file_name:
            return None
        base = os.path.splitext(os.path.basename(file_name.replace("\\", "/")))[0].lower()
        return self.nearest(self.textures.get(base, []), near)


# ---------------------------------------------------------------------------------------------------------------------
# One FBX file, as Blender dumped it

class Fbx:
    def __init__(self, paths, dump, src_root):
        self.paths = paths
        self.path = paths[0]
        self.dump = dump
        with open(dump + ".json", "r", encoding="utf-8") as f:
            self.info = json.load(f)
        self.error = self.info.get("error")
        self.folder, name = os.path.split(self.path)
        self.stem = os.path.splitext(name)[0]
        self.clip_name = self.stem.split("@", 1)[1] if "@" in self.stem else None
        tokens = [t for t in re.split(r"[^a-z0-9]+", self.stem.split("@")[0].lower()) if t]
        kept = [t for t in tokens if not re.fullmatch(NAME_NOISE, t)]
        self.animal = "_".join(kept or tokens)
        self.root_motion_file = "rm" in tokens or "rm_animations" in self.folder.lower()
        # Animations/, IP_animations/, RM_animations/ belong to the folder above
        self.group_folder = os.path.dirname(self.folder) if re.fullmatch(r"((ip|rm)_)?animations?", os.path.basename(self.folder).lower()) else self.folder
        self._arrays = None
        if self.error:
            self.joints, self.meshes, self.takes, self.helpers = [], [], [], []
            return

        full = os.path.join(src_root, self.path)
        self.meta = parse_model_meta(read_text(full + ".meta") if os.path.isfile(full + ".meta") else "")
        # Unity's size : Blender applied the file's unit, Unity only does when asked to
        self.scale = self.meta["global_scale"]
        if not self.meta["use_file_scale"]:
            self.scale *= 100.0 / max(fbx_unit_scale(full), 1e-6)

        self.joints = self.info["joints"]
        for joint in self.joints:
            if not joint["armature"]:
                joint["parent"] = joint.get("attached_to", -1)
        self.bone_names = { j["name"] for j in self.joints if j["armature"] }
        self.joint_index = {}
        for index, joint in enumerate(self.joints):
            self.joint_index.setdefault(joint["name"], index)

        skinned = any(m["skinned"] for m in self.info["meshes"])
        self.meshes, self.helpers = [], []
        for index, mesh in enumerate(self.info["meshes"]):
            mesh["index"] = index
            lower = mesh["name"].lower()
            helper = False
            if not mesh["skinned"]:
                # Shapes of the rig's controls and bones drawn as boxes, exported with the animal
                helper = re.search(r"(^|[_ ])ctrl($|[_ .])", lower) is not None or (skinned and (not mesh["has_uv"] or mesh["vertices"] <= 64))
            if re.fullmatch(r"shadows?(\.\d+)?", lower):
                helper = True   # a quad under the animal, for a blob shadow
            (self.helpers if helper else self.meshes).append(mesh)
        self.takes = [t for t in self.info["takes"] if t["frames"] > 2]

    @property
    def arrays(self):
        if self._arrays is None:
            self._arrays = np.load(self.dump + ".npz")
        return self._arrays

    def release(self):
        self._arrays = None

    def linear(self):
        """Blender's world to the engine's, at Unity's size"""
        return TO_ENGINE * self.scale

    def rest(self, name):
        m = np.array(self.joints[self.joint_index[name]]["rest"])
        A = self.linear()
        return to44(np.concatenate([A @ m[:, :3], (A @ m[:, 3])[:, None]], axis=1))

    def take_world(self, take):
        """(frames, joints, 4, 4) in the engine's space"""
        W = self.arrays["take%d_world" % take["index"]].astype(np.float64)
        A = self.linear()
        return to44(np.concatenate([np.einsum("ab,fjbc->fjac", A, W[..., :3]), np.einsum("ab,fjb->fja", A, W[..., 3])[..., None]], axis=3))

    def mesh_signature(self, mesh):
        position = self.arrays["mesh%d_position" % mesh["index"]]
        return (mesh["vertices"], mesh["triangles"], tuple(np.round(position.min(0) * self.scale, 3)), tuple(np.round(position.max(0) * self.scale, 3)))

    def clip_sources(self):
        """[(name, take, first index, last index, loop or None)] : Unity's clips when the .meta has some, else the takes"""
        if not self.takes:
            return []
        by_name = { t["name"]: t for t in self.takes }
        out = []
        if self.meta["clips"]:
            for clip in self.meta["clips"]:
                take = by_name.get(clip["take"]) or (self.takes[0] if len(self.takes) == 1 or not clip["take"] else None)
                if take is None:
                    continue
                first = min(max(clip["first"] - take["first_frame"], 0), take["frames"] - 1)
                last = min(max(clip["last"] - take["first_frame"], 0), take["frames"] - 1)
                if last <= first:
                    continue
                name = clip["name"].split("|")[-1].strip()
                if self.clip_name and len(self.meta["clips"]) == 1:
                    name = self.clip_name
                out.append((name or self.stem, take, first, last, clip["loop"]))
            if out:
                return out
        for take in self.takes:
            name = take["name"].split("|")[-1].strip()
            if len(self.takes) == 1 and self.clip_name:
                name = self.clip_name
            elif len(self.takes) == 1 and name.lower() in GENERIC_TAKES:
                name = self.stem
            out.append((name, take, 0, take["frames"] - 1, None))
        return out


def skeletons_match(a, b):
    """Same bones by name, for the most part"""
    if not a.bone_names or not b.bone_names:
        return not a.bone_names and not b.bone_names
    common = len(a.bone_names & b.bone_names)
    return common >= 0.8 * len(a.bone_names) and common >= 0.8 * len(b.bone_names)


def clips_fit(model, anim):
    """A file without a mesh animates `model` : its bones are the model's, and played on the model's bind pose they
    stay attached to their parents (two skeletons of the same rig at different sizes do not)"""
    common = model.bone_names & anim.bone_names
    if not common or len(common) < 0.8 * len(model.bone_names) or not anim.takes:
        return False
    take = anim.takes[0]
    W = anim.take_world(take)
    errors = []
    for joint in model.joints:
        parent = joint["parent"]
        if parent < 0 or joint["name"] not in common or model.joints[parent]["name"] not in common:
            continue
        bind, bind_parent = model.rest(joint["name"]), model.rest(model.joints[parent]["name"])
        length = np.linalg.norm(bind[:3, 3] - bind_parent[:3, 3])
        if length < 1e-5:
            continue
        for f in (0, W.shape[0] // 2):
            skin_parent = W[f, anim.joint_index[model.joints[parent]["name"]]] @ np.linalg.inv(bind_parent)
            errors.append(np.linalg.norm((skin_parent @ bind[:, 3])[:3] - W[f, anim.joint_index[joint["name"]], :3, 3]) / length)
    return bool(errors) and float(np.median(errors)) < 0.05


# ---------------------------------------------------------------------------------------------------------------------
# Animals

class Animal:
    def __init__(self, files):
        self.files = files          # with meshes, the most detailed first
        self.extra = []             # files without meshes that animate it
        self.borrowed = None        # the animal whose clips it plays
        self.id = None

    @property
    def reference(self):
        return self.files[0]

    def clip_files(self):
        if self.borrowed is not None:
            return self.borrowed.clip_files()
        return [f for f in self.files if f.takes] + self.extra


def in_scope(a_folder, b_folder):
    """Folders of the same pack : one inside the other, or next to each other"""
    a, b = a_folder.split("/"), b_folder.split("/")
    common = 0
    for x, y in zip(a, b):
        if x != y:
            break
        common += 1
    # The folder under Faune/ (or Realistic/) is the pack
    pack = next((i for i, part in enumerate(a) if part in ("Faune", "Realistic")), 2) + 2
    return common >= pack and common >= min(len(a), len(b)) - 1


def find_animals(fbx_files, log):
    with_meshes = [f for f in fbx_files if f.meshes]
    without = [f for f in fbx_files if not f.meshes and f.takes]

    groups = {}
    for f in with_meshes:
        groups.setdefault((f.group_folder, f.animal), []).append(f)

    animals = []
    for (folder, name), files in sorted(groups.items()):
        # Same name, different rigs : different animals (Buffalo.FBX and the buffalo@ files)
        clusters = []
        for f in files:
            for cluster in clusters:
                if skeletons_match(cluster[0], f):
                    cluster.append(f)
                    break
            else:
                clusters.append([f])
        for cluster in clusters:
            cluster.sort(key=lambda f: (-max(m["triangles"] for m in f.meshes), "@" in f.stem, f.root_motion_file, len(f.path), f.path))
            animals.append(Animal(cluster))

    unused = []
    for f in without:
        fits = [a for a in animals if in_scope(a.reference.group_folder, f.group_folder) and clips_fit(a.reference, f)]
        for a in fits:
            a.extra.append(f)
        if not fits:
            unused.append(f)
        f.release()

    for a in animals:
        if not a.clip_files():
            donors = [b for b in animals if b is not a and b.clip_files() and b.borrowed is None
                      and in_scope(a.reference.group_folder, b.reference.group_folder) and a.reference.bone_names == b.reference.bone_names
                      and len(a.reference.bone_names) > 0]
            if donors:
                a.borrowed = min(donors, key=lambda b: (b.reference.group_folder != a.reference.group_folder, b.reference.path))

    # An animal whose meshes are all another's, with nothing of its own, is that animal exported twice
    signatures = { id(a): { f.mesh_signature(m) for f in a.files for m in f.meshes } for a in animals }
    kept = []
    for a in animals:
        twin = None
        if a.borrowed is not None and signatures[id(a)] <= signatures[id(a.borrowed)]:
            twin = a.borrowed
        if twin is not None:
            log.append("%s : same meshes as %s, not a separate animal" % (a.reference.path, twin.reference.path))
        else:
            kept.append(a)
    for f in fbx_files:
        f.release()

    # Ids : the name of the animal, with the pack when two packs have one
    used = {}
    for a in sorted(kept, key=lambda a: a.reference.path):
        base = slug(a.reference.animal) or "animal"
        if clip_kind(base) != "other":
            # A file named after its clip (Walking.fbx) : the pack names the animal
            parts = a.reference.path.split("/")
            base = slug(parts[3] if len(parts) > 4 else parts[-2]) + "_" + base
        candidate = base
        if candidate in used:
            tag = next((t for word, t in PACK_TAGS if word in a.reference.path), None)
            if tag:
                candidate = base + "_" + tag
        n = 2
        while candidate in used:
            candidate = "%s_%d" % (base, n)
            n += 1
        used[candidate] = a
        a.id = candidate
    return kept, unused


# ---------------------------------------------------------------------------------------------------------------------
# Geometry

def quaternions(R):
    """Rotation matrices (..., 3, 3) to quaternions (..., 4) as x, y, z, w"""
    m00, m01, m02 = R[..., 0, 0], R[..., 0, 1], R[..., 0, 2]
    m10, m11, m12 = R[..., 1, 0], R[..., 1, 1], R[..., 1, 2]
    m20, m21, m22 = R[..., 2, 0], R[..., 2, 1], R[..., 2, 2]
    q = np.stack([
        np.stack([1.0 + m00 - m11 - m22, m01 + m10, m02 + m20, m21 - m12], -1),
        np.stack([m01 + m10, 1.0 - m00 + m11 - m22, m12 + m21, m02 - m20], -1),
        np.stack([m02 + m20, m12 + m21, 1.0 - m00 - m11 + m22, m10 - m01], -1),
        np.stack([m21 - m12, m02 - m20, m10 - m01, 1.0 + m00 + m11 + m22], -1),
    ], -2)
    best = np.argmax(np.stack([q[..., 0, 0], q[..., 1, 1], q[..., 2, 2], q[..., 3, 3]], -1), -1)
    out = np.take_along_axis(q, best[..., None, None], -2)[..., 0, :]
    return out / np.maximum(np.linalg.norm(out, axis=-1, keepdims=True), 1e-12)


def decompose(local):
    """(..., 4, 4) to translation (..., 3), rotation (..., 4) and scale (..., 3)"""
    L = local[..., :3, :3]
    scale = np.linalg.norm(L, axis=-2)
    flip = np.linalg.det(L) < 0.0
    scale[..., 0] = np.where(flip, -scale[..., 0], scale[..., 0])
    R = L / np.where(np.abs(scale) > 1e-12, scale, 1.0)[..., None, :]
    return local[..., :3, 3].copy(), quaternions(R), scale


def compose(t, q, s):
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    R = np.stack([
        np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], -1),
        np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], -1),
        np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], -1),
    ], -2)
    out = np.zeros(t.shape[:-1] + (4, 4))
    out[..., :3, :3] = R * s[..., None, :]
    out[..., :3, 3] = t
    out[..., 3, 3] = 1.0
    return out


def lod_of(name):
    m = re.search(r"lod[_ ]?(\d)", name.lower())
    return int(m.group(1)) if m else None


def box_overlap(a_min, a_max, b_min, b_max):
    inter = np.prod(np.maximum(np.minimum(a_max, b_max) - np.maximum(a_min, b_min), 0.0))
    union = np.prod(a_max - a_min) + np.prod(b_max - b_min) - inter
    return inter / union if union > 0 else 0.0


def clip_kind(name):
    n = "_" + re.sub(r"[^a-z0-9]+", "_", name.lower()) + "_"
    if re.search(r"turn|left|right|back|strafe|rot|attack|bite|jump|hit|death|die|dead|swim|fly|glide|land|take_?off|fall|hunted|crawl|damage|stop|start|_(b|bl|br|l|r)_", n):
        return "other"
    if "walk" in n:
        return "eat" if re.search(r"graz|eat", n) else "walk"
    if "trot" in n:
        return "trot"
    if re.search(r"run|gallop|sprint", n):
        return "run"
    if re.search(r"eat|graz|drink|chew|peck|chrom", n):
        return "eat"
    if re.search(r"sleep|rest|lie|lay|sit", n):
        return "rest"
    if "hop" in n:
        return "walk"
    if re.search(r"idle|breath|stand|look|sniff", n):
        return "idle"
    return "other"


def gait_rank(name):
    """The plain gait first (Walk, Walk_forward_IP, Walk_F_IP), its variants after (Walk Smell, walk_fast)"""
    n = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    n = re.sub(r"_(ip|rm|mec)$", "", n)
    plain = n in ("walk", "walking", "walk_forward", "walk_f", "trot", "trot_forward", "trot_f", "run", "runing", "running", "run_forward", "run_f", "gallop", "galloping")
    return (not plain, "hop" in n, len(n), n)


def similarity(source, target):
    """Scale, rotation and translation that best take the points `source` to `target` (Umeyama)"""
    mean_s, mean_t = source.mean(0), target.mean(0)
    a, b = source - mean_s, target - mean_t
    U, D, Vt = np.linalg.svd(b.T @ a / len(source))
    S = np.eye(3)
    if np.linalg.det(U) * np.linalg.det(Vt) < 0:
        S[2, 2] = -1.0
    R = U @ S @ Vt
    variance = (a ** 2).sum() / len(source)
    scale = float(np.trace(np.diag(D) @ S) / variance) if variance > 0 else 1.0
    return scale, R, mean_t - scale * R @ mean_s


AXIS_TURNS = None

def axis_turns():
    """The 24 rotations that take axes to axes"""
    global AXIS_TURNS
    if AXIS_TURNS is None:
        AXIS_TURNS = []
        for x in range(3):
            for y in range(3):
                if x == y:
                    continue
                for sx in (1, -1):
                    for sy in (1, -1):
                        R = np.zeros((3, 3))
                        R[x, 0], R[y, 1] = sx, sy
                        R[:, 2] = np.cross(R[:, 0], R[:, 1])
                        AXIS_TURNS.append(R)
    return AXIS_TURNS


def rotation_angle(R):
    return float(np.degrees(np.arccos(np.clip((np.trace(R) - 1.0) / 2.0, -1.0, 1.0))))


class Builder:
    def __init__(self, animal, project, textures, src_root, out_dir, log):
        self.animal = animal
        self.project = project
        self.textures = textures
        self.src_root = src_root
        self.out_dir = out_dir
        self.log = log
        self.ref = animal.reference
        self.turn = np.eye(4)
        self.materials, self.material_index = [], {}

    def note(self, text):
        line = "%s : %s" % (self.animal.id, text)
        if line not in self.log:
            self.log.append(line)

    # Skeleton : the reference file's joints, parents first
    def skeleton(self):
        ref = self.ref
        kept_rigid = { m["name"] for f in self.animal.files for m in f.meshes if not m["skinned"] }
        source = [j for j in ref.joints if j["armature"] or j["name"] in kept_rigid]
        index_of = { id(j): i for i, j in enumerate(source) }

        order, placed = [], set()
        def place(joint):
            if id(joint) in placed:
                return
            if joint["parent"] >= 0 and id(ref.joints[joint["parent"]]) in index_of:
                place(ref.joints[joint["parent"]])
            placed.add(id(joint))
            order.append(joint)
        for joint in source:
            place(joint)

        self.names = [j["name"] for j in order]
        self.index = {}
        for i, name in enumerate(self.names):
            self.index.setdefault(name, i)
        self.parents = [self.index[ref.joints[j["parent"]]["name"]] if j["parent"] >= 0 and id(ref.joints[j["parent"]]) in index_of else -1 for j in order]
        if not order:
            # A mesh without a rig still needs something to hang on
            self.names, self.parents, self.index = ["root"], [-1], { "root": 0 }
            self.rest = np.eye(4)[None]
        else:
            self.rest = np.stack([ref.rest(name) for name in self.names])
        self.derive()

    def derive(self):
        self.inverse_bind = np.linalg.inv(self.rest)
        self.rest_local = np.stack([self.rest[i] if p < 0 else np.linalg.inv(self.rest[p]) @ self.rest[i] for i, p in enumerate(self.parents)])

    def worlds(self, file, W):
        """World matrices of a file's joints (frames, file joints, 4, 4) to those of this skeleton's joints. Joints
        the file does not have follow their parent as in the bind pose"""
        W = self.turn @ W
        world = np.empty((W.shape[0], len(self.names), 4, 4))
        for i, name in enumerate(self.names):
            k = file.joint_index.get(name)
            if k is not None:
                world[:, i] = W[:, k]
            elif self.parents[i] >= 0:
                world[:, i] = world[:, self.parents[i]] @ self.rest_local[i]
            else:
                world[:, i] = self.rest[i]
        return world

    def locals(self, world):
        local = np.empty_like(world)
        for i, p in enumerate(self.parents):
            local[:, i] = world[:, i] if p < 0 else np.linalg.inv(world[:, p]) @ world[:, i]
        return local

    # A mesh of an FBX can be bound in another space than the armature Blender made from the first mesh (the low
    # levels of the wolf are 100 times larger and lying down) : such a mesh is brought back on the skeleton
    def bind_fixes(self, f):
        if hasattr(f, "bind_fix"):
            return f.bind_fix
        f.bind_fix = {}
        arrays = f.arrays
        A = f.linear()
        centroids, fits = {}, {}
        for mesh in f.meshes:
            if not mesh["skinned"]:
                continue
            key = "mesh%d_" % mesh["index"]
            position = arrays[key + "position"].astype(np.float64) @ A.T
            joints, weights = arrays[key + "weight_joint"], arrays[key + "weight"].astype(np.float64)
            total = np.zeros(len(f.joints))
            summed = np.zeros((len(f.joints), 3))
            for k in range(4):
                np.add.at(total, joints[:, k], weights[:, k])
                np.add.at(summed, joints[:, k], weights[:, k, None] * position)
            used = np.nonzero(total > 1e-6)[0]
            if len(used) < 4:
                continue
            centroids[mesh["index"]] = { f.joints[k]["name"]: summed[k] / total[k] for k in used }
            targets = np.stack([f.rest(f.joints[k]["name"])[:3, 3] for k in used])
            scale, R, t = similarity(np.stack([summed[k] / total[k] for k in used]), targets)
            fits[mesh["index"]] = 0.6 < scale < 1.6 and rotation_angle(R) < 30.0

        good = [index for index, ok in fits.items() if ok]
        for index, ok in fits.items():
            if ok:
                continue
            mesh = next(m for m in f.meshes if m["index"] == index)
            source = centroids[index]
            reference = max(good, key=lambda g: len(set(centroids[g]) & set(source)), default=None)
            if reference is not None and len(set(centroids[reference]) & set(source)) >= 4:
                common = sorted(set(centroids[reference]) & set(source))
                target = np.stack([centroids[reference][n] for n in common])
            else:
                common = sorted(source)
                target = np.stack([f.rest(n)[:3, 3] for n in common])
            points = np.stack([source[n] for n in common])
            scale, R, t = similarity(points, target)
            # The true change is a turn of axes and a change of unit : take the nearest one when it is close
            nearest = min(axis_turns(), key=lambda turn: rotation_angle(turn.T @ R))
            if rotation_angle(nearest.T @ R) < 8.0:
                R = nearest
            for unit in (0.001, 0.01, 0.0254, 0.1, 0.3048, 1.0, 10.0, 39.3701, 100.0, 1000.0):
                if abs(scale / unit - 1.0) < 0.06:
                    scale = unit
            t = (target - scale * points @ R.T).mean(0)
            if np.linalg.norm(t) < 0.02 * np.linalg.norm(target.max(0) - target.min(0)):
                t = np.zeros(3)
            fix = np.eye(4)
            fix[:3, :3] = scale * R
            fix[:3, 3] = t
            f.bind_fix[index] = fix
            self.note("mesh %s is bound in another space than the skeleton (%.4g times its size, turned %.0f degrees) : brought back on it" % (
                mesh["name"], 1.0 / scale, rotation_angle(R)))
        return f.bind_fix

    # Meshes : every different one of the animal's files, in levels of detail. Positions and normals stay as floats
    # until the animal is turned to face -Z
    def geometry(self):
        seen = set()
        parts = []
        for f in self.animal.files:
            for mesh in f.meshes:
                signature = (mesh["name"].lower(), mesh["vertices"], mesh["triangles"])
                if signature in seen:
                    continue
                seen.add(signature)
                part = self.convert_mesh(f, mesh)
                if part is not None:
                    parts.append(part)
        if not parts:
            raise ValueError("no mesh with triangles")

        named = [p for p in parts if p["lod"] is not None]
        if named:
            levels = sorted({ p["lod"] for p in named })
            for p in parts:
                p["levels"] = [levels.index(p["lod"])] if p["lod"] is not None else list(range(len(levels)))
        else:
            # Bodies of different detail fill the same box, parts (teeth, eyes) fill a corner of it
            body = max(parts, key=lambda p: float(np.prod(p["max"] - p["min"])))
            bodies = sorted([p for p in parts if box_overlap(p["min"], p["max"], body["min"], body["max"]) >= 0.6], key=lambda p: -p["triangles"])
            levels = list(range(len(bodies)))
            for p in parts:
                p["levels"] = [next(i for i, b in enumerate(bodies) if b is p)] if any(b is p for b in bodies) else levels
        self.parts = parts
        self.level_count = len(levels)

    def convert_mesh(self, f, mesh):
        arrays = f.arrays
        key = "mesh%d_" % mesh["index"]
        position = arrays[key + "position"].astype(np.float64) @ f.linear().T
        loop_normal = arrays[key + "normal"].astype(np.float64) @ TO_ENGINE.T
        loop_uv = arrays[key + "uv"].astype(np.float64)
        loop_vertex = arrays[key + "loop_vertex"]
        triangles = arrays[key + "triangles"]
        tri_material = arrays[key + "tri_material"]
        if len(triangles) == 0:
            return None

        fix = self.bind_fixes(f).get(mesh["index"])
        if fix is not None:
            position = position @ fix[:3, :3].T + fix[:3, 3]
            loop_normal = loop_normal @ (fix[:3, :3] / np.cbrt(abs(np.linalg.det(fix[:3, :3])))).T

        # Joints of this file to the animal's; the vertices move to the animal's bind pose when the file has another
        joint_map = np.zeros(len(f.joints), dtype=np.int64)
        missing = 0
        moved = False
        relocate = np.tile(np.eye(4), (len(f.joints), 1, 1))
        for k, joint in enumerate(f.joints):
            i = self.index.get(joint["name"])
            if i is None:
                missing += 1
                continue
            joint_map[k] = i
            if f is not self.ref:
                relocate[k] = self.rest[i] @ np.linalg.inv(f.rest(joint["name"]))
                moved = moved or not np.allclose(relocate[k], np.eye(4), atol=1e-4)
        weight_joint = arrays[key + "weight_joint"].astype(np.int64)
        weight = arrays[key + "weight"].astype(np.float64)
        total = weight.sum(1)
        unweighted = int((total <= 0).sum())
        weight[total <= 0, 0] = 1.0     # on the first joint
        weight /= weight.sum(1, keepdims=True)
        lost = int((weight * (np.array([self.index.get(j["name"]) is None for j in f.joints])[weight_joint])).sum(1).astype(bool).sum()) if missing else 0
        if moved:
            homogeneous = np.concatenate([position, np.ones((len(position), 1))], axis=1)
            position = np.einsum("vk,vkab,vb->va", weight, relocate[weight_joint], homogeneous)[:, :3]
            rotations = relocate[:, :3, :3] / np.cbrt(np.abs(np.linalg.det(relocate[:, :3, :3])))[:, None, None]
            loop_normal = np.einsum("vk,vkab,vb->va", weight[loop_vertex], rotations[weight_joint[loop_vertex]], loop_normal)
        weight_joint = joint_map[weight_joint]

        # One vertex per position, normal and uv
        used = np.unique(triangles.reshape(-1))
        rows = np.concatenate([loop_vertex[used, None].astype(np.float64), np.round(loop_normal[used] * 512.0), np.round(loop_uv[used] * 16384.0)], axis=1)
        unique, first, inverse = np.unique(rows, axis=0, return_index=True, return_inverse=True)
        remap = np.zeros(len(loop_vertex), dtype=np.int64)
        remap[used] = inverse.reshape(-1)
        source_loop = used[first]
        source_vertex = loop_vertex[source_loop]

        w = weight[source_vertex]
        quantized = np.floor(w * 255.0).astype(np.int64)
        quantized[np.arange(len(w)), np.argmax(w, axis=1)] += 255 - quantized.sum(1)
        joints = np.where(quantized > 0, weight_joint[source_vertex], 0)
        p = position[source_vertex]
        normal = loop_normal[source_loop]
        normal /= np.maximum(np.linalg.norm(normal, axis=1, keepdims=True), 1e-12)
        uv = np.stack([loop_uv[source_loop, 0], 1.0 - loop_uv[source_loop, 1]], axis=1)

        tris = remap[triangles]
        geometric = np.cross(p[tris[:, 1]] - p[tris[:, 0]], p[tris[:, 2]] - p[tris[:, 0]])
        shading = normal[tris[:, 0]] + normal[tris[:, 1]] + normal[tris[:, 2]]
        if np.mean(np.einsum("ij,ij->i", geometric, shading) > 0.0) < 0.5:
            tris = tris[:, ::-1]

        draws = []
        order = np.argsort(tri_material, kind="stable")
        tris, tri_material = tris[order], tri_material[order]
        for slot in np.unique(tri_material):
            count = int((tri_material == slot).sum())
            first_triangle = int(np.argmax(tri_material == slot))
            info = mesh["materials"][slot] if slot < len(mesh["materials"]) else None
            draws.append({ "material": self.material(f, info, mesh), "first": first_triangle * 3, "count": count * 3 })

        if lost or unweighted:
            self.note("mesh %s has %d vertices without weights, %d on joints the skeleton does not have" % (mesh["name"], unweighted, lost))
        return { "name": mesh["name"], "file": f.path, "position": p, "normal": normal, "uv": uv, "joints": joints, "weights": quantized,
                 "indices": tris.reshape(-1).astype(np.uint32), "draws": draws, "lod": lod_of(mesh["name"]), "triangles": len(tris),
                 "min": p.min(0), "max": p.max(0) }

    # Materials : Unity's .mat (the link of the .meta, else its name, else the nearest one named alike), then the
    # textures the FBX names, then the pack's color texture named most like the material
    def material(self, f, info, mesh):
        name = info["name"] if info else "default"
        if name in self.material_index:
            return self.material_index[name]
        project = self.project
        out = { "name": name, "color": None, "normal": None, "tint": [1.0, 1.0, 1.0, 1.0], "alpha_cutoff": 0.0, "two_sided": False, "source": None }
        base = re.sub(r"\.\d{3}$", "", name).lower()
        pack = f.group_folder

        def color_of(mat):
            for prop in ("_BaseMap", "_MainTex", "_BaseColorMap", "_Albedo", "_Diffuse", "_DiffuseMap", "_AlbedoMap", "_FurTex", "_Color"):
                if prop in mat["textures"]:
                    return mat["textures"][prop]
            return next((path for prop, path in sorted(mat["textures"].items()) if re.search(r"main|base|albedo|diff|col", prop.lower())), None)

        def normal_of(mat):
            for prop in ("_BumpMap", "_NormalMap", "_Normal"):
                if prop in mat["textures"]:
                    return mat["textures"][prop]
            return None

        def likeness(candidate):
            import difflib
            stem = os.path.splitext(os.path.basename(candidate))[0].lower()
            score = max(difflib.SequenceMatcher(None, stem, base).ratio(), 0.8 * difflib.SequenceMatcher(None, stem, self.ref.animal).ratio())
            penalty = 0.15 * bool(re.search(r"legacy|mobile|dirt|black|dark|arctic|white|4k|snow", stem)) - 0.05 * bool(re.search(r"2k", stem))
            return (-(score - penalty), len(stem), candidate)

        candidates = []
        linked = project.by_guid.get(f.meta["materials"].get(name, ""))
        if linked and linked.lower().endswith(".mat"):
            candidates.append(linked)
        named = project.nearest(project.materials.get(base, []), f.path)
        if named:
            candidates.append(named)
        around = sorted((path for paths in project.materials.values() for path in paths if in_scope(os.path.dirname(path), pack) or path.startswith(pack + "/")), key=likeness)
        chosen = next((c for c in candidates if color_of(project.material(c))), None)
        exact = chosen is not None
        if chosen is None:
            chosen = next((c for c in around if color_of(project.material(c))), None)
        color = normal = None
        if chosen:
            mat = project.material(chosen)
            out["source"] = chosen
            color, normal = color_of(mat), normal_of(mat)
            tint = mat["colors"].get("_BaseColor") or mat["colors"].get("_Color")
            if tint:
                out["tint"] = [round(float(np.clip(c, 0.0, 1.0)), 4) for c in tint[:3]] + [1.0]
            out["alpha_cutoff"] = mat["alpha_cutoff"]
            out["two_sided"] = mat["two_sided"]
        if info and (color is None or not exact):
            own = project.find_texture(info.get("color"), f.path)
            if own is None:
                for image in info.get("images", []):
                    if re.search(r"color|albedo|diffuse|base|_d\b|_col", os.path.basename(image).lower()):
                        own = own or project.find_texture(image, f.path)
            if own is not None:
                color = own
                normal = project.find_texture(info.get("normal"), f.path) or normal
                out["source"] = "textures named in the FBX"
        if color is None:
            textures = sorted((path for paths in project.textures.values() for path in paths
                               if (in_scope(os.path.dirname(path), pack) or path.startswith(pack + "/"))
                               and re.search(r"color|albedo|diffuse|basecolor|_dif|_d\d|_d$|diff", os.path.splitext(os.path.basename(path))[0].lower())), key=likeness)
            if textures:
                color = textures[0]
                out["source"] = "the pack's color texture"
        if color is None and info and info.get("base_color") and not chosen:
            out["tint"] = [round(float(np.clip(c, 0.0, 1.0)), 4) for c in info["base_color"][:3]] + [1.0]
        if color is None and max(out["tint"][:3]) < 0.08:
            # Black is the color of a material whose texture was lost, not of the animal
            out["tint"] = [0.6, 0.55, 0.5, 1.0]
        if color and color.lower().endswith(TEXTURE_EXTENSIONS):
            out["color"] = self.textures.get(color, "color")
        if normal and normal.lower().endswith(TEXTURE_EXTENSIONS):
            out["normal"] = self.textures.get(normal, "normal")
        if out["color"] is None:
            out["alpha_cutoff"] = 0.0
            self.note("no color texture for material %s" % name)
        elif not exact:
            self.note("material %s : %s (%s)" % (name, os.path.basename(color), out["source"]))
        self.material_index[name] = len(self.materials)
        self.materials.append(out)
        return self.material_index[name]

    # The animal's skin on some frames of a clip : a few hundred vertices of its lightest body
    def sample_skin(self, world):
        body = min((p for p in self.parts if len(p["position"]) >= 50), key=lambda p: len(p["position"]), default=self.parts[0])
        step = max(len(body["position"]) // 600, 1)
        position = np.concatenate([body["position"][::step], np.ones((len(body["position"][::step]), 1))], axis=1)
        joints, weights = body["joints"][::step], body["weights"][::step] / 255.0
        skin = world @ self.inverse_bind
        return np.einsum("vk,fvkab,vb->fva", weights, skin[:, joints], position)[..., :3]

    def ground_slide(self, world, fps):
        """Velocity (x, z) of the parts of the skin that are on the ground : the feet of a gait played in place"""
        frames = world.shape[0]
        if frames < 4:
            return np.zeros(2)
        skin = self.sample_skin(world)
        ground = np.percentile(skin[..., 1], 0.5)
        height = max(np.percentile(skin[..., 1], 99.5) - ground, 1e-6)
        slides = []
        for frame in range(frames - 1):
            down = (skin[frame, :, 1] < ground + 0.06 * height) & (skin[frame + 1, :, 1] < ground + 0.06 * height)
            if down.sum() >= 3:
                slides.append(np.median((skin[frame + 1, down] - skin[frame, down])[:, [0, 2]], axis=0) * fps)
        return np.mean(slides, axis=0) if len(slides) >= frames // 3 else np.zeros(2)

    def travel_of(self, world):
        """What the whole body travels over a clip, and the joint that carries it (-1 : none, a change of pose)"""
        moved = world[-1, :, :3, 3] - world[0, :, :3, 3]
        travel = np.median(moved, axis=0)
        if np.hypot(travel[0], travel[2]) < 0.15 * self.size:
            return np.zeros(3), -1
        for j in range(len(self.names)):
            if np.linalg.norm(moved[j] - travel) < 0.25 * np.linalg.norm(travel):
                return travel, j
        return np.zeros(3), -1

    # Which way the animal looks : where its walk takes it, else to its head. Turned by quarter turns to face -Z
    def face_forward(self):
        direction, reason = None, "unknown"
        sources = [(gait_rank(name), f, name, take, first, last) for f in self.animal.clip_files() for name, take, first, last, loop in f.clip_sources()
                   if clip_kind(name) in ("walk", "trot", "run")]
        for rank, f, name, take, first, last in sorted(sources, key=lambda s: (clip_kind(s[2]) != "walk", s[0]))[:3]:
            world = self.worlds(f, f.take_world(take)[first:last + 1])
            travel, joint = self.travel_of(world)
            if joint >= 0:
                direction, reason = travel[[0, 2]], "the travel of " + name
                break
            slide = self.ground_slide(world, f.info["fps"])
            if np.linalg.norm(slide) > 0.03 * self.size:
                direction, reason = -slide, "the feet of " + name
                break
        if direction is None:
            head = [i for i, n in enumerate(self.names) if re.search(r"head|eye|nose|mouth|whisker|jaw", n.lower()) and not re.search(r"nub|end|top|ctrl|ik|target", n.lower())]
            if head and len(self.names) > 2:
                to_head = self.rest[head[0], :3, 3] - self.rest[:, :3, 3].mean(0)
                if max(abs(to_head[0]), abs(to_head[2])) > 0.02 * self.size:
                    direction, reason = to_head[[0, 2]], "the joint " + self.names[head[0]]
        if direction is None:
            low, high = self.bounds()
            if high[0] - low[0] > 1.3 * (high[2] - low[2]) and high[0] - low[0] > high[1] - low[1] and len(self.names) > 1:
                direction, reason = np.array([1.0, 0.0]), "its length (head or tail first, not known)"
        self.facing = reason
        if direction is None:
            return
        x, z = direction
        quarter = (0 if z < 0 else 2) if abs(z) >= abs(x) else (1 if x < 0 else 3)
        c, s = [(1, 0), (0, -1), (-1, 0), (0, 1)][quarter]
        self.turn[:3, :3] = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)
        self.facing_in_file = ["-z", "-x", "+z", "+x"][quarter]
        if quarter:
            R = self.turn[:3, :3]
            self.rest = self.turn @ self.rest
            self.derive()
            for part in self.parts:
                part["position"] = part["position"] @ R.T
                part["normal"] = part["normal"] @ R.T
                part["min"], part["max"] = part["position"].min(0), part["position"].max(0)

    def joint_skin(self):
        counts = np.zeros(len(self.names))
        total = 0
        for part in (p for p in self.parts if 0 in p["levels"]):
            used = part["weights"] > 25
            np.add.at(counts, part["joints"][used], 1.0)
            total += len(part["position"])
        return counts / max(total, 1)

    def bounds(self):
        level0 = [p for p in self.parts if 0 in p["levels"]]
        return np.min([p["min"] for p in level0], axis=0), np.max([p["max"] for p in level0], axis=0)

    # Clips
    def clips(self):
        self.clip_list, self.clip_data = [], []
        by_name = {}
        offset = 0
        for f in self.animal.clip_files():
            for name, take, first, last, loop in f.clip_sources():
                world = self.worlds(f, f.take_world(take)[first:last + 1])
                frames = world.shape[0]
                probe = world[[0, frames // 2, -1]]

                # The same clip in another file of the animal (its other levels of detail), or the same clip with
                # the root moving (the RM files)
                final = name
                if final in by_name:
                    other = by_name[final]
                    if other["frames"] == frames and np.allclose(other["probe"], probe, atol=1e-3 * max(self.size, 0.1)):
                        continue
                    final = name + "_RM" if f.root_motion_file and name + "_RM" not in by_name else name
                    n = 2
                    while final in by_name:
                        final = "%s_%d" % (name, n)
                        n += 1

                t, q, s = decompose(self.locals(world))
                # The shortest way from a frame to the next
                for frame in range(1, frames):
                    flip = np.einsum("ja,ja->j", q[frame], q[frame - 1]) < 0.0
                    q[frame, flip] *= -1.0

                fps = f.info["fps"]
                duration = (frames - 1) / fps
                kind = clip_kind(final)
                travel, carrier = self.travel_of(world)
                still = world[:, :, :3, 3] - (np.linspace(0.0, 1.0, frames)[:, None, None] * travel if carrier >= 0 else 0.0)
                looping = bool(loop) if loop is not None else bool(np.abs(still[-1] - still[0]).max() < 0.02 * self.size and np.abs(still - still[0]).max() > 1e-4 * self.size)

                flags = np.zeros(len(self.names), dtype=np.uint8)
                chunks = []
                for j in range(len(self.names)):
                    animated_t = bool(np.abs(t[:, j] - t[0, j]).max() > 1e-4 * max(1.0, np.abs(t[0, j]).max()))
                    animated_r = bool(np.abs(q[:, j] - q[0, j]).max() > 2e-4)
                    animated_s = bool(np.abs(s[:, j] - s[0, j]).max() > 1e-4 * max(1e-3, np.abs(s[0, j]).max()))
                    flags[j] = animated_t | (animated_r << 1) | (animated_s << 2)
                    chunks.append((t[:, j] if animated_t else t[:1, j]).astype("<f4").tobytes())
                    chunks.append(np.clip(np.rint((q[:, j] if animated_r else q[:1, j]) * 32767.0), -32767, 32767).astype("<i2").tobytes())
                    chunks.append((s[:, j] if animated_s else s[:1, j]).astype("<f4").tobytes())
                data = flags.tobytes() + b"\0" * (-len(flags) % 4) + b"".join(chunks)

                speed = 0.0
                if carrier >= 0 and duration > 0:
                    speed = float(np.hypot(travel[0], travel[2])) / duration
                elif kind in ("walk", "trot", "run"):
                    # A planted foot goes back (+z) under an animal that walks toward -z
                    speed = max(float(self.ground_slide(world, fps)[1]), 0.0)
                clip = {
                    "name": final,
                    "source": f.path,
                    "frames": frames,
                    "fps": round(fps, 4),
                    "loop": looping,
                    "kind": kind,
                    "offset": offset,
                    "size": len(data),
                    # The joint that carries the animal over the clip and how far, in the joint's parent : the engine
                    # can take this out and move the animal itself
                    "motion_joint": carrier,
                    "root_motion": [round(float(x), 5) for x in (t[-1, carrier] - t[0, carrier])] if carrier >= 0 else [0.0, 0.0, 0.0],
                    "travel": [round(float(x), 5) for x in travel],
                    "speed": round(speed, 4),
                }
                by_name[final] = { "frames": frames, "probe": probe.copy(), "clip": clip }
                self.clip_list.append(clip)
                self.clip_data.append(data)
                offset += len(data)
            f.release()

        # A gait played in place moves as fast as its twin with the root moving
        moving = { re.sub(r"_?(rm|ip|mec)$", "", c["name"].lower()): c for c in self.clip_list if c["motion_joint"] >= 0 }
        for clip in self.clip_list:
            twin = moving.get(re.sub(r"_?(rm|ip|mec)$", "", clip["name"].lower()))
            if clip["motion_joint"] < 0 and clip["kind"] in ("walk", "trot", "run") and twin is not None and twin["frames"] == clip["frames"]:
                clip["speed"] = twin["speed"]

    def standing_pose(self):
        """World matrices of the joints as the animal stands in its first idle; the bind pose without one"""
        for kind in ("idle", "walk", "eat", "rest"):
            for f in self.animal.clip_files():
                for name, take, first, last, loop in f.clip_sources():
                    if clip_kind(name) == kind:
                        return self.worlds(f, f.take_world(take)[first:first + 1])[0]
        return self.rest

    def build(self):
        animal = self.animal

        # A scan a millimetre tall : nothing in the files gives its size
        extent = max((float(np.ptp(f.arrays["mesh%d_position" % m["index"]], axis=0).max()) * f.scale for f in animal.files for m in f.meshes), default=0.0)
        if 0.0 < extent < 0.05 and not animal.clip_files():
            for f in animal.files:
                f.scale *= 1.0 / extent
            self.log.append("%s : %.4f m in its file, with nothing that gives its size : scaled to 1 m" % (animal.id, extent))

        self.skeleton()
        self.geometry()
        low, high = self.bounds()
        self.size = float(max(high - low).clip(1e-3))
        self.facing_in_file = "-z"
        self.face_forward()
        self.clips()

        # Bounds : the animal as it stands (the bind pose of a bird has its wings open)
        low, high = self.bounds()
        skin = self.standing_pose() @ self.inverse_bind
        posed_min, posed_max = np.full(3, np.inf), np.full(3, -np.inf)
        for part in (p for p in self.parts if 0 in p["levels"]):
            homogeneous = np.concatenate([part["position"], np.ones((len(part["position"]), 1))], axis=1)
            posed = np.einsum("vk,vkab,vb->va", part["weights"] / 255.0, skin[part["joints"]], homogeneous)[:, :3]
            posed_min, posed_max = np.minimum(posed_min, posed.min(0)), np.maximum(posed_max, posed.max(0))

        # Files
        vertex_chunks, index_chunks, lods = [], [], [{ "draws": [] } for _ in range(self.level_count)]
        first_vertex = first_index = 0
        for part in self.parts:
            vertices = np.zeros(len(part["position"]), dtype=[("position", "<f4", 3), ("normal", "i1", 4), ("uv", "<f4", 2), ("joints", "<u2", 4), ("weights", "u1", 4)])
            vertices["position"] = part["position"]
            vertices["normal"][:, :3] = snorm8(part["normal"])
            vertices["uv"] = part["uv"]
            vertices["joints"] = part["joints"]
            vertices["weights"] = part["weights"]
            vertex_chunks.append(vertices)
            index_chunks.append(part["indices"] + first_vertex)
            for level in part["levels"]:
                for draw in part["draws"]:
                    lods[level]["draws"].append({ "mesh": part["name"], "material": draw["material"], "first_index": first_index + draw["first"], "index_count": draw["count"] })
            first_vertex += len(vertices)
            first_index += len(part["indices"])
        for level, lod in enumerate(lods):
            lod["triangles"] = sum(d["index_count"] for d in lod["draws"]) // 3
            # Drawn while the animal is taller than this part of the screen; the last level always is
            lod["screen_height"] = round(0.3 * 0.5 ** level, 4) if level < len(lods) - 1 else 0.0

        os.makedirs(os.path.join(self.out_dir, "species"), exist_ok=True)
        rest_t, rest_q, rest_s = decompose(self.rest_local)
        with open(os.path.join(self.out_dir, "species", animal.id + ".bin"), "wb") as f:
            for chunk in vertex_chunks:
                f.write(chunk.tobytes())
            for chunk in index_chunks:
                f.write(chunk.astype("<u4").tobytes())
            # Inverse bind matrices, 3 rows of 4, then the bind pose as local translation, rotation (x y z w), scale
            f.write(self.inverse_bind[:, :3, :].astype("<f4").tobytes())
            f.write(np.concatenate([rest_t, rest_q, rest_s], axis=1).astype("<f4").tobytes())
        with open(os.path.join(self.out_dir, "species", animal.id + ".anim"), "wb") as f:
            for data in self.clip_data:
                f.write(data)

        def best(kind):
            found = [c for c in self.clip_list if c["kind"] == kind]
            found = [c for c in found if c["motion_joint"] < 0] or found
            found = [c for c in found if c["loop"]] or found
            if kind in ("walk", "trot", "run"):
                found = [c for c in found if c["speed"] > 0.0] or found
            return min(found, key=lambda c: gait_rank(c["name"]))["name"] if found else None

        name = re.sub(r"[_ ]+", " ", self.ref.animal).strip().capitalize()
        species = {
            "format_version": FORMAT_VERSION,
            "id": animal.id,
            "name": name,
            "sources": [f.path for f in animal.files],
            "clip_sources": sorted({ c["source"] for c in self.clip_list }),
            "bounds": { "min": [round(float(x), 4) for x in posed_min], "max": [round(float(x), 4) for x in posed_max] },
            "bind_bounds": { "min": [round(float(x), 4) for x in low], "max": [round(float(x), 4) for x in high] },
            "facing_in_file": self.facing_in_file,
            "facing_from": self.facing,
            "vertex_count": first_vertex,
            "index_count": first_index,
            # skin : part of the vertices of the most detailed level the joint moves (0 : a control of the rig, an end)
            "joints": [{ "name": n, "parent": p, "skin": round(float(w), 5) } for n, p, w in zip(self.names, self.parents, self.joint_skin())],
            "materials": self.materials,
            "lods": lods,
            "clips": self.clip_list,
            "roles": { kind: best(kind) for kind in ("idle", "walk", "trot", "run", "eat", "rest") },
        }
        with open(os.path.join(self.out_dir, "species", animal.id + ".json"), "w", encoding="utf-8") as f:
            json.dump(species, f, indent=1)

        def speed_of(role):
            clip = next((c for c in self.clip_list if c["name"] == species["roles"][role]), None)
            return clip["speed"] if clip else 0.0

        entry = {
            "id": animal.id,
            "name": name,
            "source": self.ref.path,
            "height": round(float(posed_max[1] - min(posed_min[1], 0.0)), 4),
            "length": round(float(posed_max[2] - posed_min[2]), 4),
            "width": round(float(posed_max[0] - posed_min[0]), 4),
            "joints": len(self.names),
            "triangles": lods[0]["triangles"],
            "lods": len(lods),
            "clips": len(self.clip_list),
            "seconds": round(sum((c["frames"] - 1) / c["fps"] for c in self.clip_list), 1),
            "walk": species["roles"]["walk"],
            "walk_speed": speed_of("walk"),
            "run": species["roles"]["run"],
            "run_speed": speed_of("run"),
            "facing": self.facing,
        }
        for f in animal.files:
            f.release()
        return entry


# ---------------------------------------------------------------------------------------------------------------------
# Unity's own clips (.anim) : compared with the clips of the models they were duplicated from

def anim_file_summary(path):
    """Length, sample rate and bone paths of a Unity AnimationClip"""
    text = read_text(path)
    name = re.search(r"^  m_Name: (.*)$", text, re.M)
    rate = re.search(r"^  m_SampleRate: ([0-9.eE+-]+)", text, re.M)
    start = re.search(r"^    m_StartTime: ([0-9.eE+-]+)", text, re.M)
    stop = re.search(r"^    m_StopTime: ([0-9.eE+-]+)", text, re.M)
    paths = set(re.findall(r"^    path: (.*)$", text, re.M))
    humanoid = re.search(r"attribute: (Spine Front-Back|LeftHand\.|Left Upper Leg|Chest Front-Back)", text) is not None
    return {
        "name": name.group(1).strip() if name else os.path.splitext(os.path.basename(path))[0],
        "rate": float(rate.group(1)) if rate else 0.0,
        "length": (float(stop.group(1)) - float(start.group(1))) if start and stop else 0.0,
        "bones": { p.split("/")[-1] for p in paths if p.strip() },
        "humanoid": humanoid,
    }


def check_unity_clips(src_root, files, species_by_folder):
    """Every .anim against the clips imported from the models of its pack : [(path, verdict)]"""
    out = []
    for path in sorted(p for p in files if p.lower().endswith(".anim")):
        summary = anim_file_summary(os.path.join(src_root, path))
        best = None
        for folder, entries in species_by_folder.items():
            if not in_scope(folder, os.path.dirname(path)) and not os.path.dirname(path).startswith(folder + "/"):
                continue
            for species in entries:
                joints = { j["name"] for j in species["joints"] }
                share = len(summary["bones"] & joints) / max(len(summary["bones"]), 1)
                for clip in species["clips"]:
                    same_name = clip["name"].lower() == summary["name"].lower() or clip["name"].lower() == os.path.splitext(os.path.basename(path))[0].lower()
                    length = (clip["frames"] - 1) / clip["fps"]
                    same_length = abs(length - summary["length"]) <= 1.5 / max(clip["fps"], 1.0)
                    score = (same_name and same_length and share >= 0.8, same_name, same_length, share)
                    if best is None or score > best[0]:
                        best = (score, species["id"], clip["name"], length)
        if summary["humanoid"]:
            verdict = "humanoid muscle clip (Unity's retargeting space), not importable without its avatar"
        elif best and best[0][0]:
            verdict = "same clip as %s / %s (%.2f s, the same bones) : imported from the FBX" % (best[1], best[2], best[3])
        elif best and best[0][1]:
            verdict = "named as %s / %s but %.2f s against %.2f s : NOT imported" % (best[1], best[2], summary["length"], best[3])
        else:
            verdict = "no clip of the models matches (%.2f s, %d bones) : NOT imported" % (summary["length"], len(summary["bones"]))
        out.append((path, verdict))
    return out


# ---------------------------------------------------------------------------------------------------------------------

def build(work, files, dumps, out_dir, max_texture, only=None, list_only=False):
    src_root = os.path.join(work, "src")
    by_dump = {}
    for path in sorted(dumps):
        by_dump.setdefault(dumps[path], []).append(path)
    fbx_files = [Fbx(paths, dump, src_root) for dump, paths in sorted(by_dump.items(), key=lambda e: e[1][0])]
    log = []
    for f in fbx_files:
        if f.error:
            log.append("%s : not read by Blender (%s)" % (f.path, f.error))
    fbx_files = [f for f in fbx_files if not f.error]

    animals, unused = find_animals(fbx_files, log)
    if only:
        pattern = re.compile(only, re.I)
        selected = [a for a in animals if pattern.search(a.id) or pattern.search(a.reference.path)]
    else:
        selected = animals

    if list_only:
        for a in sorted(selected, key=lambda a: a.id):
            clips = sum(len(f.clip_sources()) for f in a.clip_files())
            print("%-28s %3d clip sources  %s%s" % (a.id, clips, a.reference.path, "  (clips of %s)" % a.borrowed.id if a.borrowed else ""))
        print(len(selected), "animals")
        return

    project = UnityProject(src_root, files)
    textures = TextureStore(out_dir, max_texture, FolderSource(src_root), None)
    entries, failed = [], []
    for a in sorted(selected, key=lambda a: a.id):
        try:
            entry = Builder(a, project, textures, src_root, out_dir, log).build()
        except (ValueError, KeyError, IndexError, np.linalg.LinAlgError) as e:
            failed.append((a.id, a.reference.path, "%s: %s" % (type(e).__name__, e)))
            print("%-28s left out : %s" % (a.id, e), flush=True)
            continue
        entries.append(entry)
        print("%-28s %5.2f m tall %5.2f long %5.2f wide  %3d joints %6d triangles %d lods  %3d clips %6.1f s  walk %s %.2f m/s  run %s %.2f m/s  (faces by %s)" % (
            entry["id"], entry["height"], entry["length"], entry["width"], entry["joints"], entry["triangles"], entry["lods"], entry["clips"], entry["seconds"],
            entry["walk"], entry["walk_speed"], entry["run"], entry["run_speed"], entry["facing"]), flush=True)
    textures.finish()

    library_path = os.path.join(out_dir, "library.json")
    previous = {}
    if os.path.isfile(library_path):
        with open(library_path, "r", encoding="utf-8") as f:
            previous = json.load(f)
    if only:
        done = { e["id"] for e in entries }
        entries += [e for e in previous.get("species", []) if e["id"] not in done]
    else:
        kept = { e["id"] for e in entries }
        species_dir = os.path.join(out_dir, "species")
        for name in os.listdir(species_dir) if os.path.isdir(species_dir) else []:
            if os.path.splitext(name)[0] not in kept:
                os.remove(os.path.join(species_dir, name))
    entries.sort(key=lambda e: e["id"])

    # Unity's .anim files
    species_by_folder = {}
    for entry in entries:
        path = os.path.join(out_dir, "species", entry["id"] + ".json")
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as f:
                species = json.load(f)
            species_by_folder.setdefault(os.path.dirname(species["sources"][0]), []).append(species)
    unity_clips = check_unity_clips(src_root, files, species_by_folder)

    report = {
        "fbx_files": len(dumps),
        "different_fbx_files": len(by_dump),
        "animations_without_animal": [f.path for f in unused],
        "helper_meshes_left_out": sorted({ "%s : %s" % (f.path, m["name"]) for f in fbx_files for m in f.helpers }),
        "unity_clips": [{ "path": p, "verdict": v } for p, v in unity_clips],
        "failed": [{ "id": i, "source": p, "error": e } for i, p, e in failed],
        "notes": log,
    }
    with open(library_path, "w", encoding="utf-8") as f:
        json.dump({ "format_version": FORMAT_VERSION, "species": entries, "report": previous.get("report") if only else report }, f, indent=1)

    print("%d animals, %d clips, %.0f s of animation, %d textures written, %d already there" % (
        len(entries), sum(e["clips"] for e in entries), sum(e["seconds"] for e in entries), textures.written, textures.reused))
    if failed:
        print("%d animals left out :" % len(failed))
        for i, p, e in failed:
            print("  %s (%s) : %s" % (i, p, e))
    if unused:
        print("%d animation files fit no animal :" % len(unused))
        for f in unused:
            print("  " + f.path)
    if textures.missing:
        print("%d missing textures :" % len(set(textures.missing)))
        for m in sorted(set(textures.missing)):
            print("  " + m)
    not_imported = [(p, v) for p, v in unity_clips if "NOT imported" in v or "humanoid" in v]
    print("%d Unity .anim files : %d are clips already imported from their FBX, %d are not" % (len(unity_clips), len(unity_clips) - len(not_imported), len(not_imported)))
    for p, v in not_imported:
        print("  %s : %s" % (p, v))
    helpers = report["helper_meshes_left_out"]
    if helpers:
        print("%d helper meshes left out (rig controls, bones drawn as boxes, blob shadows)" % len(helpers))
    for line in log:
        print("  note : " + line)
