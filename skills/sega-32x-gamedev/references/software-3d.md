# Software 3D on the 32X

The 32X has **no GPU and no FPU**. "3D" means a real polygon pipeline written
in **fixed-point** and rasterized in software on the SH-2(s) — the same math a
GL/WebGL scene uses, minus the hardware. This is exactly how the 32X shipped 3D
(Virtua Racing-style). It is very achievable and looks like a low-poly game;
what is *not* achievable is faithful textured/high-poly GPU 3D or copying a
GPU game's original models. Be honest about that boundary up front.

A proven, reusable implementation of everything below ships in this skill:
`assets/r3d.h`, `assets/r3d.c`, and `assets/gen_tables.py`. Two full games were
built on it (a rally racer and a rail shooter), so start there rather than from
scratch.

## Fixed-point

Use **16.16** (`typedef int fx; #define FX(n) ((n)*65536)`). Multiply via a
64-bit intermediate: `fmul(a,b) = ((long long)a*b) >> 16`. Everything — world
coords, camera, velocities — is `fx`. Screen coords are plain `int`.

Trig comes from a **256-entry sine table** (angles 0..255 = 0..2π), generated at
build time (`gen_tables.py`) as an `.inc` of 16.16 values. `cos(a)=sin(a+64)`.
Generating tables at build time and `#include`-ing them keeps them in ROM and
avoids any runtime float.

## The pipeline (per object, per frame)

1. **Model → world**: rotate each vertex by the object's yaw (sine table) and
   translate to its world position.
2. **World → camera**: subtract camera position, rotate by `-camera.yaw`.
3. **Project**: perspective divide. `sx = W/2 + (x*focal)/z`,
   `sy = H/2 - (y*focal)/z`, with `focal` ~140–160px. **Reject z < ~0.25** (at
   or behind the camera) before dividing.
4. **Rasterize**: flat-shaded triangle fill into the 8bpp back buffer.
5. **Sort**: painter's algorithm — sort faces back-to-front by average vertex
   camera-z. No z-buffer (saves the RAM and the per-pixel compare); fine for
   convex-ish meshes and separated objects.

## The two things that make it fast enough

- **No runtime divide.** The SH-2's divide is slow, and naive projection does a
  divide per vertex. Replace it with a **reciprocal table**: precompute
  `recip[k] ≈ (1<<SH)/k` for `k = z>>12`, then `screen = (coord*focal*recip)>>(SH+12)`.
  Accurate to ~1px vs the exact divide. See `optimization.md`.
- **Offload to the second SH-2.** The slave core is idle by default; at minimum
  give it the full-screen framebuffer clear each frame. See `architecture.md`
  (dual-core) and `optimization.md`.

## Rasterizer

A standard scanline fill: sort the 3 points by y, walk scanlines, interpolate
the left/right x on the long edge and the two short edges, fill the span with
the flat color byte. Keep it HAL-free by writing into a caller-supplied
`{u8 *px; int w,h;}` descriptor — then the **same rasterizer host-tests** (fill
a triangle in a malloc'd buffer, assert area + an interior pixel) and runs on
the 32X back buffer unchanged. Clip x/y to the buffer as you go.

## Mesh format

Keep it trivial and `const` (lands in ROM):
```c
typedef struct { fx x,y,z; } vec3;
typedef struct { int a,b,c; unsigned char color; } tri_t;   /* color = palette index */
typedef struct { const vec3 *verts; int nverts; const tri_t *tris; int ntris; } mesh_t;
```
Author meshes as clean-room low-poly (a car ≈ 16 verts / 20 tris; an enemy ≈ 6
verts / 8 tris is plenty). Give distinct objects **distinct palette colors** —
it makes them readable *and* makes pixel-based emulator tests unambiguous (see
testing.md).

## Motion & camera

- A **chase camera** = place the camera behind the player object along its
  heading: `cam.pos = obj.pos - heading*dist`, `cam.yaw = obj.yaw`.
- Convey speed cheaply with **scrolling ground detail** (transverse grid lines
  or road dashes whose world-z scrolls with distance) — a flat single-color
  ground shows no motion.
- A curved track/rail = a centreline of nodes `{x,y,z,yaw}` expanded from a
  segment list `{curve, slope, len}`; render the ribbon as two triangles per
  segment between left/right edge points (centre ± perpendicular*half_width).

## Verifying 3D in the emulator

Flat-shaded scenes use **few colors** — lower the black-screen guard's color
threshold (see testing.md). Verify structure numerically: sky fills the top,
the ground/road ribbon is present, the player object is centre-screen. Verify
"real 3D" by checking an object's on-screen **size grows as its z decreases**
(approaching) — that's the unfakeable signature of perspective, not sprite
scaling.

## Coordinate-convention gotcha (porting a 3D game)

When you port a 3D/pseudo-3D game, the source may use a different **handedness or
an inverted world-Y** than your renderer. Symptom: everything draws, but motion
is mirrored — steering goes the wrong way, or driving "forward" makes scenery
*recede* instead of approach, or objects face away from the camera. The fix is a
sign flip when reconstructing heading/position, e.g. building the heading from
`atan2(dx, -dy)` instead of `(dx, dy)` (this is exactly the correction Speed
Haste 32X applies to the original DOS heading math). If your first 3D port looks
"inside-out" or mirrored, suspect an axis-sign/handedness mismatch before you
suspect the projection math. Verify with the size-grows-as-it-approaches check
(above): if approaching objects *shrink*, your forward axis is flipped.

## Other 3D/pseudo-3D rendering modes on the 32X

Filled-triangle painter's 3D (above) is one option. Two others are proven on
real 32X hardware and are often cheaper for specific genres:

- **Wireframe / vector.** Project vertices as usual, then draw each edge with a
  **Bresenham line** routine instead of filling faces — no rasterizer, no depth
  sort. Ideal for vector games and debug overlays. See `wirefight-32x`
  (`gfx_line`) and `xquest-32x`. A whole game can run on just `plot()` +
  `gfx_line()` into the 8bpp framebuffer.

- **Mode-7 / perspective floor (scanline sampling).** For a ground-plane racer,
  don't build floor polygons — sample a tilemap **per scanline**. Precompute a
  `z_per_scanline[]` depth table (the ground distance for each screen row from
  the horizon down) so the inner loop has **no divide**; step a fixed-point
  (u,v) across the row and look up the tile pixel with shifts + a LUT. Draw
  objects as **scaled sprites**. This is the classic Mode-7/OutRun approach —
  see `apex-vector-60-32x` (a `z_fov_table[128]`, inner loop reduced to a few
  shifts + two lookups) and `cannonball-outrun-32x` (segmented road with forks
  and per-stage themes). Pseudo-3D roads are often far cheaper than polygons and
  look great for racers.

Pick per genre: polygons for free-camera 3D (racers with real geometry, rail
shooters), Mode-7/road for ground racers, wireframe for vector games.
