# 9. Ragged silhouette (idea 13)

**Goal.** On a crest the grass ends with the shell: a smooth line against the sky, a hard step in front of what is behind. It should end in blade tips.

## Design

- A pixel is on a silhouette when the shell is seen edge on: `edge = 1 - smoothstep(0.03, 0.14, -dot(view, flat normal))`, with the normal of the triangle (the same one that keeps the hit over the ground).
- There, a ray that enters the top of the grass and meets nothing high in its first tile really goes on over the crest. The pixel is discarded when `baked < edge × silhouette`: at the very edge only the tall parts of the blades stay, lower down everything stays.
- Restricted to the edge: on flat ground seen from far the same test would open holes onto the bare terrain (the ray does meet grass further on, which one lookup cannot know).
- Mips: far away `baked` tends to its mean (about 0.5), so the cut becomes a line again, a little lower, without sparkle.

## Settings

`silhouette` (0 to 1, default 0.75): how much of the blades is cut at the very edge. 0 turns it off.

## Check

Eye and low cameras: tips against the sky on the crests and in front of the far hills. No holes in the field itself.

## Result

Done, weaker than planned.

- The triangle normal cannot be the edge test: terrain triangles are 4 m wide, a whole triangle turned edge on at once and was cut as one dark wedge. The smooth normal is used.
- The cut fades out between mip 0.5 and 2. Further out (tried up to mip 3.5) texels are means of blades and the cut opens holes onto the far hills and the dark terrain instead of drawing tips.

Seen: blade tips on a crest a few metres from a camera near the ground (`grass_skim`). Crests further than about 10 m keep their smooth line: for those the shell itself would have to be ragged.
