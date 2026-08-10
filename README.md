# sega-32x-gamedev

A Claude **skill** for building, porting, optimizing, and testing games for the
**Sega 32X** (codename *Mars*). It turns a game idea or a port target into a
**playable, verified `.32x` cartridge ROM** — and, just as importantly, it knows
how to prove the ROM actually works instead of stopping at "it compiled."

The 32X is an unusual target: **two 23 MHz SH-2 CPUs** plus the Genesis
**68000**, **256 KB of SDRAM**, a **256-colour packed framebuffer**, **no GPU**,
and **no FPU**. Naive builds compile cleanly and then boot to a black screen.
This skill exists to get past that reliably.

---

## What it does

Point Claude at any of these and this skill drives the work:

- **Make a new 32X game** in any of three rendering families:
  - **Software-3D polygon** — fixed-point transform, reciprocal-divide
    projection, flat-triangle rasterizer (a ready-made engine ships in the
    skill). Games built: a rally racer, a rail shooter, kart racers.
  - **Voxel landscape** — a Comanche-style scrolling heightmap of
    perspective-scaled cells (cell-billboard *and* per-column-raycaster methods,
    with the compute-vs-fillrate trade documented). Game built: a voxel shmup.
  - **2D sprites** — packed 8bpp framebuffer plus scanline shape fills
    (triangle/circle/polygon), menu/flow state machines, faithful-graphics
    reconstruction. Game built: a faithful vertical shooter.
- **Port a game to the 32X** — from DOS, Genesis/Mega Drive, a **PICO-8** cart,
  an HTML5/JS game, a C/Pascal codebase, or a reverse-engineered original —
  including the **licensing model** for open-source (GPL) and CC-BY-NC targets.
- **Use the second SH-2** — dual-core work via the COMM mailbox (offloading the
  framebuffer clear, audio mixing, or a render phase).
- **Add sound** — PWM audio and a software voice mixer, wired event-driven so the
  game logic stays hardware-free and testable.
- **Optimize SH-2 code** — fixed-point, table lookups, killing divides,
  divide-hoisting, fillrate/overdraw reduction, cache handling — with a method to
  **measure effective framerate** through the video test harness first.
- **Debug a black-screen or crash**, or **set up automated PicoDrive tests**.

It triggers on things as short as "port X to 32X", "make a 32X game",
"3D/voxel on 32X", porting a PICO-8/HTML5/DOS game, a mention of a `.32x` ROM,
`mars.ld`, dual-SH2 / 68000 Mars hardware, VDP framebuffer/palette issues, or
PWM audio.

---

## The core idea: *verified* done, not *compiled* done

The skill's backbone is a five-point **definition of done** — a task isn't
finished until all of them hold:

1. **It compiles** — `make` produces a `.32x` with no errors.
2. **It links within RAM** — `.data + .bss` + stacks fit in 256 KB SDRAM, checked
   mechanically (a "successful" link can still overflow RAM).
3. **It is not a black screen** — a headless PicoDrive test boots the real ROM
   and asserts frames are lit, colourful, and changing.
4. **It is playable** — scripted controller inputs drive the game through its
   real states (title → menu → gameplay) and each checkpoint frame passes.
5. **The ROM is reachable** — the final `.32x` is copied to a known path and
   presented, never left in a scratch dir.

Every capability in the skill is wired to this loop: **build → static verify →
headless emulate → assert**. A compiling black screen is the default failure
mode of a naive 32X port, and defeating it is the whole point.

---

## What's inside

### References (`references/`)
- **toolchain-and-build.md** — installing Chilly Willy's 32XDK (both
  `sh-elf-gcc` and `m68k-elf-gcc`), exact flags, linker map, ROM header/checksum
  fix-up, CI; the alternative distro cross-toolchain route; and the **GCC 12.1
  SH-2 miscompile traps** to code around (12-byte struct returns, 64-bit
  multiply chains, dropped stores, calls across optimization levels).
- **architecture.md** — dual SH-2, the 68000's role, the SDRAM budget, VDP
  framebuffer & palette, the COMM mailbox, and the memory-coherency rules
  (uncached framebuffer vs. cached SDRAM). Includes the SH-2 `long` is 32-bit
  trap that passes on a host test and only breaks on hardware.
- **porting-workflow.md** — the core/shell split, using a desktop build as a
  test oracle, incremental bring-up, build-time **asset conversion**, and the
  **open-source IP model** (GPL stays GPL + attribute; CC-BY-NC → non-commercial
  + ship original; AGPL; sanctioned/user-supplied; third-party mocap/asset
  provenance notices; decline ripped active-IP).
- **software-3d.md** — a full fixed-point software-3D pipeline (transform,
  perspective projection, flat-triangle rasterizer, painter sort + backface, no
  z-buffer), plus wireframe/vector and Mode-7/pseudo-3D-road rendering modes.
- **voxel-landscape.md** — Comanche-style scrolling voxel terrain: cell-billboard
  vs per-column-raycaster methods, camera-above-near-plane projection with a
  per-slice hoisted divide, the compute-vs-fillrate trade, and the
  into-the-screen projectile/enemy/lock-on model.
- **2d-and-shmup.md** — 2D sprite games: scanline shape fills
  (triangle/circle/polygon), menu/flow state machines, faithful-graphics
  reconstruction from a source's draw code, and event-driven sound.
- **pico8-porting.md** — the recurring PICO-8 → 32X pattern: a `pico8_api` compat
  layer (16-colour palette → CRAM, `sspr`/`pal`/`print`, `btn`, `atan2`/angle
  conventions), fixed-point sprite stepping, and 160×112 → 320×224 doubling.
- **audio.md** — the PWM stereo FIFO and a software voice mixer; an **8-voice
  S3M-style tracker mixer** with priority SFX; the **Genesis-side XGM/SGDK**
  route (YM2612 + PSG driven by the 68000/Z80 with the UI on the SH-2 over COMM);
  the FIFO-starvation gotcha; and how to actually verify audio.
- **optimization.md** — SH-2 patterns: fixed-point, table lookups, the
  reciprocal-table divide, divide-hoisting, scanline depth LUTs, 32-bit-aligned
  framebuffer writes, fillrate/overdraw reduction, staged dual-core offload, and
  **how to measure effective framerate through the video harness**.
- **testing.md** — building the PicoDrive libretro core, the harness + input
  script format, black-screen/playability assertions, static ROM verification,
  the harness gotchas (button mapping, don't-press-on-frame-0, pixel-detection
  false-positives, "run N ≠ N game iterations"), the **rebuild-from-a-known-good
  -tree** black-screen fix, and the **deterministic desktop-oracle record/replay
  + tripled-input** method. Includes audio verification via PCM capture.
- **examples.md** — a catalogue of real, shipped 32X ports to learn from,
  grouped by rendering technique, port source, and audio.

### Assets (`assets/`) — ready to adapt
- **3d/** — a reusable clean-room software-3D engine (`r3d.c`, `r3d.h`) with
  fixed-point math, reciprocal-divide projection, and a flat-triangle
  rasterizer, plus `gen_tables.py` (sine + reciprocal tables). Multiple games
  shipped on this exact engine.
- **2d/gfx_shapes.c** — drop-in scanline `GFX_FillTri` / `FillCircle` /
  `FillPoly` primitives for 2D sprite art.
- **Makefile**, **mars.ld** — a build + linker template for the SH-2/68000 ROM.
- **romfix.py** — writes the Genesis/Mars header and checksum and pads the ROM.
- **verify_rom.py** — static structural checks (header, checksum, sections, RAM
  budget, gc-sections guards).
- **harness.c**, **run_tests.py**, **scripts/** — the headless PicoDrive
  point-to-point test harness, runner, and example input scripts.

---

## How Claude uses it (the loop)

```sh
# 0. install the devkit (once)  — GENDEV defaults to /opt/toolchains/sega
make -j                                                   # → rom/<game>.32x (+ .elf/.map)
python3 tests/verify_rom.py rom/<game>.32x build/<game>.elf   # static checks
python3 tests/run_tests.py                                 # headless PicoDrive
```

Game logic is kept strictly separate from 32X hardware code, so the same core
can run on desktop as a test oracle. Work proceeds one **verified milestone** at
a time — each adds a feature, passes host unit tests + `verify_rom` + an
emulator point-to-point check, and is packaged before moving on.

---

## Capability matrix

| Area | Covered |
|---|---|
| 2D sprite / tile games | ✅ scanline shape fills in `assets/2d/` |
| Software 3D (polygons) | ✅ reusable engine in `assets/3d/` |
| Voxel landscape (Comanche-style) | ✅ technique (cell-billboard + raycaster) |
| Mode-7 / pseudo-3D roads | ✅ technique (scanline depth LUT, scaled sprites) |
| Wireframe / vector | ✅ technique (line rasterizer) |
| 3D fighting / skeletal animation | ✅ technique (keyframe interpolation, mocap bake) |
| PICO-8 → 32X porting | ✅ `pico8_api` compat layer + resolution doubling |
| Dual-core (2nd SH-2) | ✅ COMM job dispatcher; frame-clear offload proven |
| PWM audio + mixer | ✅ technique + verification method |
| 8-voice tracker (S3M) audio | ✅ technique (software mixer → stereo PWM FIFO) |
| Genesis-side music (XGM/SGDK) | ✅ technique (YM2612/PSG via 68000+Z80, SH-2 UI) |
| Porting (DOS/JS/PICO-8/RE) | ✅ core/shell split, asset pipeline, IP model |
| Optimization | ✅ fixed-point, tables, reciprocal divide, aligned writes |
| Framerate measurement | ✅ on-screen frame-counter read through the harness |
| Automated testing | ✅ static ROM checks + headless PicoDrive |
| Deterministic oracle testing | ✅ desktop record/replay + tripled-input |

---

## Provenance — this is battle-tested, not theoretical

The skill's reference implementation is **DOOM 32X: Resurrection** (`d32xr`)
built with **Chilly Willy's 32XDK**. Beyond that, its guidance was hardened by
actually using it and by studying a large body of shipped 32X ports:

- **Games built on the skill, verified milestone-by-milestone in PicoDrive:**
  a software-3D **rally racer** and Star-Fox-style **rail shooter** (on the
  bundled 3D engine); kart racers (polygon and Mode-7); a faithful 2D vertical
  **shmup** (with a faithful-graphics pass reconstructing the original's exact
  ships/enemies/boss from its draw code); and a **voxel-landscape shmup** (a
  clean-room GPLv3 port of a PICO-8 game — scrolling Comanche terrain, ship +
  lock-on reticle, bullets/homing missiles/laser, enemies, bonuses, energy/
  game-over, PWM sound, a title screen, a measured performance pass, and an
  honestly-rejected raycaster experiment).
- **A library of expert 32X ports** (haroldo-ok's work) was studied to fold in
  proven techniques, each cited in `references/examples.md`:
  - **Mode-7 racer** (a PICO-8 port) — the `pico8_api` compat layer, scanline
    depth LUTs, fixed-point sprite stepping, 32-bit-aligned framebuffer writes,
    attract/demo mode.
  - **Vector tube shooter** (Tempest-style) — 16 3D webs, translucent lane
    polys, dual-SH2 with the slave running a real-time PWM synth + techno loop.
  - **3D fighting games** (PICO-8 and DirectX sources) — a free 3D arena,
    keyframe-interpolated skeletal animation, per-body-part colour customization,
    mocap-baked poses with third-party provenance notices.
  - **SUPERHOT-style shooter** — time-scales-with-movement, real 3D meshes.
  - **PICO-8 raycaster** (dungeon crawler) — strafe controls; the `rom/obj`
    snapshot-exclusion workspace gotcha.
  - **8-voice S3M tracker player** — a software mixer to the stereo PWM FIFOs
    with priority SFX (the key PWM-audio reference).
  - **XGM player** — SGDK's XGM driver on the Genesis side (68000 + Z80 driving
    YM2612/PSG) with the UI on the SH-2, cooperating over COMM registers.
  - **AGPL 3D marble game** — a desktop-oracle record/replay determinism model,
    30 Hz fixed-tick pacing, and the **GCC 12.1 SH-2 miscompile traps**.
  - **PICO-8 city-builder sim** — grid sim, BFS pathfinding, day/night palette,
    4-voice PWM, a tiny ~7 KiB SDRAM footprint.
  - **Rail shooter** (PiFox) — 3D mesh conversion and PCM assets streamed through
    PWM.

Every technique in the skill has either shipped in one of those builds or been
lifted from a working, shipped 32X ROM — and cited.

---

## Honest boundaries

- **"Real 3D" means software rendering.** The 32X has no GPU and no FPU, so 3D is
  a fixed-point polygon/pseudo-3D pipeline running on the SH-2s — the way the
  32X actually did 3D. Faithful GPU/WebGL 3D or high-poly textured models from a
  modern engine are **not** achievable on this hardware; the skill says so up
  front rather than overpromising.
- **Full ports of huge codebases are scoped honestly.** A 200k-line SDL/GPL game
  won't fit or run on two SH-2s with 256 KB of RAM; the right move is a
  clean-room reimplementation of its design, and the skill flags that instead of
  pretending.
- **Intellectual property is respected.** The default is to ship **clean-room**
  assets and provide a build step so a user converts original data **they own**
  (the WinWar / user-supplied `DATA.WAR` model). The skill describes techniques;
  it doesn't redistribute other games' code, art, or music.
- **The devkit compiler has known traps.** GCC 12.1 for SH-2 miscompiles a few
  patterns (12-byte struct returns, 64-bit multiply chains, dropped stores, calls
  across mixed optimization levels). The skill lists them and writes around them
  (pointer math, MAC macros, one optimization level) rather than being surprised
  by a "correct" program that misbehaves only on hardware.
- **Claims match what was verified.** Video tests confirm visuals; audio is only
  called "confirmed" when a PCM-capture test actually ran, otherwise it's
  "wired." The skill is careful not to overclaim.

---

## Requirements

- A Linux environment for the cross-toolchain (Chilly Willy 32XDK at
  `/opt/toolchains/sega`, or distro `gcc-sh-elf` + `m68k` binutils).
- **PicoDrive** built as a libretro core for the headless tests.
- Python 3 (with Pillow for the pixel-level frame assertions).
- To run a finished ROM: a 32X-capable emulator (PicoDrive, Kega Fusion,
  BlastEm, Ares) or real hardware.

---

## References

- DOOM 32X: Resurrection — https://github.com/viciious/d32xr
- Chilly Willy's 32XDK — https://github.com/viciious/32XDK/releases
- Worked examples (haroldo-ok) — see `references/examples.md` for the full list
  with per-repo techniques.
