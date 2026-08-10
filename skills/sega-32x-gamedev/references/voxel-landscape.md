# Voxel-landscape rendering (Comanche-style) on the 32X

A third rendering family, distinct from the flat-polygon 3D engine
(`software-3d.md`) and 2D sprites (`2d-and-shmup.md`): a **height-mapped voxel
landscape** that scrolls toward the camera, as in a Comanche-style terrain flyer
or a "2d voxel shmup" (the technique behind REZ's *Zepton*, ported in
`zepton32x`).

The landscape is **not** a polygon mesh. It is a grid of terrain cells, each
with a height and a colour, drawn as **perspective-scaled rectangles**. There
are two ways to draw it; pick based on where your frame budget goes (measure —
see below).

## The world model

- A grid of `NX` columns × `NZ` depth slices. Column spacing `CELL`, depth
  spacing `CELLZ`. The nearest slice sits at `NEAR` world units ahead.
- Terrain height `h(col, worldz)` is procedural: a sum of a few
  `r3d_sin/r3d_cos` waves at different frequencies, plus features (a central
  river valley, a plateau, a volcano) and **quantised into steps** for the
  blocky voxel look: `h = (h / QSTEP) * QSTEP`.
- Height → colour by band (water → sand → grass → rock → snow).
- The camera sits **above** the terrain (`CAMY`) looking forward and slightly
  down. Forward motion = advance a `scroll` accumulator; `worldz = slice + (scroll>>16)`
  and a sub-slice fraction shifts depth smoothly.

Coordinate/scale sanity: choose `NEAR`, `CELL`, and `FOCAL` so that at the
nearest slice the `NX` columns roughly fill the 320px width. If you put the near
plane at `z≈1` you will project everything thousands of pixels off-screen and
get a black frame. Rule of thumb: near cell screen width `≈ CELL*FOCAL/NEAR`
should be ~10px, so with `CELL=1, FOCAL=120` the near plane wants `NEAR≈12`.

## Projection (shared by both methods)

Camera-relative perspective divide, one per point:

```c
/* wz>0 required; reject points at/behind the camera */
int zz = wz >> 8;                 /* wz is 16.16; work in 8.8 for the divide */
fx  dy = CAMY - wy;               /* camera above terrain -> dy>0 -> below horizon */
sx   = 160 + ((wx>>8) * FOCAL) / zz;
sy   = HORIZON + ((dy>>8) * FOCAL) / zz;
size = ((CELL>>8) * FOCAL) / zz;  /* nearer = bigger */
```

Watch 32-bit overflow on the sub-slice scroll term: `frac * CELLZ` with
`frac` up to 65535 and `CELLZ≈98304` overflows `int`; compute it as
`(fx)(((long long)frac * CELLZ) >> 16)`.

### Hoist the divide out of the column loop (big win)

Every cell in a depth slice shares the same `wz`, hence the same `FOCAL/zz`.
Compute a reciprocal factor **once per slice**, then place each cell with
multiplies — no per-cell divide:

```c
int rf = (FOCAL << 12) / zz;                 /* ONE divide per slice, 12.12 */
/* per cell: */
sx   = 160 + (((wx>>8) * rf) >> 12);
sy   = HORIZON + (((dy>>8) * rf) >> 12);
size =           (((CELL>>8) * rf) >> 12);   /* constant per slice -> hoist too */
```

This turns ~`NX*NZ*3` hardware divides into ~`NZ` divides + multiplies. Keep the
divide version around as a reference and unit-test that the fast path matches it
within 1px across sampled `(wz, col)`.

## Method A — per-cell billboards (recommended default)

Draw far-to-near (painter's order); nearer cells overdraw farther ones:

```c
for (slice = NZ-1; slice >= 0; slice--) {
    fx wz = NEAR + slice*CELLZ - subslice_scroll;
    int rf = vox_recip(wz); if (!rf) continue;
    int size = ((CELL>>8)*rf) >> 12; if (size<1) size=1;
    int w = size+1, ht = size*2+2, half = w/2;       /* column height: see overdraw */
    for (col = 0; col < NX; col++) {
        fx h = vox_height(col, worldz);
        fx wx = (col - NX/2)*CELL;
        int sx = 160 + (((wx>>8)*rf)>>12);
        int sy = HORIZON + ((((CAMY-h)>>8)*rf)>>12);
        GFX_FillRect(sx-half, sy, w, ht, band_col[vox_color(h)]);
        if (size>=2) GFX_FillRect(sx-half, sy, w, 1, C_WHITE);  /* lit ridge */
    }
}
```

- **Samples the heightmap once per cell** (~`NX*NZ` ≈ 900 for 32×28). Cheap
  compute.
- Needs a **full-screen clear** each frame (offload it to the slave SH-2 via a
  COMM job — see `architecture.md`) because the cells don't tile the screen.
- **Overdraw is the cost.** Tall columns (`ht = size*3`) look solid but pile up
  fillrate on the near slices. `ht = size*2` is a good balance; measure.

## Method B — per-screen-column raycaster (classic Comanche)

For each screen column, march front-to-back with a **y-buffer** (`ybuf`), draw
one vertical span whenever the terrain rises above what's already drawn. Each
screen column is painted exactly **once** — zero overdraw, and only the sky band
needs clearing:

```c
GFX_FillRect(0,0,SCREEN_W,HORIZON+2,C_SKY);          /* sky only */
for (sx = 0; sx < SCREEN_W; sx += 2) {               /* 2px columns */
    int ybuf = SCREEN_H;
    for (slice = 0; slice < NZ; slice++) {           /* near -> far */
        fx wz = NEAR + slice*CELLZ - subslice_scroll;
        int rf = vox_recip(wz); if (!rf) continue;
        int wxcol = ((sx-160) * (wz/FOCAL)) >> shift; /* inverse project */
        fx h = vox_height_wx(wxcol, worldz);          /* river centred at wx=0 */
        int sy = HORIZON + ((((CAMY-h)>>8)*rf)>>12);
        if (sy < ybuf) { GFX_FillRect(sx, sy, 2, ybuf-sy, band_col[vox_color(h)]); ybuf = sy; }
    }
}
```

- **No overdraw, no full clear** — wins when fillrate dominates.
- BUT it **samples the heightmap far more often** (`SCREEN_W/step * NZ`, e.g.
  160×28 ≈ 4480) than Method A. If your height function does trig per sample,
  the raycaster becomes **compute-bound and can be *slower* than Method A**.

### Measured result from Zepton (why the default is Method A)

For Zepton's procedural terrain (2×sin + 1×cos per sample), a straight port
measured **Method B ≈ 8 fps vs Method A ≈ 19 fps** — the raycaster's ~4480
trig-heavy samples/frame swamped the fillrate it saved. Method A was kept.

**The fix that would make B win** (left as an avenue): precompute the frame's
heightmap grid **once** (~900 trig samples) into an array, then have the
raycaster read/interpolate from that array instead of recomputing trig per
screen-column sample. That makes B fillrate-bound (its strength) with A's sample
count. Do this only if profiling says fillrate is the wall.

**Lesson:** raycast vs billboard is a compute-vs-fillrate trade. Measure both on
*your* terrain before committing — see `optimization.md` for the on-screen
frame-counter measurement trick.

## Entities over the landscape (Zepton shmup layer)

- **Into-the-screen projectiles**: model a projectile as a screen-space path
  from the ship to the reticle with a progress `p:0→1`; position is
  `lerp(launch, target, p)` and size shrinks with `p`. Bullets fly straight to
  where the reticle was; homing missiles re-blend their target toward the live
  reticle each frame (`tx += (reticle-tx)>>3`). See `weapons.c` in zepton32x.
- **Approaching enemies**: same progress model in reverse — spawn at the horizon
  (`p=0`, tiny, centred), grow and fan out toward the player (`p=1`). Screen
  `x = 160 + lane*SPREAD*p`, `y = HORIZON + (PLAYER_Y-HORIZON)*p`, `size ∝ p`.
- **Lock-on reticle**: the reticle is "locked" (draw it red) when it overlaps a
  live enemy; a projectile that overlaps an enemy destroys it.
- These are all HAL-free and host-testable (spawn, approach, lock, hit, kill,
  reached-player). Keep the rules in a module and only draw in `main`.
