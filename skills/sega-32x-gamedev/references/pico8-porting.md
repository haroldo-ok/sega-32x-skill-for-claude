# Porting PICO-8 games to the 32X

A large share of good 32X port targets are **PICO-8** carts (a Mode-7 racer, a
3D fighter, a SUPERHOT homage, a raycaster dungeon, a city-builder sim — all
shipped by haroldo-ok). They share enough that a **reusable PICO-8 compatibility
layer** pays for itself immediately. Do **not** embed a PICO-8 interpreter;
decode the cart and reimplement its logic in native C driving the 32X hardware.

Cite the original author/cart and honour its licence (see `porting-workflow.md`;
PICO-8 games are frequently GPL code + CC-BY-NC assets).

## Get the cart's data out

A `.p8` cart is text with `__lua__`, `__gfx__`, `__map__`, `__gff__`, `__sfx__`,
`__music__` sections. The binary-ish sections aren't UTF-8; read them safely:

```sh
iconv -f utf-8 -t utf-8 -c cart.p8 | sed -n '/^__lua__/,/^__gfx__/p'   # the code
```

Write a converter (`tools/gen_*_assets.py`) that reads the **unmodified**
original and emits C arrays: `gfx_data[128*128]` (sprite sheet, 1 byte/pixel),
`map_data[128*32..64]` (tilemap), `gff_data[256]` (sprite flags), and any
authored data tables (keyframes, level defs, SFX rows). Converting the real data
— not hand-redrawn approximations — is what makes the port faithful; e.g. a
fighter port pulled all 117 keyframes, the 18-point skeleton and 13 body
segments straight from the cart's Lua tables.

## The `pico8_api` compatibility layer

Put PICO-8's runtime surface behind one small module (`pico8_api.h/.c`) so the
game logic reads almost like the Lua. Recurring pieces:

- **Palette → CRAM.** Upload PICO-8's fixed 16-colour RGB555 palette to 32X CRAM
  once via `Mars_SetPalette`. `pico_pal()` implements palette swaps; colour `0`
  is transparent for sprites.
- **`pico_spr` / `pico_sspr`** — blit / scaled-blit a sprite from `gfx_data`,
  with transparent-0 clipping. Scaling is the hot path — see the optimization
  below.
- **`pico_print` / `pico_print_centered`** — a bitmap ASCII font (PICO-8 is
  5×5-ish); centre helper for menus/HUD.
- **`pico_btn(b)`** — read the masked 3-button pad and map to PICO-8's button
  indices. **Angle/atan2 conventions differ and cause "everything faces
  backward" bugs**: in PICO-8 angle `0.0` = right, `0.25` = **up** (`dy<0`),
  `0.5` = left, `0.75` = down. Implement `pico_atan2(dx,dy)` and `pico_sin/cos`
  to match, or cars/players spawn 180° wrong.
- **`pico_present`** — copy PICO-8's logical surface to the 32X framebuffer.

## Resolution: 128×128 (or 160×112) → 320×224

PICO-8 renders 128×128. Two common strategies:

- **Native re-projection** (Mode-7/3D games): don't upscale a 128² buffer —
  render directly at 320×224 using the 32X framebuffer and scale the game's
  coordinate math. Mode-7 racers and 3D games do this.
- **Logical-surface doubling** (2D games): render to a logical 160×112 (or 128²)
  surface and **pixel-double** to 320×224 during `pico_present`. Simple, keeps
  the pixel look. SUPERHOT-style port used 160×112 → 320×224.

## Performance: the PICO-8 hot paths on SH-2

PICO-8 Lua leans on per-pixel division and modulo that murder the SH-2. From the
Mode-7 racer's optimization pass (see `optimization.md` for the general rules):

- **Scanline depth LUT** — precompute `z_fov_table[128]` (perspective depth per
  screen row) in init, removing all floating-point division from the Mode-7
  floor loop.
- **Bitshift + LUT the tile lookup** — build `sprid_to_gfx_offset[256]` at
  startup and replace tile/pixel `%`/`/` with `>>` and `&` (e.g. `>>16`, `>>13`,
  `&127`): the inner scanline loop drops to a few shifts + two array reads.
- **Fixed-point sprite stepping** — replace per-pixel integer division in the
  scaled-sprite blitter with 16.16 `step_x/step_y` accumulation (>10× on
  billboard/sprite scaling).
- **32-bit-aligned framebuffer writes** — write the packed 8bpp framebuffer as
  aligned `uint32_t` (4 pixels per SH-2 store); full-frame present ~0.3 ms.
- Build the compat layer + game with `-O3 -ffast-math -fomit-frame-pointer
  -flto -funroll-loops` — **but** watch the GCC 12.1 SH-2 miscompile traps in
  `toolchain-and-build.md`; if a hot routine misbehaves, drop it to `-O2` and
  use MAC/pointer idioms.

## Feature fidelity, including silence

Reproduce the original's actual behaviour, not a "nicer" version:

- Match menus, difficulty levels, stages, HUD layout, attract/demo mode.
- **If the original ships SFX/music data but never calls `sfx()`/`music()` in
  play, keep the port silent** too (a fighter port did this deliberately) rather
  than inventing a soundtrack the running game never had. Faithful means
  faithful, including what it *doesn't* do.
