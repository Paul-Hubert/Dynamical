"""Builds the engine's ground textures (mud, stones, sand, rock, earth) from the terrain textures of the Tautavel Unity project.

    python tools/ground_import.py --source <Tautavel repo> [--lfs <lfs objects>] [--out resources/ground]

--source and --lfs are those of tools/speedtree_import.py : a checkout or a bare git repository, and the Git LFS
object store when the source only has pointers.

Output, read by src/renderer/passes/terrain_pass (docs/ground.md) :
    ground.json                  the layers : name, size of a tile in metres, smoothness, files
    <layer>_color.png            albedo (sRGB)
    <layer>_detail.png           r g : normal (x and z of the world for a texture laid u along x and v along z,
                                 0.5 is flat), b : height (0 the hollows, 1 the tops), a : ambient occlusion

The heights are what the engine blends the layers with : stones come out of the ground by their tops, mud
fills the hollows. A layer without a height map gets one from its normal map. The sign of each normal map is
found by comparing it with the slopes of the height map.
"""

import argparse
import io
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from speedtree_import import FolderSource, GitSource, lfs_resolve  # noqa: E402

Image.MAX_IMAGE_PIXELS = None

FORMAT_VERSION = 1

NM = "Assets/Tautavel/NatureManufacture Assets/River Auto Material/Assets/Ground River Textures/"
HOMO = "Assets/Tautavel/Homo/Terrain/Textures/"

# In the order of the channels of the soil maps (src/logic/terrain/soil.h).
# tile : metres covered by the texture. gain : on the linear albedo (the mud of the project is nearly black
# without the exposure of its HDRP camera). occlusion : (file, channel)
LAYERS = [
    {
        "name": "mud",
        "tile": 2.0,
        "smoothness": 0.64,     # m_Smoothness of MuddySandShowcase.terrainlayer
        "gain": 6.0,
        "color": HOMO + "MuddySand2_1K/TexturesCom_MuddySand2_2x2_1K_albedo.tif",
        "normal": HOMO + "MuddySand2_1K/TexturesCom_MuddySand2_2x2_1K_normal.tif",
        "height": None,
        "occlusion": (HOMO + "MuddySand2_1K/TexturesCom_MuddySand2_2x2_1K_maskmap.tif", 1),
    },
    {
        "name": "stones",
        "tile": 1.8,
        "smoothness": 0.3,
        "gain": 1.0,
        "color": NM + "T_Ground_Pebbles_01_A.tga",
        "normal": NM + "T_Ground_Pebbles_01_N.png",
        "height": NM + "T_Ground_Pebbles_01_H.tga",
        "occlusion": (NM + "T_Ground_Pebbles_01_AO.tga", 0),
    },
    {
        "name": "sand",
        "tile": 2.5,
        "smoothness": 0.12,
        "gain": 1.0,
        "color": NM + "T_Ground_Sand_01_A.tga",
        "normal": NM + "T_Ground_Sand_01_N.png",
        "height": NM + "T_Ground_Sand_01_H.tga",
        "occlusion": (NM + "T_Ground_Sand_01_AO.tga", 0),
    },
    # The steep faces, after the layers of the soil maps. Laid on vertical planes, v going down.
    # match : the stones are pieces of this rock, its mean colour is brought that far towards theirs
    # (the name of a layer before this one, or an sRGB colour)
    {
        "name": "rock",
        "tile": 5.0,
        "smoothness": 0.2,
        "gain": 1.0,
        "color": HOMO + "RockCliffGranite_1K/TexturesCom_Rock_CliffGranite_1K_albedo.tif",
        "normal": HOMO + "RockCliffGranite_1K/TexturesCom_Rock_CliffGranite_1K_normal.tif",
        "height": None,
        "occlusion": (HOMO + "RockCliffGranite_1K/TexturesCom_Rock_CliffGranite_1K_maskmap.tif", 1),
        "match": ("stones", 0.7),
    },
    # The ground under the grass : what shows where the grass gets thin
    {
        "name": "earth",
        "tile": 2.5,
        "smoothness": 0.1,
        "gain": 1.0,
        "color": NM + "T_Ground_Grass_01_1_A_Sm.tga",
        "normal": NM + "T_Ground_Grass_01_N.png",
        "height": NM + "T_Ground_Grass_01_H.tga",
        "occlusion": (NM + "T_Ground_Grass_01_AO.tga", 0),
        # Towards the green of the grass pass seen from far : no step in colour where the grass ends on a slope
        "match": ([84, 106, 62], 0.85),
    },
]


def load(source, lfs_dir, path):
    raw = source.read(path)
    data, _ = lfs_resolve(raw, lfs_dir) if raw is not None else (None, None)
    if data is None:
        raise SystemExit("missing : " + path + (" (LFS object not found)" if raw is not None else ""))
    return Image.open(io.BytesIO(data))


def channels(image, size, mode):
    """The image at size x size, floats in 0..1 : (size, size, channels)"""
    image = image.convert(mode)
    if image.size != (size, size):
        image = image.resize((size, size), Image.LANCZOS)
    array = np.asarray(image).astype(np.float32) / 255.0
    return array[:, :, None] if array.ndim == 2 else array


def to_linear(srgb):
    return np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def to_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1.0 / 2.4) - 0.055)


def slopes(height):
    """dh/du (along the columns) and dh/dv (along the rows) of a tile that repeats, per texel"""
    du = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dv = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    return du, dv


def correlation(a, b):
    a = a - a.mean()
    b = b - b.mean()
    return float((a * b).sum() / max(np.sqrt((a * a).sum() * (b * b).sum()), 1e-12))


def height_from_normal(nx, nz):
    """The height whose slopes are those of the normals (Frankot and Chellappa), for a tile that repeats"""
    ny = np.sqrt(np.clip(1.0 - nx * nx - nz * nz, 0.05, 1.0))
    p, q = -nx / ny, -nz / ny
    size = nx.shape[0]
    k = np.fft.fftfreq(size) * 2.0 * np.pi
    ku, kv = np.meshgrid(k, k)
    denominator = ku * ku + kv * kv
    denominator[0, 0] = 1.0
    spectrum = (-1j * ku * np.fft.fft2(p) - 1j * kv * np.fft.fft2(q)) / denominator
    spectrum[0, 0] = 0.0
    return np.real(np.fft.ifft2(spectrum)).astype(np.float32)


def stretch(height, low=0.5, high=99.5):
    a, b = np.percentile(height, [low, high])
    return np.clip((height - a) / max(b - a, 1e-6), 0.0, 1.0)


def convert(source, lfs_dir, layer, size, out_dir, means):
    name = layer["name"]

    color = to_linear(channels(load(source, lfs_dir, layer["color"]), size, "RGB")) * layer["gain"]
    if "match" in layer:
        other, amount = layer["match"]
        target = means[other] if isinstance(other, str) else to_linear(np.array(other, dtype=np.float32) / 255.0)
        own = color.reshape(-1, 3).mean(axis=0)
        color = color * ((own + (target - own) * amount) / np.maximum(own, 1e-6))
    means[name] = color.reshape(-1, 3).mean(axis=0)

    # Unity's tangent space : r along u, g along v with v going up the image. Here v goes down the rows
    normal = channels(load(source, lfs_dir, layer["normal"]), size, "RGB") * 2.0 - 1.0
    nx, nz = normal[:, :, 0], -normal[:, :, 1]

    report = ""
    if layer["height"]:
        height = stretch(channels(load(source, lfs_dir, layer["height"]), size, "L")[:, :, 0])
        # A normal leans away from where the ground goes up
        du, dv = slopes(height)
        along_u, along_v = correlation(nx, -du), correlation(nz, -dv)
        if along_u < 0.0:
            nx = -nx
        if along_v < 0.0:
            nz = -nz
        report = "normal against the height : %+.2f along u, %+.2f along v" % (along_u, along_v)
    else:
        height = stretch(height_from_normal(nx, nz))
        report = "height from the normal map"

    file, channel = layer["occlusion"]
    occlusion = channels(load(source, lfs_dir, file), size, "RGB")[:, :, channel]

    color_file, detail_file = name + "_color.png", name + "_detail.png"
    Image.fromarray((to_srgb(color) * 255.0 + 0.5).astype(np.uint8), "RGB").save(os.path.join(out_dir, color_file), compress_level=6)
    detail = np.stack([nx * 0.5 + 0.5, nz * 0.5 + 0.5, height, occlusion], axis=2)
    Image.fromarray((np.clip(detail, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(out_dir, detail_file), compress_level=6)

    mean = to_srgb(color.reshape(-1, 3).mean(axis=0))
    print("%-7s %s, mean colour %s, occlusion %.2f" % (name, report, [int(c * 255.0 + 0.5) for c in mean], float(occlusion.mean())))

    return {
        "name": name,
        "tile": layer["tile"],
        "smoothness": layer["smoothness"],
        "color": color_file,
        "detail": detail_file,
        "sources": [layer["color"], layer["normal"]] + ([layer["height"]] if layer["height"] else []) + [file],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, help="Tautavel checkout, or bare git repository")
    parser.add_argument("--ref", default="HEAD", help="commit read from a bare repository")
    parser.add_argument("--lfs", default=None, help="Git LFS object store")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "resources", "ground"))
    parser.add_argument("--size", type=int, default=1024, help="texels of a side, the same for every layer")
    args = parser.parse_args()

    if os.path.isdir(os.path.join(args.source, "Assets")):
        source = FolderSource(args.source)
    else:
        source = GitSource(args.source, args.ref)
    source.list()

    os.makedirs(args.out, exist_ok=True)
    means = {}
    layers = [convert(source, args.lfs, layer, args.size, args.out, means) for layer in LAYERS]
    with open(os.path.join(args.out, "ground.json"), "w", encoding="utf-8") as f:
        json.dump({"format_version": FORMAT_VERSION, "size": args.size, "layers": layers}, f, indent=1)
    print("wrote %d layers of %d x %d to %s" % (len(layers), args.size, args.size, args.out))


if __name__ == "__main__":
    main()
