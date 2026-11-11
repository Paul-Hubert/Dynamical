# Animals

The animals of the Tautavel Unity project (FBX files, skinned, with their animations), converted to a library the
engine draws and animates itself; herds of them in the forests; and a procedural walk made from their walk clips,
that turns, follows the terrain and wades through soft ground, with a generator of demonstrations for imitation
learning.

```bash
cd build && ./RelWithDebInfo/dynamical.exe scene=forest_herds        # the forests of forest.md, with herds
cd build && ./RelWithDebInfo/dynamical.exe scene=forest_herds_watch  # the camera goes round a herd (var.kind=bison)
cd build && ./RelWithDebInfo/dynamical.exe scene=animals_test        # every animal in rows, playing its clips
cd build && ./RelWithDebInfo/dynamical.exe scene=animals_gait        # the procedural walk (var.path, var.soft_depth, ...)
build/venv/Scripts/python tools/herd_video.py --kind "red deer"      # build/artifacts/herd_video/*.mp4
```

## Building the library

Generated, not committed (`resources/animals/` is ignored: 550 MB, licensed models).

```bash
python tools/animal_import.py --source <Tautavel> [--lfs <lfs objects>]
```

`--source` and `--lfs` are those of `tools/speedtree_import.py` ([vegetation.md](vegetation.md)). Needs numpy, Pillow
and **Blender 4.5 or later** (`--blender`, the newest installed one by default): its FBX importer reads every FBX
version of the project (6100 to 7700). `--list` lists the animals, `--only <regex>` converts some and keeps the rest.
A full run takes 5 minutes the first time, a little over one after.

Three steps, each skipped for what is done (`build/animals_work`):

1. **Extract**: `Faune/`, `Realistic/`, `_Flore et Mineraux/Faune/` and `Animals/` of the project (3302 files, 6.6 GB).
2. **Dump** (`tools/animal_blender.py`, inside Blender): each of the 410 different FBX files (418 with the copies)
   gives its joints (bind pose, pose without animation), its meshes (corner normals, uvs, 4 weights a vertex) and,
   for each take, the world matrix of every joint at every frame. Nothing is interpreted there.
3. **Build** (`tools/animal_build.py`): what belongs together is worked out from the files.
   - Files of a folder whose names differ by a word of quality or motion (`Cat_high` / `Cat_low`, `Deer_red_LOD` /
     `Deer_red_IP` / `Deer_red_RM`, `badger@walk` / `badger@run`) and that share a skeleton are one animal.
   - Files without a mesh (`Wolf@Walk.FBX`) give their clips to the animals of their pack they fit: the same bone
     names, and bones that stay attached when the clip is played on the animal's bind pose.
   - An animal without clips takes those of one next to it with the same skeleton (`Cat_Fat`, from `Cat`).
   - Meshes found twice are kept once; bodies that fill the same box are levels of detail, the rest are parts.
   - Takes are cut in clips as Unity's `.meta` says (`clipAnimations`), sizes are Unity's (`globalScale`,
     `useFileScale`), materials come from Unity's `.mat` (the link of the `.meta`, else its name, else the nearest
     one of the pack that has a texture), then from the textures the FBX names.
   - The animal is turned to look down -Z: by where its walk takes it, else by how its feet slide in a walk played
     in place, else by its head.
   - A mesh bound in another space than the skeleton (the low levels of the forest wolf: 100 times larger, lying
     down) is brought back on it.

What comes out: **58 animals, 1797 clips, 47 minutes of animation**, 99 textures. The run ends with what was left
aside, also in `library.json` (`report`):

- `Homo Heidelbergensis/Animations/Neutral Idle.fbx` fits no model (another skeleton than the two men of that folder).
- Of the 60 Unity `.anim` files, 53 are clips Unity duplicated from an FBX (the same name, length and bones):
  imported from the FBX. The 7 others are in `Homo Heidelbergensis`: 3 are humanoid muscle clips, unreadable without
  their avatar, 4 match no model of the folder.
- 163 helper meshes (controls of rigs, bones drawn as boxes, blob shadow quads) are not animal.
- `Hemitragus_bonali` is a scan a millimetre long with nothing that gives its size: scaled to 1 m.
- The two `*_Fur_FBX` models and two materials of the men have no texture in the project: drawn plain.

## Files

```
resources/animals/
  library.json           index: id, name, height, length, width, joints, triangles, lods, clips, walk and run clip and speed
  species/<id>.json      joints (name, parent, skin), materials, levels of detail and their draw calls, clips, roles
  species/<id>.bin       vertices (36 bytes each), indices (uint32), inverse bind matrices (3 x 4 floats a joint),
                         bind pose (translation, rotation x y z w, scale: 10 floats a joint)
  species/<id>.anim      clips, one after the other
  textures/<name>.png    shared between animals
```

Model space: metres, Y up, the animal looks down -Z, feet around y = 0. Vertex: position (3 float), normal (4 int8
normalized), uv (2 float, v down), joints (4 uint16), weights (4 uint8, sum 255).

A clip: a byte per joint (bit 0: translation animated, 1: rotation, 2: scale), padded to 4, then for each joint its
translations (3 float a key), rotations (4 int16 a key, x y z w, 32767 is 1) and scales (3 float a key): one key
when the channel is constant, one a frame else. In the json: `frames`, `fps`, `loop`, `kind` (idle, walk, trot, run,
eat, rest, other, from the name), `speed` (m/s), and for a clip whose root travels (`_RM` files) `motion_joint` and
`root_motion`: the engine can take the travel out and move the animal itself. `roles` names the plain clip of each kind.

## Engine

- `src/logic/animals/animal.{h,cpp}` (no Vulkan): `AnimalLibrary` (context variable), `AnimalSpecies` (skeleton,
  clips, `sample`, `worldMatrices`, `skinMatrices`), the components `Animal` (species, clip, time, rate, `play()`
  with a cross fade) and `AnimalPose`, and `AnimalAnimationSys` (`"AnimalAnimation"`): advances the clips and
  writes the poses, on the taskflow executor.
- `AnimalPass` (`"animals"` in a scene's passes): draws every `Transform` + `Animal` + `AnimalPose`. Species load
  on first use as the vegetation does. Skinning matrices of every animal in view go in one storage buffer a frame
  (65536 joints), four joints a vertex in `animal.vert.glsl`. One level of detail per animal and target, animals out
  of the desktop view skipped. `animal.frag.glsl`: the vegetation's light and the shade of the trees; normal maps
  through screen derivatives (the library has no tangents).
- `AnimalLineupSys` (`"AnimalLineup"`, scene `animals_test`): every animal in rows on the most level ground around,
  playing all its clips or one kind (`var.clips=walk`).

## Herds

`HerdSys` (`"Herds"`), `resources/herds/tautavel.herds.json`: kinds of herds (red deer with stags and calves,
reindeer, bison, horses, rhinoceros, boars, wolves, mouflons, moose, lions, bears), each with its animals, a count,
a spacing and where it lives against the forest cover (`open`, `edge`, `forest`, `any`).

- The world is cut in cells (`cell`, 170 m); a cell has a herd with a probability (`density`), of a kind drawn by
  weight, at a home on land that suits it (slope under `max_slope`). A herd only depends on the seed and the cell:
  the same one is there when the viewer comes back. Herds within `radius` (420 m) of the camera exist.
- A herd grazes (`graze` seconds), then moves to another place within `range` of its home, at the pace of its
  slowest animal, waiting for those behind.
- An animal keeps a place in the herd. It walks at the speed of its walk clip, turns at a limited rate, keeps off
  the others and goes round the trunks of the trees (`Plant` entities over 2.5 m), stands on the ground
  (`TerrainField`) and leans with its slope. Standing, it plays its idle and eat clips and steps aside now and then.
- `HerdWatchSys` (`"HerdWatch"`, scene `forest_herds_watch`): the camera goes round a herd, of a kind when asked
  (the one with the most other herds around it). It turns as long as no crown comes between it and the herd, and
  stops rather than go through a tree (`clear`).
- Herds among each other, for a video: small cells and a short range, kept close to the camera.
  `tools/herd_video.py --kind bison --seed 6 --seconds 60 --var herd_cell=30 --var herd_radius=160 --var herd_range=60
  --var herd_density=0.85 --var distance=13 --var height=4`; `--preview 6` renders six frames to choose a seed.

## The procedural walk

`src/logic/animals/gait.{h,cpp}`. A walk clip is a straight walk on level, hard ground. The procedural walk keeps
its look and makes it turn, follow the terrain and wade through soft ground, for any animal the clip of which can
be read: four legs, two (birds), a man.

**Reading the clip** (`analyseGait`, no knowledge of the animal): feet are the joints that move skin, come down to
the ground, lift off it and go back and forth; the toes of a foot are one foot; a leg is the chain from a foot up
to the joint another leg hangs from too. A foot is down while it goes back under the body. Out come, per leg: its
chain, when it lands and leaves in the cycle, how far back it goes, how high it lifts and how. 39 of the 51 animals
that have a walk are read; the 12 others are those of one pack (cow, sheep, pig, hens, ...) whose rig holds the
feet with controls, not with the legs: they play their clip as it is.

**Making the pose** (`proceduralWalk`, `AnimalGaitSys` = `"AnimalGait"` after `"AnimalAnimation"`), from how the
entity's `Transform` moves:

1. The steps match the speed: the clip plays faster or slower (`stride`).
2. The body follows the curve of its path: every joint is carried to an arc, hind quarters out, shoulders in, neck
   and head further in (`bend`, `head_lead`, `max_bend`), and leans into the turn (`bank`).
3. Each foot is planted where it lands until it leaves, then swings to where it will land: under its hip, half a
   stance ahead of where the body will be, along the curve, on the ground there.
4. Soft ground (`soft_depth`): a planted foot sinks (`sink_time`), the body comes down (`body_sink`); to leave, the
   foot is first pulled out, straight up or along its swing (`exit_vertical`, `exit_part`), then lifted over the
   surface (`step_height`, `clearance`), and set down straight or not (`entry_vertical`).
5. Legs reach their feet by turning their joints from the clip's pose (cyclic coordinate descent), the foot keeps
   its attitude; a leg at the end of its reach brings the body down rather than let its foot go.

`GaitStyle` holds the 17 parameters; the debug menu (Animals, "One walk for all") has sliders for them, the scene
`animals_gait` has variables (`path` circle / eight / weave / straight, `radius`, `speed`, `gait`, `soft_depth`,
`exit_vertical`, `follow`, `procedural=false` to compare with the clip). Herds walk with it.

Measured on a circle of radius 2 lengths, 12 animals (`AGAIT.2`): planted feet move under 0.15 m/s 99 frames in
100, against 0.3 to 4 m/s for the clip alone.

## Demonstrations

For the imitation learning to come (physical animals trained like the walker of `physical-agents/`): a set of
demonstrations is a set of walks whose styles, speeds and turns are drawn from ranges.

```bash
cmake -DDYNAMICAL_BUILD_TOOLS=ON build && cmake --build build --config RelWithDebInfo --target gait_dataset
cd build && ./RelWithDebInfo/gait_dataset.exe --species all --samples 32 --seconds 10 --out artifacts/gait_demos
```

`--species a,b`, `--gait walk|trot|run`, `--seed`, `--flat` (a level plane instead of the terrain), `--ranges file.json`
(`GaitRanges`: `[low, high]` per parameter of `GaitStyle`, and `speed` in clip speeds, `radius` of the turns in
lengths, `straight`, `soft` the part of the walks on soft ground, `scale`, `segment` the seconds between two draws of
speed and turn). Lengths (`clearance`, `soft_depth`, `crouch`, `spread`) are in heights of the animal. 39 animals,
32 walks of 10 s each: 30 s, 470 MB.

`<species>_<gait>.json` says what is in `<species>_<gait>.bin` (float32): joints and parents, legs (chain, phases),
`sample_values` (the style, scale, start), `frame_layout`. Per walk: `sample_floats` values, then `frames` times
`frame_floats`: time, root position (3) and rotation (x y z w), speed, turn, phase, per leg planted / foot position
(3, world) / ground height, per joint position (3) and rotation (4) in model space.

```python
h = json.load(open("deer_red_walk.json"))
d = np.fromfile("deer_red_walk.bin", "<f4").reshape(h["samples"], h["sample_floats"] + h["frames"] * h["frame_floats"])
styles, frames = d[:, :h["sample_floats"]], d[:, h["sample_floats"]:].reshape(h["samples"], h["frames"], h["frame_floats"])
```

## Tests

`tests/unit/animals.cpp`: `ANIMAL.1` to `ANIMAL.3` (a library written by the test), `HERD.1`, `ANIMAL.library` (the
generated library: every animal loads, skins its bind pose in place, samples its 1797 clips).
`tests/unit/animal_gait.cpp`: `AGAIT.1` (legs found), `AGAIT.2` (feet planted through a turn, bend), `AGAIT.3` (soft
ground), `AGAIT.4` (pace), `AGAIT.5` (demonstrations). Those on the generated library are skipped without it.

## Physical animals

Every animal whose walk is read has a physical body, an agent and a training chain, generated from the library and the
gait analysis with no file per species: [physical-agents/reports/animal-agents.md](physical-agents/reports/animal-agents.md)
(what was measured, the readiness table, what is not validated).

```bash
cmake --build build --config RelWithDebInfo --target animal_body TrainingEnv      # -DDYNAMICAL_BUILD_TOOLS=ON
build/RelWithDebInfo/animal_body.exe --species all --check                       # resources/animals/bodies/<id>.body.json, stood and dropped
build/venv/Scripts/python tools/animal_sheets.py                                 # build/runs/animal_bodies: each body beside its mesh
build/venv/Scripts/python tools/train_animal.py --species deer_red --ranges walk_basic --max-steps 20000000
build/venv/Scripts/python tools/animal_eval.py --run animal_deer_red_walk_basic
build/venv/Scripts/python tools/animal_video.py --species deer_red --run animal_deer_red_walk_basic
build/venv/Scripts/python tools/animal_orders_video.py --run animal_deer_red_walk_basic   # one take through every order of its ranges
```

- `src/logic/animals/animal_body.{h,cpp}`: `generateAnimalBody` (which joints are bodies, shapes and masses from the
  skin, joint axes and limits from the motion, drives and solver settings by rule) and the loaded `AnimalBody`.
  `resources/agents/animal_overrides.json` holds what the library does not (a mass).
- `src/logic/animals/animal_agent.{h,cpp}`: `AnimalBot` and its systems (`AnimalFactory`, `AnimalDrive`, `AnimalSync`,
  `AnimalTask`, `AnimalTermination`, `AnimalRewards`, `AnimalObservation`, `AnimalEpisode`, `AnimalSoftGround`), the
  recorder of demonstrations (`AnimalGaitPlayer`: `proceduralWalk` played on the kinematic body, `AnimalChecks`).
- `src/logic/animals/gait_walk.{h,cpp}`: `WalkCommand` (the speed and turn drawn from `GaitRanges`, the same object
  for the task and for the recorded walk) and `GaitWalk` (one animal walking procedurally, no registry).
- Ranges: `resources/agents/ranges/walk_basic.json`, `walk_style.json`, `walk_soft.json`. Scenes: `animal_train`,
  `animal_eval`, `animal_view`, `animal_demo`, `animal_demo_view`, `animal_stand` and their `_terrain` variants.

## Not done

- The 12 animals whose rig holds the feet with controls, and `cat_farm` whose skeleton is not one tree: no body.
  The library's men have bodies of their own, generated like the others; they are not mapped on the walker's rig.
- Trot and run: no clip of the 38 animals is tagged trot; a run is recorded like the walk but leaves its joint limits
  and stretches its bones (see the report). Animals that hop (hare, rabbit) let their feet slide more (0.3 to 0.7 m/s).
- The pose of a walking animal does not blend with the walk of the clip it comes from beyond the 0.3 s fade.
- Herds: no flight, no predators, no drinking or resting clips; an animal can still clip a low branch.
- Drawing: no shadows of the animals, no instancing, no fur shells (the two fur models are plain).
- Prefab scales of the Unity scenes are not read: sizes are those of the model files.
