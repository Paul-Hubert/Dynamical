# 7. Sun shadows between blades (idea 11)

**Goal.** Light only depends on the normal and on the height in the grass. A blade in the shadow of the next one is what gives a field its contrast in the sun.

## Design

The oblique texture answers "what does a ray going down from this side meet first". Asked for the sun's ray through the hit point, it says whether the sun sees that point.

1. The hit point in the winning layer's tile space: the baked hit is `0.5 - baked` tiles after the middle, at height `baked`.
2. The sun ray through it, in the same tile space (same shear and layer turn): azimuth and reach `rs` (the sun is at 55° over the horizon; in tile space with 0.6 m of grass on 1 m tiles, steeper).
3. Same lookup as for the view: where that ray crosses the middle, the oblique texel of the sun's azimuth, carried to the sun ray: `seen = 0.5 - (0.5 - b) / rs`.
4. The point is lit when the sun's first hit is the point itself. Shadow = `smoothstep(0.06, 0.3, seen - h)`: the texture is only good to about 0.1 of the height, so the edge is soft and a hit close to its own height stays lit.

Only the layer of the hit is asked: a blade of another layer casts no shadow.

## Settings

`shadow` (0 to 1, default 0.6): how dark the shadow is.

## Check

45° and top cameras: darker bands behind the tufts, on the side away from the sun (the sun comes from +x +z). No speckle on the lit side of single blades.

## Result

Done, but not with one fetch: three.

In tile space the sun is steep: its reach is 0.42 tiles for 0.6 m of grass on 1 m tiles, 0.23 for the 0.33 m of the capture spot, far from the 1 of the baked rays. Carrying the oblique hit to such a ray saturates. So the shadow blends two answers by the steepness of the sun in tile space, like the view lookup does:

- slanted: the oblique texel of the sun's azimuth, as designed (reach not under 0.3);
- from above: two samples of the top texture along the sun ray above the hit; the canopy must stay under the ray.

It fades out between mip 2 and 4.

Seen: darker gaps between the tufts from the top and at 45°, no speckle on lit blades. Subtle with short grass and this high sun; longer grass or a lower sun was not tried.
