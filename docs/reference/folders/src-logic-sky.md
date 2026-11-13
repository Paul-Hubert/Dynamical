# src/logic/sky

The time of the day and what lights the world at that time: the sun, the moon, the colour of the sky and the
ambient light ([lighting](../../lighting.md)), and the scene's torches. The CPU side of what
`shaders/include/view.glsl` mirrors (`skyColor`, `skyRadiance`).

## Files

| File | What |
|---|---|
| [`sky.h`](../../../src/logic/sky/sky.h) | `Sky` (context: time, day length, latitude, declination), `SkyLight` (sun and moon directions, the one directional light, sky colours, glow, discs, ambient), `sky_light_scale`, `sunDirection`, `moonDirection`, `skyLight`, `skyColor`, `skyRadiance`, the system classes |
| [`sky.cpp`](../../../src/logic/sky/sky.cpp) | the celestial positions (hour angle, declination, latitude; east +X, north -Z), the sun through the air (Kasten and Young air mass), the moon taking over at night, the sky's colours and its light on a surface facing up (integrated over 12 x 24 directions); systems Sky and Torch, `registerSky` |

## Systems

- [Sky](../systems/Sky.md): sets the Sky of the scene and moves its time
- [Torch](../systems/Torch.md): a point light, carried in front of the camera or left in place

## Components and context variables

- [Sky](../components/Sky.md): host context, the time of the day and the place

Touched from elsewhere: [PointLight](../components/PointLight.md) (`src/logic/components/point_light.h`),
[Transform](../components/Transform.md), [Camera](../components/Camera.md).

## Worth knowing

- One directional light at a time: the brighter of the sun and the moon (`SkyLight::is_sun`); its strength goes
  through zero when one takes over, so the shadows never jump.
- Without the Sky system the renderer's `Lights` emplaces a default Sky (10.3349 h, stopped): the sun stays where
  the shaders had it, `normalize(0.5, 1, 0.5)`.
- Fixed-clock runs (captures, probes) stop the sun unless the scene gives a `day_length`.

## Dependencies

- `src/logic/components/` (Camera, PointLight, Transform), `src/logic/systems/` (System), `src/scene/` (RunArgs,
  `registerSystem`).
- Used by `src/renderer/lighting/lights.cpp` (the frame's lights), `src/renderer/tree_shade.cpp`, the sky pass, the
  debug menu (`src/view/systems/`), `tests/unit/lighting.cpp`.

## Design docs

- [docs/lighting.md](../../lighting.md), [docs/lighting/built.md](../../lighting/built.md)
