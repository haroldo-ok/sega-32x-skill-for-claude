# 2D sprite games & shmups on the 32X

For a 2D game (a shooter, a puzzle game, an arcade port) you don't need the 3D
engine or the voxel renderer — just the packed 8bpp framebuffer, a few shape
primitives, and the same HAL-free-core discipline. Reference project: `shmup32x`
(a faithful clean-room port of an HTML5 vertical shooter, M1–M5 + a
faithful-graphics pass).

## Shape primitives (add to gfx.c)

`GFX_FillRect` alone forces everything into rectangles. Three scanline fills
cover almost all 2D sprite art and cost little. They write the packed 8bpp back
buffer (`Mars_BackBufferPixels()`, stride `SCREEN_W`) exactly like `FillRect`:

```c
static void hspan(int x0,int x1,int y,u8 c){            /* clipped horizontal run */
    if (y<0||y>=SCREEN_H) return;
    if (x0>x1){int t=x0;x0=x1;x1=t;}
    if (x0<0)x0=0; if (x1>=SCREEN_W)x1=SCREEN_W-1; if (x0>x1) return;
    u8 *d=(u8*)Mars_BackBufferPixels()+y*SCREEN_W+x0; for(int x=x0;x<=x1;x++)*d++=c;
}
void GFX_FillTri(int x0,int y0,int x1,int y1,int x2,int y2,u8 c);  /* scanline, 3 edges */
void GFX_FillCircle(int cx,int cy,int r,u8 c);                     /* per-row integer sqrt */
void GFX_FillPoly(int cx,int cy,const signed char *pts,int n,int num,int den,u8 c);
                                              /* triangle-fan from (cx,cy) over scaled pts */
```

`GFX_FillPoly` fans triangles from a centre point over a list of relative vertex
pairs scaled by `num/den` — good for centre-visible ship/enemy shapes. Full
implementations are in `assets/2d/gfx_shapes.c`.

## Faithful-graphics reconstruction from source

When the ask is "make it look like the original" (and you have the original's
source — an HTML5 canvas game, a PICO-8 cart, etc.), **read the original's draw
code and reproduce its exact shapes and colours**, don't approximate with
rectangles:

1. Grep the source for every `fillStyle`/colour literal → build a matching
   palette with the *exact* hex values (`CRAM(r,g,b)` from the `#rrggbb`).
2. Grep for the draw functions; extract each entity's **polygon vertices** (the
   `moveTo/lineTo` path or point list) and shape kind (triangle, pentagon,
   circle, rect).
3. Reproduce with `GFX_FillPoly`/`FillTri`/`FillCircle` at a scale that fits the
   hitbox (the original's ~40px ship on 600px canvas ≈ ~20px on 224px).
4. Match per-entity details: cockpit accents, gun barrels, HP-bar colours,
   laser core stripes, radial-burst tables.

This turned the shmup's placeholder rectangles into the original's four distinct
ships, typed enemy shapes, and winged boss — a large, cheap fidelity jump. See
the `draw_ship/draw_enemy/draw_boss` helpers in `shmup32x/src/main.c`.

## Menu / game-flow state machine

Arcade games need title → select → play → game-over flow. Keep a single `flow`
enum in `main` and gate update + draw on it:

```c
int flow = TITLE;                 /* TITLE, SHIPSEL, DIFF, PLAY, OVER */
int navEdge = nav && !prevNav; prevNav = nav;   /* rising-edge for menu nav */
switch (flow) { ... }             /* one screen's logic per case */
```

- Detect menu navigation on the **rising edge** of a button, not the level, or
  one press advances many screens.
- Carry selections into gameplay (ship stats, difficulty) via `reset(difficulty, ship)`.
- **Menu screens are sparse and will trip the black-screen colour guard.** Give
  them real colour (a ship icon, coloured difficulty labels) or the automated
  test flags them as blank. This is a feature of the guard, not a bug — sparse
  menus and dead frames look identical to it.
- When you add a menu, the game no longer boots straight into gameplay: **update
  every input script** to navigate the menu first (`press start`, `press a`),
  and update `verify_rom.py --marker` strings to text that's actually live.

## Event-driven sound keeps game logic HAL-free

Don't call the PWM sound API from inside the portable game module. Detect events
in `main` by diffing state across the update, and trigger SFX there:

```c
int pre_hp = lives+shield, pre_parts = particles_active(), pre_cd = cooldown;
game_update(&in);
if (in.fire && pre_cd==0)               snd_fire();   /* a shot went out */
if (particles_active() > pre_parts)     snd_boom();   /* something exploded */
if (lives+shield < pre_hp)              snd_hit();    /* took damage */
```

The game module stays pure and host-testable; only `main` knows about the PWM.
See `audio.md` for the mixer and the honest note that video test harnesses can't
confirm audio output.

## Reset must fully clear entity pools

A subtle, real bug from `shmup32x`: `reset()` cleared only the `alive` flags of
enemy/bullet arrays, not the whole structs. A reused slot then inherited a stale
velocity/position; and a later-added `boss_active` field wasn't reset at all, so
restart left a ghost boss. **Zero the entire entity arrays on reset**, and
initialise every new field you add. Test restarts, not just first-run.

## Verification specifics

- Give each object a **distinct palette colour** and detect it by that colour in
  captured frames (see `testing.md` — and the false-positive warning: a bullet
  yellow that's near your sand terrain colour will read as "present" when it
  isn't; use a colour no terrain uses and a tight threshold).
- Objects appear where the projection puts them: fast projectiles are large near
  the player and shrink to ~1px toward the horizon, so a still frame shows one
  bright near shot, not a long tracer. Scan the whole path, and don't expect a
  "stream".

## Split-screen two-player

Two-player split-screen (a shipped racer in the `dmar` collection) is just the
render pass run twice with an independent camera and HUD per player into two
viewport halves of the one 320×224 framebuffer:

- Give each player its own camera/state; render viewport A (top/left half) then
  viewport B, each **clipped to its half** (clip spans/blits to the sub-rect —
  don't let one half's draws bleed into the other).
- Halve the vertical (or horizontal) resolution per view; the dirty-rect and
  cached-background tricks in `optimization.md` apply per half.
- One HUD per player, positioned in its own viewport. Watch the fill budget: two
  passes doubles pixel writes, so the 60/n vblank math (`optimization.md`) is
  tighter — profile with `fillcount`.
