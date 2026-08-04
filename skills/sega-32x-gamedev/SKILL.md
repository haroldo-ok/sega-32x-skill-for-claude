---
name: sega-32x-gamedev
description: >-
  Create, improve, optimize, and port games for the Sega 32X (Sega Mars) using
  Chilly Willy's 32XDK toolchain and the DOOM 32X Resurrection (d32xr) codebase
  as reference. Use whenever the user wants to port a game (from DOS,
  Genesis/Mega Drive, C, Pascal, or any platform) to the 32X, build or debug a
  homebrew 32X ROM, make a 32X game (2D or a software-3D / polygon game with a
  fixed-point transform, perspective projection, and flat-shaded triangle
  rasterizer), use the second SH-2 / dual-core, optimize SH-2 code, fix a 32X
  ROM that boots to a black screen, or set up automated PicoDrive tests. Trigger
  even when the user only says "port X to 32X", "make a 32X game", "3D on 32X",
  "compile this for 32X", mentions a `.32x` ROM, SH-2 / dual-SH2 / 68000 Mars
  hardware, the mars.ld linker, VDP framebuffer/palette issues, or PWM audio.
  Prefer this skill over general knowledge for anything touching the 32X: the
  toolchain, memory map, header format, and test methodology are specific and
  easy to get subtly wrong from memory.
---

# Sega 32X game development & porting

This skill turns a game — an existing port target or a new idea — into a
**playable, verified `.32x` cartridge ROM**. It encodes the toolchain, hardware
model, project layout, build pipeline, optimization playbook, and the
automated PicoDrive test methodology that catches the number-one 32X failure:
a ROM that compiles cleanly but boots to a **black screen**.

The reference implementation for everything here is Victor Luchits' **DOOM 32X:
Resurrection** (`d32xr`) built with **Chilly Willy's Sega devkit (32XDK)**. When
in doubt about how to do something on real hardware, look at how d32xr does it.

- d32xr source: https://github.com/viciious/d32xr
- 32XDK releases: https://github.com/viciious/32XDK/releases


For **3D games** (a software polygon pipeline — no GPU/FPU on the 32X),
see `references/software-3d.md` and the ready-made engine in `assets/3d/`
(fixed-point transform, reciprocal-divide projection, flat-triangle
rasterizer). Two games shipped on it (a rally racer and a rail shooter).


For **worked examples** of complete 32X ports (a DOS software-3D racer, an
HTML5 game, and a DOS action-adventure with a 68000 side and an asset
pipeline), see `references/examples.md`.


For **sound**, see `references/audio.md` (PWM stereo FIFO, a software voice
mixer, the framebuffer-redraw-starves-the-FIFO gotcha, and how to actually
verify audio via PCM capture + a spectral fingerprint).

## Definition of done (do not stop early)

A 32X task is complete only when **all** of these hold. Treat them as a
checklist and report each one:

1. **It compiles** — `make` produces a `.32x` ROM with no errors.
2. **It links within RAM** — `.data + .bss` fit in SDRAM and BSS does not
   collide with the SH-2 stacks (see the memory map below). This is checked
   mechanically; a link that "succeeds" can still overflow RAM.
3. **It is not a black screen** — a headless PicoDrive test boots the real ROM
   and asserts that rendered frames are lit, colourful, and change over time.
4. **It is playable** — scripted controller inputs drive the game through its
   real states (title → menu → gameplay) and each checkpoint frame passes.
5. **The ROM is reachable by the user** — the final `.32x` is copied to a
   known output path and presented (never left only in a scratch build dir).

Never declare success on "it compiled." A compiling black screen is the
default failure mode of a naive 32X port, and the whole point of this skill is
to get past it.

## Step 0 — Install the toolchain

Chilly Willy's devkit provides both cross-compilers: **`sh-elf-gcc`** (the two
SH-2 CPUs) and **`m68k-elf-gcc`** (the Genesis 68000). Install release
`20220418` into `/opt/toolchains/sega` (override with `GENDEV=<path>`):

```sh
curl -LO https://github.com/viciious/32XDK/releases/download/20220418/chillys-sega-devkit-20220418-opt.tar.zst
sudo tar --zstd -xf chillys-sega-devkit-20220418-opt.tar.zst -C /
```

Verify: `/opt/toolchains/sega/sh-elf/bin/sh-elf-gcc --version` (GCC 12.1).
Full details, exact flags, and CI-cache tricks: **`references/toolchain-and-build.md`**.

## Pick the workflow

- **Porting an existing game** (DOS, Genesis, a C/Pascal codebase, an emulator
  core, etc.) → read **`references/porting-workflow.md`**. This is the most
  common request. The core idea: split the game into a *platform-clean core*
  and a *thin 32X shell*, get it running on desktop first as an oracle, then
  bring it up on hardware incrementally.
- **Creating a new native 32X game from scratch** → start from the project
  layout below and `references/architecture.md`; the porting doc's "bring-up
  order" still applies.
- **Optimizing / improving an existing 32X project** → read
  **`references/optimization.md`**. Mine d32xr for patterns (fixed-point,
  bitshifting, hoisting work out of loops, offloading to the second SH-2,
  cache alignment).
- **Debugging a black screen / crash** → jump to "Black-screen triage" below
  and `references/testing.md`.

Whatever the workflow, wire up the tests from **`references/testing.md`** early.
They are how you *know* you are done rather than hoping.

## Canonical project layout

Keep game logic strictly separate from 32X hardware code. This is what makes a
port verifiable (you can run the same core on desktop) and what keeps the SH-2
side small.

```
game-32x/
├── Makefile                    # SH-2 + 68000 build → .32x  (template in assets/)
├── src/
│   ├── core/                   # portable C11: game logic, physics, rendering
│   │                           #   NO OS calls, NO float in hot paths, endian-clean
│   └── platform/
│       ├── 32x/                # SH-2 shell: main, hw/VDP, palette, audio, input
│       │   ├── mars.ld         # SH-2 linker script     (template in assets/)
│       │   ├── mars_start.s    # SH-2 startup / ROM+Mars header + embedded 68000 bin
│       │   └── md_src/         # 68000 resident: controller + VBlank + music service
│       └── sdl/                # desktop reference shell (test oracle; optional but
│                               #   strongly recommended for ports)
├── tools/                      # build-time asset converters, romfix  (assets/)
├── tests/                      # PicoDrive harness + scripts + verify_rom  (assets/)
│   ├── harness.c               # headless libretro host
│   ├── run_tests.py            # runner + black-screen / playability assertions
│   ├── verify_rom.py           # static ROM/ELF structural checks
│   └── scripts/                # point-to-point input scripts (boot, menu, play…)
└── rom/  (or release/)         # OUTPUT: the final .32x lands here
```

Two-CPU rule of thumb: put the **game** on the master SH-2, dedicate the
**slave SH-2** to a heavy parallel job (PWM audio mixing, or a rendering phase),
and use the **68000** for controller polling, VBlank timing, and native
YM2612/PSG music. See `references/architecture.md`.

## Build → verify → emulate loop

```sh
make -j                                    # → rom/<game>.32x (+ .elf + .map)
python3 tests/verify_rom.py rom/<game>.32x build/<game>.elf   # static checks
python3 tests/run_tests.py                 # headless PicoDrive point-to-point
```

The Makefile template ends with a `romfix` step (writes the Genesis header
checksum and pads the ROM) and a `check` target that runs `verify_rom.py`. Wire
`run_tests.py` into CI. Build PicoDrive's libretro core once (instructions in
`references/testing.md`).

## Hard constraints cheat-sheet

Memory map the SH-2 linker (`mars.ld`) must honor:

```
0x02000000  ROM  (.text + .rodata; cartridge, read-only, ~4 MiB window)
0x06000000  SDRAM (256 KiB total, shared by both SH-2s):
              .data (initialized, copied from ROM by startup)
              .bss  (zeroed by startup; heap grows up from its end)
              ...
0x0603FC00  top of master SH-2 stack (grows down)     ← single-CPU layout
0x0603F800 / 0x06040000  split stacks if you use the slave SH-2
```

- **SDRAM is only 256 KiB.** `.data + .bss` plus stacks must fit. Verify that
  `__bss_end` stays well below the stack base (CI in d32xr asserts
  `bss_end < 0x603C000`). Overflowing RAM is a top black-screen cause.
- **Large/immutable data lives in ROM, not RAM.** Decode assets at build time
  and read them from the cartridge; do not `malloc` big buffers.
- **The ROM needs a valid Genesis + Mars header.** `SEGA 32X` at 0x100, the
  Mars module header, correct SH-2 entry points/vector bases, ROM-end at
  0x1A4, and the 16-bit word checksum at 0x18E. Always run the `romfix` step.
- **Everything is big-endian.** Byte-swap when reading little-endian source
  assets (DOS files) at build time or load time.
- **Mask the controller to the reliable 3-button subset** (U/D/L/R, A/B/C,
  Start). Several emulators mirror d-pad bits into the 6-button extended
  nibble, making every direction read as a Jump/Back press.

## Black-screen triage

When a ROM compiles but shows black, check in this order (details in
`references/testing.md` and `references/architecture.md`):

1. **RAM overflow** — `.data + .bss` exceeds SDRAM / collides with stacks.
2. **`--gc-sections` stripped live code** — the linker kept only the header and
   discarded the game. `verify_rom.py` guards this by asserting known code
   markers are present and `.text` is large.
3. **Palette never loaded** — nonzero pixels all map to palette entry 0
   (black). Seed the palette before the first frame.
4. **VDP / framebuffer not initialized**, or frame buffers never flipped.
5. **68000 handshake stall** — startup released the slave/68000 through a stale
   register, or a blocking audio/VGM wait wedged VBlank service.
6. **Asset blob placed beyond the fixed low-ROM window** the 68000 copies from
   at boot.

## Optimization quick rules (full playbook in references/optimization.md)

When asked to "optimize the code":

- Hoist invariant work **out of loops**; precompute tables.
- Use **bit-shifts and masks** instead of `*`, `/`, `%` by powers of two; the
  SH-2 has no fast hardware divide.
- Use **fixed-point** (e.g. 16.16), never floating point, in hot paths.
- **Offload** a parallel workload to the slave SH-2 via the COMM registers.
- Mark hot, DMA-touched routines with the cache-aligned section attribute (see
  `ATTR_DATA_CACHE_ALIGN` in d32xr) and clear cache lines deliberately.
- Build `release` with `-Os -flto -fomit-frame-pointer -ffunction-sections
  -fdata-sections -Wl,--gc-sections`.
- Look at d32xr's `r_phase*.c`, `sh2_*.s`, and `marsnew.c` for concrete idioms.

## Bundled resources

- `references/toolchain-and-build.md` — devkit install, both compilers, exact
  flags, linker map, header/romfix, CI.
- `references/architecture.md` — dual SH-2, 68000 role, SDRAM budget, VDP
  framebuffer & palette, PWM audio, VGM music, controllers, inter-CPU COMM.
- `references/porting-workflow.md` — the step-by-step port method (core/shell
  split, desktop oracle, asset conversion, timing model, incremental bring-up).
- `references/optimization.md` — SH-2 optimization patterns drawn from d32xr.
- `references/testing.md` — build PicoDrive, the harness + script DSL + runner,
  black-screen assertions, static ROM verification.
- `assets/` — ready-to-adapt `Makefile`, `mars.ld`, `romfix.py`,
  `verify_rom.py`, `harness.c`, `run_tests.py`, and example test scripts.
