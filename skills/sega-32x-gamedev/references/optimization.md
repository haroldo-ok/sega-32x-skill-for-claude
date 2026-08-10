# SH-2 optimization playbook

Apply these when the user says "optimize the code" or the game is too slow.
Correctness first — optimize a working, tested build so `run_tests.py` can
prove you did not regress. Study d32xr's `r_phase*.c`, `sh2_*.s`, `marsnew.c`,
`marsdraw.c`, and `tables.c` for worked examples of every idiom below.

## The SH-2 cost model

- **No fast hardware divide.** `/` and `%` are very expensive; a 32/32 divide
  can cost dozens of cycles. Avoid them in loops entirely.
- **Multiply is cheap-ish; shifts are cheapest.** Prefer `<<`/`>>`/`&` over
  `* / %` by powers of two.
- **Small cache, big penalty for misses.** Keep hot loops and their data
  compact and aligned; a cache miss to SDRAM is many cycles.
- **Two SH-2s.** Wall-clock time halves for anything you can run in parallel on
  the slave. This usually beats any micro-optimization.
- **~23 MHz.** There is not much headroom; per-pixel work must be tiny.

## Rules, in rough priority order

1. **Hoist invariant work out of loops.** Compute anything that does not change
   per iteration once, before the loop. Lift array-base + index math, repeated
   struct-field loads, and function calls out of inner loops. This is the
   highest-yield, lowest-risk change and the first thing to do on any hot path.

2. **Precompute tables.** Trig, reciprocals, color/lighting maps, fixed-point
   scale factors, log2 — bake them into `const` arrays in ROM (see the large
   `tables.c` in d32xr). A table lookup + shift replaces a runtime divide or
   transcendental.

3. **Bit-shift and mask instead of `* / %`.**
   - `x / 64` → `x >> 6`; `x % 64` → `x & 63`; `x * 8` → `x << 3`.
   - Divide by a non-power-of-two constant via a precomputed reciprocal and a
     shift (fixed-point), not runtime `/`.

4. **Use fixed-point, never float, in hot paths.** 16.16 is the common format.
   Multiply: `(int64_t)a * b >> 16` (or a 32×32→64 helper); the core should be
   float-free so the desktop and 32X builds share the exact math.

5. **Offload a whole phase to the slave SH-2.** Split the frame into stages and
   let the slave run one (e.g. audio mixing, plane/floor fill, sprite draw)
   while the master does the next. Use the COMM begin/wait/end pattern from
   `mars.h`. Clear cache on buffers the other CPU wrote. This is typically the
   biggest single speedup available.

6. **Shrink the work, not just the code.** Lower the internal render resolution
   (render narrow and let the hardware line-double), add a "potato" mode that
   fills floors/ceilings with a solid color, cap draw distance, skip offscreen
   objects early. Doing less is faster than doing the same thing faster.

7. **Reduce cache pressure.** Keep hot structs small and hot loops short. Align
   DMA/shared buffers to 16 bytes. Mark hot, shared, DMA-touched routines with
   the cache-aligned section attribute (d32xr's `ATTR_DATA_CACHE_ALIGN`) and
   pin their optimization level so LTO cannot reshape their timing.

8. **Use DMA for big copies** instead of CPU loops (framebuffer fills, sample
   streaming), and overlap DMA with compute.

9. **Prefer `int`-sized locals.** The SH-2 is a 32-bit core; gratuitous 8/16-bit
   arithmetic can add masking. Use the width the algorithm needs, default to
   `int` for loop counters and accumulators.

10. **Avoid function-call overhead in inner loops.** Inline tiny helpers, or
    mark them `static inline`. Move branchy setup out; keep the inner loop
    straight-line.

## Release build flags

```
-Os -flto -fuse-linker-plugin -fomit-frame-pointer \
-ffunction-sections -fdata-sections -fno-common \
-ffast-math -funroll-loops -fno-align-loops -fno-align-jumps -fno-align-labels
-Wl,--gc-sections
```

`-Os` + `--gc-sections` keep the ROM small; `-flto` lets the linker inline and
prune across files. **Exception:** compile timing-critical objects (the PWM
mixer, anything that must hit a fixed sample deadline) at `-O2 -fno-lto` so
their behavior is stable, as d32xr does for its hardware layer.

## Measuring

- `sh-elf-size game.elf` and the map file: watch `.text` size and `__bss_end`.
- Use the VBlank counter / watchdog timer (`Mars_GetTicCount`,
  `Mars_GetWDTCount`, `Mars_FRTCounter2Msec`) to time frame phases on hardware.
- After every optimization, re-run `run_tests.py`: identical checkpoint frames
  (or intended differences only) prove you optimized without changing behavior.

## What not to do

- Don't hand-write SH-2 assembly until C + the above is exhausted and profiled;
  when you do, keep it in `.s` files mirroring the d32xr `sh2_*.s` style.
- Don't introduce floating point "just here".
- Don't optimize before the emulator test is green — you will not know what you
  broke.

## Killing the divide (software 3D and elsewhere)

The SH-2 has no fast divide, so any per-vertex or per-pixel `/` is a hot spot.
The perspective divide in projection is the classic case. Replace it with a
**reciprocal table**:

- Precompute `recip[k] = (1<<RECIP_SH)/k` for a key `k = z >> 12` (a 4096-entry
  table covers a wide z range; `RECIP_SH ≈ 22`). Generate it at build time into
  an `.inc` (see `assets/gen_tables.py`), so it lives in ROM.
- Project with a multiply + shift instead of a divide:
  `screen = (coord * focal * recip[k]) >> (RECIP_SH + 12)`.
- Verify accuracy against the exact divide on the host: worst error should be
  ~1px. This removed every runtime divide from the projection hot path.

Same trick applies to any `a/b` where `b` falls in a bounded range you can key
a table on.

## Put the second SH-2 to work

By default the slave SH-2 boots and then **parks in a spin loop** (in `crt0.s`)
— a whole CPU doing nothing. Stages of offload, easiest/safest first:

1. **Framebuffer clear on the slave.** The full-screen clear is ~72 KB of
   writes every frame. Hand it to the slave: master posts a "clear + color"
   command, does its game logic while the slave clears, then waits for the
   slave's ack before it draws. Because the frame buffer at `0x24000000` is
   **uncached I/O space**, and the two cores touch disjoint memory (clear vs.
   nothing, then master draws after the ack), no cache management is needed.
   This is low-risk and fail-safe: bound the master's wait so a silent slave
   never hangs the game, and the master still renders everything itself.
2. **Split rasterization** (harder): master draws the top half, slave the
   bottom half. This needs the master to publish per-frame geometry the slave
   reads from **cached SDRAM**, so it requires real cross-core cache handling —
   a bigger, riskier step. Do it as its own milestone, not a drive-by.

See `architecture.md` for the COMM job-dispatch protocol.

## Measure before you optimise (and after)

Guessing the bottleneck wastes effort. Two real cases from the voxel port
(`zepton32x`) where the obvious culprit was wrong:

- Hoisting ~1800 perspective **divides** out of a per-cell loop changed the
  framerate by **zero** — divides weren't the wall.
- Shortening terrain **columns** (less overdraw) moved it **12 → 20 fps** — the
  wall was **fillrate/overdraw**, not arithmetic.

So: measure, change one thing, measure again.

### Measuring effective framerate through the video harness

The PicoDrive harness captures video, not timing. To read the game's *effective*
framerate, draw a bar whose width encodes the game's own frame counter and read
it from two captures N emulated frames apart:

```c
GFX_FillRect(0, 0, (int)(frame & 127), 3, C_WHITE);   /* width == frame & 127 */
```

```python
# barw(t0), barw(t1) = white run-length at row 1; N emulated frames between shots
iters_per_N_frames = (barw(t1) - barw(t0)) & 127        # game iterations, not frames
```

If the game does one iteration per emulated frame it reads ~N; a heavy renderer
reads far less (Zepton's terrain ran ~12–19 iterations per 60 frames). See the
critical consequence in `testing.md`: **"run 60" emulated frames is NOT 60 game
iterations** when the loop is heavy — spawn timers, cooldowns, and approach
speeds are counted in *iterations*, so scripted waits must be long enough for the
game to actually advance.

## Divide-hoisting for per-slice / per-scanline projection

When many points share a divisor (all cells in a voxel depth slice, all pixels
in a Mode-7 scanline), compute the reciprocal **once** and multiply:

```c
int rf = (FOCAL << 12) / zz;        /* one divide for the whole slice, 12.12 */
sx = 160 + (((wx>>8) * rf) >> 12);  /* per point: a multiply + shift */
```

Unit-test the fast path against the reference divide (match within 1px). It
reduces SH-2 CPU even when it doesn't move a fillrate-bound framerate — headroom
for real hardware and for adding entities.

## Fillrate & overdraw are often the wall on the 32X

The framebuffer is uncached I/O; every pixel written costs. Painter's-order
renderers (voxel billboards, sprite stacks) can write each screen pixel several
times. Before micro-optimising arithmetic:

- Reduce **overdraw**: shorter columns, tighter sprites, skip fully-occluded
  draws, don't redraw static regions.
- Reduce the **clear**: a full 320×224 clear is ~9 game-iterations of budget in
  the Zepton loop. Offload it to the slave SH-2 (COMM job), or clear only the
  sky band if the geometry tiles the rest.
- Cutting **cell/vertex count** helps only if you were geometry-bound; if
  fewer cells changes nothing, you're fillrate-bound — attack pixels, not counts.

## Compute-vs-fillrate: raycast vs billboard

A per-screen-column raycaster (no overdraw, sky-only clear) *sounds* faster than
per-cell billboards, but it samples the heightmap far more often. If the height
function is trig-heavy, the raycaster becomes compute-bound and can be **slower**
(measured 8 vs 19 fps in Zepton). Precompute the heightmap once per frame to make
the raycaster fillrate-bound before adopting it. Full analysis in
`voxel-landscape.md`. General rule: a rewrite you *built* is not a rewrite you
should *ship* — adopt it only if it measures better.

## PICO-8 / Mode-7 scanline hot-path idioms

From a heavily-optimized Mode-7 racer (see `pico8-porting.md` for the full port
context). These turn a floating-point, divide-per-pixel PICO-8 renderer into an
SH-2-friendly one:

- **Scanline depth LUT** — precompute `z_fov_table[SCREEN_H]` (perspective depth
  per row) once in init; the floor/Mode-7 loop reads it instead of dividing.
- **Bitshift + LUT the inner loop** — precompute `sprid_to_gfx_offset[256]` and
  replace tile/pixel `/` and `%` with `>>`/`&`, cutting the per-pixel work to a
  couple of shifts and array reads.
- **Fixed-point sprite stepping** — accumulate 16.16 `step_x/step_y` across a
  scaled sprite instead of dividing per pixel (>10× on billboard/car scaling).
- **32-bit-aligned framebuffer writes** — pack four 8bpp pixels into a `uint32_t`
  and store aligned words; a full 320×224 present lands around ~0.3 ms.

## Attract / demo mode (cheap, expected of arcade ports)

After N seconds of menu inactivity, seed the game with a scripted/bot input
stream and run a live gameplay demo (a Mode-7 race auto-started after ~7.5 s).
It reuses the exact game loop, doubles as a always-on smoke test of the render
path, and is what players expect from an arcade title. The bot input stream is
also the seed for the deterministic record/replay tests above.
