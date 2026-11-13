# model

`ModelPass` in [`src/renderer/passes/model_pass.cpp`](../../../src/renderer/passes/model_pass.cpp) · Scope: Host · Needs renderer: yes
· Lanes: `renderer.passes` (`"model"`) · Parameters: no

Draws every entity with a `Transform` and a `ModelRenderable`: the glTF models of the `ModelManager`
(`ModelC` entities, one vertex and index buffer each, node transforms baked in), one textured material per
part, tinted by the renderable. With ray tracing it gives each model's mesh to the rays (`RayMeshes`) so
that models cast shadows and are seen by the probes.

## Behaviour

**Constructor**: ensures the `ModelC`, `ModelRenderable` and `Transform` storages exist; a descriptor pool of
1024 sets (free allowed), the material set layout (**set 1**: binding 0 the base colour texture, fragment),
a repeating linear sampler, a 1x1 white texture, the layout (set 0 view, set 1 material, push constants
`ModelPC`: model matrix, colour; vertex + fragment).

**createPipeline(target, renderpass)**: `model.vert.glsl.spv` + `sceneFragmentShader(ctx, "model")`,
`ModelVertex` input (position, normal, uv), back face culled.

**update(frame_index)** (main thread): for every `ModelC` entity:
- each material without a set gets one (`makeMaterialSet`: one image view per texture, shared, white when
  the material has no image);
- with `RayMeshes` and a model not given yet (`ray_mesh < 0`) that has buffers: a `RayMesh` of kind
  `Solid` over the whole vertex buffer, a part per mesh part coloured by its material's base colour; its
  index goes into `ModelC::ray_mesh`.

**record(command, view)** (both targets concurrently; reads only): returns when `RenderSettings::models` is
false; binds the pipeline and set 0; for every `Transform` + `ModelRenderable` whose `model` entity has a
`ModelC` with buffers: binds them, and per part with a material set pushes `ModelPC` (transform matrix,
`base_color x tint`), binds set 1 and draws indexed.

**Destructor**: pipelines, views, layout, sampler, set layout, pool. (`Renderer::finish` clears the
`ModelC` storage and erases the `ModelManager` before the device goes.)

## Parameters

None from the scene. Settings read: `RenderSettings::models` (bool, true): false draws no model.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| ModelC | component | readwrite | update, record | material sets and `ray_mesh` written in update; buffers, parts, materials read in record |
| ModelRenderable | component | read | record | the model entity and the tint |
| Transform | component | read | record | model matrix |
| RayMeshes | context | readwrite | update | adds each model's mesh to the rays (ray tracing only) |
| RenderSettings | context | read | record | `models` |

## Side effects

- Allocates a descriptor set per material and an image view per texture as models load.
- With ray tracing, gives one mesh per model to the rays (`RayScene` builds its structure).
- Records the indexed draws of every model part into both targets.

## Order and dependencies

- `ModelManager` (context variable, emplaced by the `Renderer`) creates the `ModelC` entities when a scene or system loads a model; this pass's `update` gives them their sets before the first frame they are recorded.
- `RayScene::gatherInstances` reads `ModelC::ray_mesh`, written here, and places models that move (`RigidBody` not static) as dynamic instances.

## Used in scenes
<!-- generated -->

## Tests

- Visual: `V0.1`, `V0.2`, `V1.1`, `V2.2` (scene `base`), `V2.1` (`worlds_boxes.capture.json`, scene `test_boxes_view`: the boxes are models), `P1 check` (throws a box).
