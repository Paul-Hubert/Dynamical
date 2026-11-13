# Lights

`Lights` in [`src/renderer/lighting/lights.cpp`](../../../src/renderer/lighting/lights.cpp) · Scope: Host · Needs renderer: yes
· Lanes: none (owned by the `Renderer`, always there) · Parameters: no

Fills the `LightsUBO` of the frame (set 0 binding 3 of every scene pipeline, `Lights` of
`include/scene.glsl`): the sun or the moon (the one directional light), the sky colours and the ambient
light from the `Sky` context variable (`skyLight`), the light of the open sky as a probe stores it, and the
8 `PointLight`s nearest to a viewer. One buffer per frame in flight, the same for both targets. The file also
holds `lightingView()`, the `ViewUBO::lighting` of a target from its lighting tier.

## Behaviour

**Constructor**: emplaces a default `Sky` when the scene has none (no `"Sky"` system: the sun stays where it
always was); makes sure the `PointLight` storage exists; a mapped uniform buffer per frame in flight, given
to `Context::lights_buffers` (written into the view sets by `Context::writeViewSet`).

**update(frame_index, time)** (main thread, first thing of `Renderer::render`):

1. `skyLight(Sky)`: direction, disc radius and colour of the light (sun or moon), zenith, horizon, night,
   glow, sun and moon discs, ambient sky and ground.
2. `frame`: frames drawn so far (mod 65536) and `time` (seconds since the renderer started, or the
   `FrameClock` in deterministic runs, given by the `Renderer`).
3. `sky_light`: 256 Fibonacci directions summed into a `ProbeLight` (sky radiance above, ground light below).
4. The viewers (`Camera`; the headset from `VRRig` x `VRInput::head` when its session runs); every
   `Transform` + `PointLight` with intensity and radius > 0 sorted by distance to the nearest viewer minus
   its radius; the first 8 written (position + radius, colour x intensity), their count in
   `ambient_ground.w`.
5. Written into this frame's buffer; `current()` keeps a copy.

**lightingView(reg, target)** (free function, called by `ClassicRender` / `VRRender::updateView`):
`(traced_range, sun_rays, shadowed_torches, 0)` of `RenderSettings::lighting[target]`, zero without settings.

**Destructor**: clears `Context::lights_buffers`.

## Parameters

None.

## Components and context

| Name | Kind | Access | Where | What for |
|---|---|---|---|---|
| Sky | context | read+create | constructor (emplace when absent), update | sun, moon and sky of the time of day |
| PointLight | component | read | update | torches: colour, intensity, radius |
| Transform | component | read | update | torch positions |
| Camera | context | read | update | viewer |
| VRInput | context | read | update | headset running, head position |
| VRRig | context | read | update | rig transform |
| RenderSettings | context | read | lightingView | the target's lighting tier |

## Side effects

- Writes the mapped lights uniform buffer of the frame; sets `Context::lights_buffers`.

## Order and dependencies

- After `SkySys` (`"Sky"`, writes `Sky`) and `TorchSys` (`"Torch"`, moves the torches): systems tick before `Renderer::render`.
- Before every pass records (the buffer of the frame is read by all scene shaders and by `SkyPass`).

## Used in scenes
<!-- generated -->

## Tests

- `LIGHT.1` (a scene without a Sky system), `LIGHT.2`, `LIGHT.3`, `LIGHT.5` (the probe light of the open sky) in [`tests/unit/lighting.cpp`](../../../tests/unit/lighting.cpp). Shots by hand: `tests/capture/lighting_torch.capture.json`.
