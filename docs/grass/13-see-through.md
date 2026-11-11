# 13. Seen through where the ground falls away

**Goal.** Grass with nothing behind it (on a crest, at the edge of a step down) was a full surface up to its top: a smooth line against the sky. What is behind must show between the blades. Step 9 cut blades on the crests from the angle of the view on the ground alone, which cannot tell a crest from flat ground seen from far, so it was kept weak and faded out after a few metres.

## Design

**What the ray really does.** The lookup knows the ground as a plane under the pixel: a ray that enters the grass goes down for ever and always ends on a blade or on the ground. Over ground that bends down along the view, it does not. With `sink` the height the ray loses on the ground per metre of its way and `falling` how much the ground bends down along the view (per metre), the ray is

```
depth(l) = sink × l - falling × (flat × l)² / 2        flat : horizontal part of the view vector
```

under the top of the grass after `l`: `sink² / (2 × falling × flat²)` at its deepest, then it comes out of the grass again, with the air behind it. **What the lookup met deeper than that is not there**: the pixel is discarded, fully, and whatever is behind shows. On flat ground and in hollows `falling` is zero or less and nothing is cut: the ray does meet grass further on.

The cut is a depth under the shell, so it works across the layers, and it goes to zero where the ray only touches the grass: the blades thin out to their tips towards the line of the crest.

**How the ground bends** comes from the second derivatives of the terrain density, as the normal comes from the first. The ground is where the density is 0: its height `h(x, z)` is an implicit function, and

```
h_x  = -F_x / F_y
h_xx = -(F_xx + 2 F_xy h_x + F_yy h_x²) / F_y        (xz and zz alike)
```

from the differences of the 18 samples around a sample (`groundCurve`, `src/logic/terrain/ground_curve.h`). The marching cubes shader computes it at both ends of an edge and gives `(h_xx, h_xz, h_zz)` to every vertex; the grass shader takes it along the view: `falling = -(h_xx tx² + 2 h_xz tx tz + h_zz tz²)`. Per vertex and not from screen derivatives of the normal: those are constant over a triangle and wrong where triangles are seen edge on, which is where the cut happens.

**Vertex.** The terrain vertex stays 32 bytes: the normal is octahedral now (2 floats, decoded in `terrain.vert.glsl` and `grass.vert.glsl`), and the texture coordinates nothing read are gone: position, normal, curve.

## Settings

`silhouette` ("See through on crests" in the debug menu, 0 to 3, default 1): 0 cuts nothing, 1 cuts what is under the deepest point of the ray, more cuts higher. The rule of step 9 is gone.

## Check

`GRASS.10`: `groundCurve` on a sphere (top and side), a hollow, a plane and a wall; the cut depth against the deepest point of the ray stepped along its way. Captures `grass_crest` (a crest a few metres away) and `grass_far` of `tests/capture/grass.capture.json`. A debug picture of `falling` over the hills: crests and hollows come out smooth, without the triangles.

## Result

Done. On a crest a few metres away blades stand one by one against what is behind. Further away (texels are means of blades) the cut is a line a little under the top of the grass, ragged by a few pixels.

- A first version let the ray see `sqrt(16 / 3 × fall / sink)` times deeper, for the way the real ray stays in the grass around its deepest point (as much to meet as the straight ray of the lookup had met that much deeper, with blades thinner towards their tips). It is closer to what dense grass 1 m high does and left only a few pixels cut: dropped, `silhouette` under 1 gives it back.
- On a hill of 15 m of radius the cut starts 12° before the line of the crest.
- Where the real ground is still under the ray although the parabola says it is not, the dark earth of the terrain pass shows between the blades.
- Not done: nothing is cut for an object standing behind the grass on flat ground (a blade there hides it as before), and the cut does not know the shell of the grass changes height.
