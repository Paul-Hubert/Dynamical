"""Run by tools/animal_import.py inside Blender (4.5 or later) : reads FBX files, writes what the engine needs.

    blender --background --factory-startup --python tools/animal_blender.py -- <job.json>

job.json : { "files": [{ "fbx": path, "out": path without extension }] }

For each file, <out>.json and <out>.npz :
    joints     the bones of every armature, then one joint per mesh that is not skinned (its object). name, parent,
               rest (the bind pose), pose (the file without animation). Matrices are 3 x 4, in Blender's world : meters, Z up
    meshes     vertices in the world at rest, corner normals and uvs, triangles, their material slot, 4 weights a vertex
    takes      per frame, the world matrix of every joint

Nothing is interpreted here : clips, levels of detail, materials and the engine's axes are tools/animal_import.py's.
"""

import json
import os
import sys
import time
import traceback

import bpy
import numpy as np
from mathutils import Matrix


def m34(matrix):
    return [[matrix[r][c] for c in range(4)] for r in range(3)]


def material_images(material):
    """File names of the images a material reads, the ones wired to the color and to the normal first"""
    out = { "name": material.name, "color": None, "normal": None, "images": [] }
    tree = material.node_tree
    if not tree:
        return out

    def image_behind(socket, depth=0):
        if depth > 6 or not socket.is_linked:
            return None
        node = socket.links[0].from_node
        if node.type == "TEX_IMAGE":
            return node.image.filepath if node.image else None
        for inp in node.inputs:
            found = image_behind(inp, depth + 1)
            if found:
                return found
        return None

    for node in tree.nodes:
        if node.type == "TEX_IMAGE" and node.image:
            out["images"].append(node.image.filepath)
        if node.type == "BSDF_PRINCIPLED":
            out["color"] = image_behind(node.inputs["Base Color"])
            out["normal"] = image_behind(node.inputs["Normal"])
            out["base_color"] = list(node.inputs["Base Color"].default_value)
    return out


DUMP_VERSION = 1


def dump(path, out):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.fbx_import(filepath=path)
    scene = bpy.context.scene
    fps = scene.render.fps / scene.render.fps_base
    objects = list(scene.objects)

    # The file without animation : actions are taken off before anything is evaluated
    actions = list(bpy.data.actions)
    animated = [o for o in objects if o.animation_data]
    for o in animated:
        o.animation_data.action = None
    statics = []
    for o in objects:
        statics.append((o, o.location.copy(), o.rotation_euler.copy(), o.rotation_quaternion.copy(), o.scale.copy()))
        if o.type == "ARMATURE":
            for b in o.pose.bones:
                statics.append((b, b.location.copy(), b.rotation_euler.copy(), b.rotation_quaternion.copy(), b.scale.copy()))

    def restore():
        for item, location, euler, quaternion, scale in statics:
            item.location = location
            item.rotation_euler = euler
            item.rotation_quaternion = quaternion
            item.scale = scale

    bpy.context.view_layer.update()

    armatures = [o for o in objects if o.type == "ARMATURE"]
    meshes = [o for o in objects if o.type == "MESH" and len(o.data.polygons) > 0]

    # Joints
    joints = []
    joint_of = {}       # (armature name, bone name) or ("", object name) -> index
    for arm in armatures:
        world = arm.matrix_world.copy()
        for bone in arm.data.bones:
            joint_of[(arm.name, bone.name)] = len(joint_of)
        for bone in arm.data.bones:
            joints.append({
                "name": bone.name,
                "armature": arm.name,
                "parent": joint_of[(arm.name, bone.parent.name)] if bone.parent else -1,
                "rest": m34(world @ bone.matrix_local),
                "pose": m34(world @ arm.pose.bones[bone.name].matrix),
                "length": bone.length * world.to_scale()[0],
            })

    def skin_armature(o):
        for modifier in o.modifiers:
            if modifier.type == "ARMATURE" and modifier.object is not None:
                return modifier.object
        return None

    rigid = []
    for o in meshes:
        if skin_armature(o) is None:
            joint_of[("", o.name)] = len(joint_of)
            parent = joint_of.get((o.parent.name, o.parent_bone), -1) if o.parent is not None and o.parent_type == "BONE" else -1
            joints.append({ "name": o.name, "armature": "", "parent": -1, "attached_to": parent, "rest": m34(o.matrix_world), "pose": m34(o.matrix_world), "length": 0.0 })
            rigid.append(o)

    arrays = {}
    info = { "source": path, "dump_version": DUMP_VERSION, "fps": fps, "joints": joints, "meshes": [], "takes": [], "blender": bpy.app.version_string,
             "objects": [{ "name": o.name, "type": o.type, "parent": o.parent.name if o.parent else None } for o in objects] }

    # Meshes
    for index, o in enumerate(meshes):
        mesh = o.data
        world = np.array(o.matrix_world, dtype=np.float64)
        vertex_count = len(mesh.vertices)
        co = np.empty(vertex_count * 3, dtype=np.float32)
        mesh.vertices.foreach_get("co", co)
        co = co.reshape(-1, 3).astype(np.float64)
        position = co @ world[:3, :3].T + world[:3, 3]

        mesh.calc_loop_triangles()
        triangle_count = len(mesh.loop_triangles)
        loops = np.empty(triangle_count * 3, dtype=np.int32)
        mesh.loop_triangles.foreach_get("loops", loops)
        tri_material = np.empty(triangle_count, dtype=np.int32)
        mesh.loop_triangles.foreach_get("material_index", tri_material)

        loop_count = len(mesh.loops)
        loop_vertex = np.empty(loop_count, dtype=np.int32)
        mesh.loops.foreach_get("vertex_index", loop_vertex)
        normal = np.empty(loop_count * 3, dtype=np.float32)
        mesh.corner_normals.foreach_get("vector", normal)
        normal = normal.reshape(-1, 3).astype(np.float64) @ np.linalg.inv(world[:3, :3])
        normal /= np.maximum(np.linalg.norm(normal, axis=1, keepdims=True), 1e-12)
        uv = np.zeros(loop_count * 2, dtype=np.float32)
        if mesh.uv_layers.active is not None:
            mesh.uv_layers.active.data.foreach_get("uv", uv)

        # The 4 largest weights of each vertex
        weight_joint = np.zeros((vertex_count, 4), dtype=np.int32)
        weight = np.zeros((vertex_count, 4), dtype=np.float32)
        arm = skin_armature(o)
        unweighted = 0
        if arm is not None:
            group_joint = [joint_of.get((arm.name, g.name), -1) for g in o.vertex_groups]
            for v in mesh.vertices:
                found = sorted(((g.weight, group_joint[g.group]) for g in v.groups if g.weight > 0.0 and group_joint[g.group] >= 0), reverse=True)[:4]
                if not found:
                    unweighted += 1
                    continue
                for k, (w, j) in enumerate(found):
                    weight[v.index, k] = w
                    weight_joint[v.index, k] = j
        else:
            weight_joint[:, 0] = joint_of[("", o.name)]
            weight[:, 0] = 1.0

        key = "mesh%d_" % index
        arrays[key + "position"] = position.astype(np.float32)
        arrays[key + "normal"] = normal.astype(np.float32)
        arrays[key + "uv"] = uv.reshape(-1, 2)
        arrays[key + "loop_vertex"] = loop_vertex
        arrays[key + "triangles"] = loops.reshape(-1, 3)
        arrays[key + "tri_material"] = tri_material
        arrays[key + "weight_joint"] = weight_joint
        arrays[key + "weight"] = weight
        info["meshes"].append({
            "name": o.name,
            "data_name": mesh.name,
            "skinned": arm is not None,
            "armature": arm.name if arm is not None else None,
            "parent": o.parent.name if o.parent else None,
            "vertices": vertex_count,
            "triangles": triangle_count,
            "unweighted": unweighted,
            "has_uv": mesh.uv_layers.active is not None,
            "materials": [material_images(m) if m else None for m in mesh.materials],
        })

    # Takes : skinned meshes do not have to follow
    for o in meshes:
        o.modifiers.clear()
    for take_index, action in enumerate(actions):
        restore()
        used = False
        for o in animated:
            slot = next((s for s in action.slots if s.identifier == "OB" + o.name), None)
            if slot is not None:
                o.animation_data.action = action
                o.animation_data.action_slot = slot
                used = True
            else:
                o.animation_data.action = None
        if not used:
            continue
        first, last = int(round(action.frame_range[0])), int(round(action.frame_range[1]))
        frames = last - first + 1
        world = np.zeros((frames, len(joints), 3, 4), dtype=np.float32)
        buffer, order = {}, {}
        for f in range(frames):
            scene.frame_set(first + f)
            depsgraph = bpy.context.evaluated_depsgraph_get()
            for arm in armatures:
                evaluated = arm.evaluated_get(depsgraph)
                count = len(evaluated.pose.bones)
                if count not in buffer:
                    buffer[count] = np.empty(count * 16, dtype=np.float32)
                evaluated.pose.bones.foreach_get("matrix", buffer[count])
                # foreach_get gives matrices column by column
                pose = buffer[count].reshape(count, 4, 4).transpose(0, 2, 1)
                arm_world = np.array(evaluated.matrix_world, dtype=np.float32)
                if arm.name not in order:
                    order[arm.name] = np.array([joint_of[(arm.name, b.name)] for b in evaluated.pose.bones])
                world[f, order[arm.name]] = (arm_world @ pose)[:, :3, :]
            for o in rigid:
                world[f, joint_of[("", o.name)]] = np.array(o.evaluated_get(depsgraph).matrix_world, dtype=np.float32)[:3, :]
        arrays["take%d_world" % take_index] = world
        # Frame 0 of the take (FBX time 0) is Blender's frame 1
        info["takes"].append({ "name": action.name, "index": take_index, "first_frame": first - 1, "frames": frames })
        for o in animated:
            o.animation_data.action = None

    os.makedirs(os.path.dirname(out), exist_ok=True)
    np.savez(out + ".npz", **arrays)
    with open(out + ".json", "w", encoding="utf-8") as f:
        json.dump(info, f)
    return info


def main():
    with open(sys.argv[sys.argv.index("--") + 1], "r", encoding="utf-8") as f:
        job = json.load(f)
    for index, entry in enumerate(job["files"]):
        start = time.time()
        try:
            info = dump(entry["fbx"], entry["out"])
            print("DUMP %d/%d %s : %d joints, %d meshes, %d takes, %d frames, %.1f s" % (
                index + 1, len(job["files"]), os.path.basename(entry["fbx"]), len(info["joints"]), len(info["meshes"]), len(info["takes"]),
                sum(t["frames"] for t in info["takes"]), time.time() - start), flush=True)
        except Exception as e:  # one bad file does not stop the others
            traceback.print_exc()
            os.makedirs(os.path.dirname(entry["out"]), exist_ok=True)
            with open(entry["out"] + ".json", "w", encoding="utf-8") as f:
                json.dump({ "source": entry["fbx"], "dump_version": DUMP_VERSION, "error": "%s: %s" % (type(e).__name__, e) }, f)
            print("DUMP %d/%d %s : FAILED %s" % (index + 1, len(job["files"]), os.path.basename(entry["fbx"]), e), flush=True)


main()
