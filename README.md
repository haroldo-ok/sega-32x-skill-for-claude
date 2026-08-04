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

- **Make a new 32X game** — 2D sprite/tile games, software-3D polygon games,
  Mode-7 / pseudo-3D racers, or wireframe/vector games.
- **Port a game to the 32X** — from DOS, Genesis/Mega Drive, an HTML5/JS game, a
  C/Pascal codebase, or a reverse-engineered original.
- **Use the second SH-2** — dual-core work via the COMM mailbox (offloading the
  framebuffer clear, audio mixing, or a render phase).
- **Add sound** — PWM audio and a software voice mixer.
- **Optimize SH-2 code** — fixed-point, table lookups, killing divides, cache
  handling, `--gc-sections`.
- **Debug a black-screen or crash**, or **set up automated PicoDrive tests**.

It triggers on things as short as "port X to 32X", "make a 32X game", "3D on
32X", a mention of a `.32x` ROM, `mars.ld`, dual-SH2 / 68000 Mars hardware, VDP
framebuffer/palette issues, or PWM audio.

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
  fix-up, CI; plus the alternative distro cross-toolchain route.
- **architecture.md** — dual SH-2, the 68000's role, the SDRAM budget, VDP
  framebuffer & palette, the COMM mailbox, and the memory-coherency rules
  (uncached framebuffer vs. cached SDRAM). Includes the SH-2 `long` is 32-bit
  trap that passes on a host test and only breaks on hardware.
- **porting-workflow.md** — the core/shell split, using a desktop build as a
  test oracle, incremental bring-up, and build-time **asset conversion**
  (de-planing bitmaps, byte-swapping to big-endian, ROM archives read in place).
- **software-3d.md** — a full fixed-point software-3D pipeline (transform,
  perspective projection, flat-triangle rasterizer, painter sort), plus
  wireframe/vector and Mode-7/pseudo-3D-road rendering modes.
- **audio.md** — the PWM stereo FIFO, a software voice mixer, SFX voice-stealing,
  the "redrawing every frame starves the FIFO and turns music to buzz" gotcha,
  and how to actually verify audio.
- **optimization.md** — SH-2 patterns: fixed-point, table lookups, the
  reciprocal-table divide (the SH-2 has no fast divide), and staged dual-core
  offload.
- **testing.md** — building the PicoDrive libretro core, the harness + input
  script format, black-screen/playability assertions, static ROM verification,
  and the harness gotchas (button mapping, don't-press-on-frame-0,
  pixel-detection pitfalls). Includes how to verify audio via PCM capture + a
  spectral fingerprint.
- **examples.md** — a catalogue of real, shipped 32X ports to learn from,
  grouped by rendering technique, port source, and audio.

### Assets (`assets/`) — ready to adapt
- **3d/** — a reusable clean-room software-3D engine (`r3d.c`, `r3d.h`) with
  fixed-point math, reciprocal-divide projection, and a flat-triangle
  rasterizer, plus `gen_tables.py` (sine + reciprocal tables). Two games shipped
  on this exact engine.
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
| 2D sprite / tile games | ✅ |
| Software 3D (polygons) | ✅ reusable engine in `assets/3d/` |
| Mode-7 / pseudo-3D roads | ✅ technique (scanline depth LUT, scaled sprites) |
| Wireframe / vector | ✅ technique (line rasterizer) |
| Dual-core (2nd SH-2) | ✅ COMM job dispatcher; frame-clear offload proven |
| PWM audio + mixer | ✅ technique + verification method |
| Porting (DOS/JS/RE/etc.) | ✅ core/shell split, asset pipeline |
| Optimization | ✅ fixed-point, tables, reciprocal divide, cache |
| Automated testing | ✅ static ROM checks + headless PicoDrive |

---

## Provenance — this is battle-tested, not theoretical

The skill's reference implementation is **DOOM 32X: Resurrection** (`d32xr`)
built with **Chilly Willy's 32XDK**. Beyond that, its guidance was hardened by
actually using it:

- **Two complete games were built on the bundled 3D engine** and verified
  milestone-by-milestone in PicoDrive: a software-3D **rally racer** (fixed-point
  vehicle physics, a curved/hilly track, timed race with checkpoints, an
  opponent, a reciprocal-divide optimization pass, and dual-core framebuffer
  clearing) and a Star-Fox-style **rail shooter** (on-rails flight, aimed 3D
  shooting, incoming enemy types, hit detection, waves + a boss, explosions,
  sound, a title/game-over flow, and multiple stages).
- **A library of expert 32X ports** (haroldo-ok's work — a DOS software-3D
  racer, an OutRun-style road racer, a Mode-7 racer, wireframe games, a DOS
  action-adventure with a real 68000 side and asset pipeline, an RTS driven by
  user-supplied data, and an eight-voice PWM tracker) was studied to fold in
  proven techniques for audio, alternative rendering modes, endianness handling,
  and toolchain bootstrapping.

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
