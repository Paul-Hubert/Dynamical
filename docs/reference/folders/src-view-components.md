# src/view/components

Plain data of the viewer: the window input of the frame, the game calendar, and the model an entity draws.

## Files

| File | What |
|---|---|
| [`input.h`](../../../src/view/components/input.h) | `Input` (context): keys as actions, mouse, text, window state of the frame |
| [`time.h`](../../../src/view/components/time.h) | `Time` (context): game seconds, speed, pause; `Date`: a time split into years, seasons, days, h:m:s; `dy::time_t` (uint64_t) |
| [`model_renderable.h`](../../../src/view/components/model_renderable.h) | `ModelRenderable` (component): the `ModelC` entity to draw at this entity's `Transform`, and a tint |

## Systems registered from here

None (Input, Time are in [`../systems/`](src-view-systems.md); `ModelRenderable`'s scene loader is in
`src/view/register_view.cpp`).

## Components and context variables defined here

- [Input](../components/Input.md) — written by [Input](../systems/Input.md) and [UI](../systems/UI.md), read by every
  desktop control system.
- [Time](../components/Time.md) — [Time](../systems/Time.md) only.
- [ModelRenderable](../components/ModelRenderable.md) — read by the model pass and the ray scene, made by many view
  systems and the `ModelRenderable` scene component.

## Dependencies

glm, EnTT, SDL2 (`SDL_TEXTINPUTEVENT_TEXT_SIZE`).

## Design docs

None of their own; CLAUDE.md (ECS, Models).
