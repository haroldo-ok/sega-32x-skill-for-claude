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
  fix-up, CI; the alternative distro cross-toolchain route; the **GCC 12.1 SH-2
  miscompile traps** to code around; and the `.sdata` **`@progbits`** vector-copy
  invariant.
- **architecture.md** — dual SH-2, the 68000's role, the SDRAM budget, VDP
  framebuffer & palette, the COMM mailbox (with slot-allocation discipline), cache
  coherency in two-CPU work, and:
  - **how the 32X layer composites over the Mega Drive** — `CRAM bit 15 XOR PRI`
    over real Genesis pixels, the backdrop never showing through — reconciling three
    ports' conflicting claims against PicoDrive's compositor;
  - a **hybrid renderer** (Genesis tilemaps/sprites driven by the 68000 from an
    SH-2 display list, 32X overlay on top) and its sprite-tearing and
    contiguous-tile rules;
  - **SSF bank switching** (park both SH-2s, RV, `0xA130F1`), a boot-time page
    self-test, and why **a bank switch must never pull ROM from under running
    code**;
  - **battery saves**: versioned two-phase-commit format, plus what makes them
    persist on a real SSF cart and a **Mega EverDrive** (`$300001`/`SRM3M`, page 31,
    the X-series upper half);
  - the **boot-BIOS `.data` underflow**, framebuffer facts (zero-byte writes, VDP
    auto-fill), 68000-side gotchas, and choosing a logic rate vs. render rate.
- **porting-workflow.md** — the core/shell split, the verified-milestone rhythm,
  desktop-oracle bring-up, build-time **asset conversion** (LZSS/offset tables,
  LCF, TMX, autotile→dedup atlas), the **"compile content to bytecode + a tiny VM"**
  archetype up to a full JRPG and large audited ports, data-driven engines, and:
  porting from a **binary-only `.EXE`** via a written spec; **rewriting a
  machine-translated source against its data model**; re-expressing **units**
  (tick rates, fixed-point scales, integer widths); converting another engine's
  **matrix/axis conventions**; root-motion "teleports"; and the **IP model** —
  GPL vs. game-data licences, CC/BSD/freeware cases, user-supplied data, original
  games built on a recomp's *documented structures*, clean-room design
  reimplementation, fan-port labelling, and keeping third-party sound packs
  replaceable.
- **software-3d.md** — a full fixed-point software-3D pipeline (transform,
  projection, flat-triangle rasterizer, painter sort + backface, no z-buffer),
  wireframe/vector and Mode-7 modes, filled vector webs, a **first-person
  raycaster** in several shipped budgets with **shade-bank fog**, skeletal and
  box-skeleton fighters, a PSX-style ordering-table renderer, **pseudo-3D from
  baked sprite angles / sprite-stacking**, an **extruded top-down "fake 3D" city**,
  and camera smoothing (C¹ track slope, spring-damped and jump-aware cameras).
- **strategy-and-grid.md** — RTS/tactics and grid crawlers: host-tested **A\***,
  three-state **fog of war**, deterministic grid logic with interpolated rendering,
  RTS AI/economy, turn-based pad conventions, and **lattice arenas that are
  connected by construction** (proven over seeds).
- **voxel-landscape.md** — Comanche-style scrolling voxel terrain: cell-billboard
  vs per-column-raycaster methods, camera-above-near-plane projection with a
  per-slice hoisted divide, the compute-vs-fillrate trade, and the
  into-the-screen projectile/enemy/lock-on model.
- **2d-and-shmup.md** — 2D sprite games: scanline shape fills, menu/flow state
  machines, faithful-graphics reconstruction from a source's draw code,
  event-driven sound, indexed-mode transparency and zoom, split-screen, and sprite
  animation details that read as bugs when wrong.
- **pico8-porting.md** — the recurring PICO-8 → 32X pattern: a `pico8_api` compat
  layer (16-colour palette → CRAM, `sspr`/`pal`/`print`, `btn`, `atan2`/angle
  conventions), fixed-point sprite stepping, and 160×112 → 320×224 doubling.
- **audio.md** — PWM basics and a software voice mixer, and four music routes with
  a "which path?" decision: an 8-voice tracker / **ProTracker MOD player** (ticked
  at 50 Hz, not the NTSC vblank), **streaming IMA-ADPCM** on the slave (with a
  sequence-numbered COMM protocol and loop/fade/memorize), **Genesis-side
  XGM/SGDK**, and a **MIDI→VGM** pipeline. Plus the **real-hardware PWM recipe —
  slave DMA from ping-pong buffers** (interrupt-per-sample was silent on a real
  32X), a polled single-voice player, mixing at the source's native rate, the
  **66-second song-clock overflow**, a beam-search **ADPCM encoder** that fitted
  2 MiB of radio into half the space, fully synthesised original music, and audio
  assertions (never silent, no DC offset, exactly silent when off, no clipping).
- **optimization.md** — SH-2 patterns: fixed-point, tables, reciprocal divides,
  divide-hoisting, killing hidden `__divdi3`, **arithmetic shifts that compile to
  libgcc calls** (no barrel shifter), convex/Gouraud-run rasterising, a
  **longword transparent blitter**, dirty rectangles, the **60/n vblank** model and
  the **deferred flip**, a **frame-time governor** with hysteresis, **splitting a
  frame across both SH-2s** (adaptive bands, lock-free work stealing), code in
  SDRAM, the `make headroom` probe, and **profiling** — a PicoDrive cycle-per-PC
  profiler, profile-by-subtraction builds, and the emulator's blind spots (no
  cache, wait-state, RV-contention or free-running-timer model).
- **testing.md** — the PicoDrive harness and script DSL, black-screen/playability
  assertions, static ROM verification, the black-screen ladder and a catalog of
  "verifies-clean-but-boots-black" causes, **boot-stage markers**, liveness
  beacons, telemetry asserts and wait-until-state, **harness audio capture**,
  semantic checks (palette identity, frozen pause), the verification accelerator
  and debug/cheat builds, the deterministic oracle, **generated test scripts
  validated by a model**, **building and testing with no proprietary data**,
  **colour-coded hardware probe ROMs**, **checks proven against the bug they
  guard**, exhaustive host sweeps and invariant detectors, and reproducible builds.
- **examples.md** — a catalogue of real 32X ports to learn from, grouped by
  rendering technique, port source and audio, including full-source workspaces
  studied in depth.

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
| First-person raycaster | ✅ technique (quarter-res + 2×2 expand, shade-bank fog) |
| Voxel landscape (Comanche-style) | ✅ technique (cell-billboard + raycaster) |
| Mode-7 / pseudo-3D roads | ✅ technique (scanline depth LUT, scaled sprites) |
| Pseudo-3D from sprites | ✅ technique (baked view angles, sprite-stacking, mirrored halves) |
| Extruded top-down "fake 3D" | ✅ technique (vanishing-point roofs, edge-inward order) |
| Hybrid Mega Drive VDP + 32X overlay | ✅ technique (68000 display list; emulator-verified) |
| Wireframe / vector | ✅ technique (line rasterizer) |
| 3D fighting / skeletal animation | ✅ technique (keyframe interpolation, mocap bake) |
| RTS / tactics / grid crawler | ✅ A*, fog of war, deterministic-grid + interp |
| PICO-8 → 32X porting | ✅ `pico8_api` compat layer + resolution doubling |
| Interpreter games (RPG Maker etc.) | ✅ compile content → bytecode + tiny VM |
| Full JRPG (maps, battles, party) | ✅ event VM + turn-based battle + menu/party |
| Indexed alpha / zoom overlays | ✅ stipple/blend-LUT transparency + fixed-point scale |
| Large-scope content ports | ✅ audit-first scoping + ROM banking / 32X-CD plan |
| Dual-core (2nd SH-2) | ✅ COMM jobs, adaptive row bands, lock-free work stealing |
| PWM audio + mixer | ✅ technique + verification method |
| Real-hardware PWM | ✅ slave DMA ping-pong (IRQ-per-sample flagged as emulator-only) |
| ProTracker MOD playback | ✅ on-slave player, 50 Hz tick, soft-knee mix |
| 8-voice tracker (S3M) audio | ✅ technique (software mixer → stereo PWM FIFO) |
| Compressed samples (IMA ADPCM) | ✅ technique (decode + mix on slave SH-2) |
| Streaming BGM (ADPCM from ROM) | ✅ slave decoder + SFX mix, COMM protocol |
| Genesis-side music (XGM/SGDK) | ✅ technique (YM2612/PSG via 68000+Z80, SH-2 UI) |
| MIDI → VGM music generation | ✅ build-time MIDI→YM2612/PSG/DAC pipeline |
| Battery-backed SRAM saves | ✅ versioned checksum + two-phase commit + test |
| Saves on real flash carts | ✅ SSF save window, EverDrive Pro/X page 31, `SRM3M` |
| ROMs past 4 MiB (SSF banking) | ✅ park-the-SH-2s protocol + boot-time page self-test |
| Split-screen 2-player | ✅ technique (per-view camera/HUD, clipped) |
| Porting (DOS/JS/PICO-8/RE) | ✅ core/shell split, asset pipeline, IP model |
| Optimization | ✅ fixed-point, tables, reciprocal divide, aligned writes |
| Framerate / headroom measurement | ✅ frame-counter read + continuous ballast probe |
| Frame pacing under load | ✅ deferred flip + frame-time governor with hysteresis |
| Cycle profiling | ✅ PicoDrive interpreter cycle-per-PC histogram |
| Automated testing | ✅ static ROM checks + headless PicoDrive + audio capture |
| Questions only hardware can answer | ✅ colour-coded probe ROMs |
| CI without proprietary game data | ✅ placeholder assets + synthetic converter self-test |
| Hang vs. black-screen diagnosis | ✅ heartbeat + interpreter-state overlay |
| Deterministic oracle testing | ✅ record/replay + diff vs the original's own code |

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
  - **Tempest 2000** (Jeff Minter / Llamasoft, Jaguar) — grew from a vector
    tube prototype into a full reimplementation tested on real hardware: level
    data extracted verbatim from the annotated Jaguar source, **SSF banking** with
    a boot-time page self-test (4 MiB and 8 MiB builds), an on-SH-2 ProTracker
    player — and the discovery that **interrupt-per-sample PWM was silent on a real
    32X**, fixed with the d32xr **slave-DMA** scheme.
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
  - **Real-time strategy** (a Warcraft reimplementation) — host-tested A*
    pathfinding, three-state fog of war, deterministic-grid-with-interpolation,
    worker economy, and **battery-backed SRAM saves** with a pixel-compare
    restore test.
  - **First-person raycaster dungeon crawler** — quarter-res cast + 2×2 expand,
    per-column DDA, depth-sorted billboards, and **shade-bank palette fog**.
  - **Jet-ski racer** (HTML5 port) — pseudo-3D from pre-baked sprite angles, IMA
    **ADPCM** samples mixed on the slave SH-2, and a d32xr optimization audit.
  - **A PICO-8 conversion collection** — split-screen 2P, sprite-stacking / many
    baked angles, SRAM records/custom-tracks, CC-BY-NC-SA licensing.
  - **First-person brick-breaker** (Break Free) and a clean-room `arkanoid` twin
    — fixed-camera tunnel projection, dirty-rectangle rendering, the 60/n
    vblank-quantization model, an SDRAM beacon, and shade-LUT fog.
  - **Vertical shmups** (Raptor, Tyrian ep1-l1, Kiloblaster) — a Tiled-TMX→event
    pipeline and verified build reports; a **data-driven engine + 1,009-event
    level-VM** with an in-ROM verification accelerator and COMM telemetry; and a
    freeware→from-scratch rewrite with procedural sprites.
  - **A Three.js circuit racer** — an **original-code physics oracle** (`make
    oracle` diffs C vs the shipped JS), the **`make headroom`** continuous perf
    probe, a 12→30 fps optimization log, and a nine-item "verifies-clean-but-
    boots-black" catalog (CRAM bit 15, FM-before-handshake, security-checksum
    COMM8, a COMM-collision direct-colour doubled image, handedness).
  - **An RPG Maker 2000 fangame** — the **"don't port the interpreter"** archetype:
    a build-time LCF reader + event-page→bytecode compiler with a ~250-line
    runtime VM, autotile composition, and a median-cut global palette.
  - **A complete RM2K JRPG** (Raintown Slickers) — the archetype scaled to a whole
    game: all 20 maps, the **full event interpreter** (common-event calls,
    labels/jumps, flattened move routes), a **turn-based front-view battle**
    system, a field menu/party, **autotile → deduplicated atlas** (13,025 cells →
    243 tiles), a full **streaming-ADPCM music engine** (6 tracks streamed from
    ROM, mixed concurrently with SFX on the slave SH-2 with loop/fade/memorize),
    and the **heartbeat + interpreter-state** hang diagnosis and **debug warps**
    that go with a game that big.
  - **A large RPG port in progress** (Franzen) — the value is method, not a
    finished game: a **content-audit-first** pass (176 maps, 15,580 command
    records, 45 opcodes, 86.88% VM-covered → a bounded backlog), an honest
    "title-only is not done" status, the **ROM-banking / 32X-CD decision** for
    oversized content (~170 MiB source audio vs a ~4 MiB window), per-scene
    palettes, and **two-phase-commit SRAM saves**.
  - **Another complete RM2K JRPG** (Pail in the Court of the Demon King) — the
    reference for the RM2K **Pictures layer**: 33 dialogue portraits and cutscene
    sequences animated in software with runtime **zoom (100–1000%) and 0–100%
    transparency** (no blitter or hardware alpha — stipple/blend-LUT + fixed-point
    scaling), all 30 maps, front-view battles, a field menu, 27 PWM SFX, and an
    exemplary honest **Known gaps** list.
  - **Tux Racer** (a work-in-progress port) — its value is the test harness:
    instrumented builds on separate ROM names after a benchmark ROM shipped in the
    release slot, game-mode telemetry with transition asserts, and scene-content
    checks.
  - **Three DOS/J2ME ports** — **Alien Assault II** (translated from its freeware
    source: COMM boot-stage markers, harness audio capture, byte-identical pause
    checks), **Pacamaze** (reverse-engineered from the `.EXE` into a written spec,
    then a pixel-exact raycaster), and **Doom RPG** (a clean-room engine over the
    user's own `.jar`, with placeholder assets so it builds and tests with no
    proprietary data, and test scripts generated from the maps and checked against
    a Python model).

- **Five full-source workspaces studied in depth** (each bundles this skill as its
  workflow), contributing the most real-hardware findings so far:
  - **WipEout** (anti-gravity racer) — the **boot BIOS wipes SDRAM if a ROM has
    no initialised data**; C¹-continuous track slopes and a spring-damped camera.
  - **Mr. Boom** (8-player Bomberman) — a **hybrid renderer** that draws the world
    with the Genesis VDP and only the HUD on the 32X; contiguous sprite tiles,
    sprite tearing, a source rebuilt against its original data model, and arenas
    proven connected over seeds.
  - **Fight For Life** (Jaguar 3D fighter, 6 → 20 fps) — **SH-2 shifts compile to
    library calls** (9 → 12 fps from that alone), lock-free work stealing across
    both SH-2s, VDP auto-fill, slave-DMA audio at the Jaguar DSP rate, and a
    **song clock that froze the music at 66 s**.
  - **City Carnage** (a clean-room GTA-style sandbox) — an extruded fake-3D city,
    saves that worked only in emulators until moved off the SSF ROM windows and
    onto **EverDrive page 31**, a silent boot caused by bank-switching under
    running code, a beam-search ADPCM encoder that saved 2 MiB, a frame-time
    governor, and **colour-coded probe ROMs** for questions only hardware can
    answer.
  - **Blade Arena: Kengeki** (an original fighter built on the structures a
    Toshinden recompilation documents) — adaptive two-CPU banded rasterisation
    proven pixel-identical, the deferred flip, a PicoDrive cycle profiler, and a
    fully synthesised original soundtrack.

  Studying them also corrected the skill itself in three places: its
  "set CRAM bit 15 or the frame is invisible" rule (now the precise
  `bit 15 XOR PRI` compositing rule, checked against PicoDrive's compositor),
  its advice to feed PWM from a timer, and its ProTracker tempo note.

Most of these ports explicitly credit `sega-32x-skill-for-claude` as their
workflow — the skill is in real, repeated use, and its guidance is fed back from
what that use uncovers.

The skill also folds in a build-time **tool**, not just games: **midi2vgm**, a
MIDI → VGM (YM2612/PSG/DAC) converter that makes a game's MIDI score playable on
the Genesis side — closing the "music is a project of its own" gap those RPG
ports hit, with the hard chip lessons (per-part FM/PSG routing, the FM retrigger
gap, offline-pre-mixed DAC percussion, verify against a cycle-accurate core)
captured in `audio.md`.

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
  pretending. For **large data-driven games** it goes further — audit the content
  first to bound the work, decide **ROM banking or a 32X-CD build** before
  freezing the asset format when the honest total blows past the ~4 MiB window,
  and treat a **title-only ROM as *not* a finished port**.
- **Intellectual property is respected.** The default is to ship **clean-room**
  assets and provide a build step so a user converts original data **they own**
  (the WinWar / user-supplied `DATA.WAR` model, and the same for RM2K RPG ports
  where the ROM is a personal conversion of a game the user supplies). The skill
  describes techniques; it doesn't redistribute other games' code, art, or music.
- **The devkit compiler has known traps.** GCC 12.1 for SH-2 miscompiles a few
  patterns (12-byte struct returns, 64-bit multiply chains, dropped stores, calls
  across mixed optimization levels). The skill lists them and writes around them
  (pointer math, MAC macros, one optimization level) rather than being surprised
  by a "correct" program that misbehaves only on hardware.
- **The emulator is not the console.** PicoDrive models neither the SH-2 cache,
  SDRAM wait states, RV bus contention, nor the 32X free-running timer, and real
  ports have shipped bugs that only hardware showed: interrupt-driven audio that
  was silent, a random silent boot from a bank switch, saves that never persisted.
  The skill marks which lessons were hardware-confirmed and which are
  emulator-verified only. One question stays open on hardware: whether Genesis
  graphics can show *under* the 32X layer (one port relies on it in PicoDrive,
  another's hardware probe was inconclusive). The skill says how to settle such a
  question with a colour-coded probe ROM rather than guessing.
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
