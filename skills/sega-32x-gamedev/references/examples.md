# Worked examples: real 32X ports to learn from

Beyond d32xr, haroldo-ok has shipped many complete 32X games using the same
Chilly-Willy-lineage toolchain and a similar automated-test methodology to this
skill. They're the best available reference when you hit a concrete problem —
study them for **technique**, respect their licenses, and copy neither their
code nor their embedded game assets.

## By rendering / engine technique

- **Filled-polygon software 3D** — this skill's `assets/3d/` engine; shipped in a
  rally racer and a rail shooter. Free-camera 3D geometry.
- **Mode-7 / pseudo-3D floor (scanline sampling)** — perspective floor via a
  per-scanline depth LUT, scaled sprites for objects. See `apex-vector-60-32x`
  (PICO-8 Mode-7 racer; `z_fov_table`, divide-free inner loop) and
  `cannonball-outrun-32x` (OutRun-style segmented road with forks/themes).
  `skyroads-32x` is a 1:1 reverse-engineered pseudo-3D port.
  `hexgl-32x` ports a WebGL anti-grav racer.
- **Wireframe / vector** — project verts, draw edges with a line routine; no fill
  or sort. See `wirefight-32x` (`gfx_line`, `src-md`+`src-sh2` split) and
  `xquest-32x`.
- **Sprite/tile 2D** — `hocus-pocus-32x` (DOS platformer), `warcraft-32x` /
  WinWar (RTS: tilemaps, many sprites, menus), `pong-kombat-32x` (the HAL
  lineage this skill's vendored `mars.c`/`crt0.s` descend from).

## By port source (how to approach each)

- **HTML5/JS or small C game** → native C/SH-2 rewrite. `crazy-racing-32X`.
- **WebGL/GPU game** → reimplement the *design* in software; you can't port the
  GPU path. `hexgl-32x`.
- **DOS game with a data file** → convert the data to a linear ROM archive at
  build time (see porting-workflow.md); keep only mutable state in SDRAM.
  `god-of-thunder-32X`, `hocus-pocus-32x`, `speed-haste-32x`.
- **Game the user owns the data for** → ship no copyrighted data; read a
  **user-supplied** file. `warcraft-32x` uses the user's `DATA.WAR` and *no DOS
  executable code* — the model to follow for new ports (see the IP note in
  porting-workflow.md).
- **Reverse-engineered 1:1 port** → disassemble, reimplement formats/algorithms
  portably. `skyroads-32x`.

## Audio

- **PWM Tracker 32X** (`tracker-player-32x`) — an eight-voice S3M-style software
  mixer through the stereo PWM FIFOs, priority SFX by voice-stealing, a source
  converter, *and a PCM-capture audio test*. The reference for `audio.md` and
  for the audio-verification method in `testing.md`.

## Worked example — a 2D vertical shmup (`shmup32x`)

A faithful clean-room port of a sanctioned HTML5 canvas shooter (the author
supplied the game and asked for the 32X conversion). No 3D engine — packed 8bpp
framebuffer plus the shape primitives from `2d-and-shmup.md`.

- Built in verified milestones: M1 core loop (starfield, player, typed enemies,
  collisions) → M2 swarms + a boss with radial/spread bullet patterns → M3
  power-ups (spread shot, shield) → M4 particle explosions + a full
  title/ship-select/difficulty/game-over flow → M5 PWM sound + in-session high
  score.
- Then a **faithful-graphics pass**: read the original canvas draw code, pulled
  exact hex colours and polygon vertices, and reproduced the four ships, typed
  enemy shapes, and winged boss with `GFX_FillPoly/FillTri/FillCircle`.
- Lessons it contributed: reset must zero whole entity pools (ghost-boss bug);
  event-driven sound to keep the game module HAL-free; menus need colour to pass
  the black-screen guard; update input scripts + markers when you add a menu.

## Worked example — a voxel-landscape shmup (`zepton32x`)

A GPLv3 clean-room port of REZ's *Zepton* (a Comanche-style "2d voxel shmup"
from a PICO-8 cart). Introduced the **voxel-landscape** renderer
(`voxel-landscape.md`) — a scrolling procedural heightmap drawn as
perspective-scaled cells with a hoisted per-slice reciprocal divide.

- Milestones: M1 scrolling voxel terrain → M2 player ship (original voxel model)
  + lock-on reticle → M3 into-the-screen bullets + homing missiles → M4
  approaching enemies + reticle lock + scoring → M5 blue/red bonuses + laser +
  energy/game-over → M6 PWM sound (engine hum + fire/boom/hit) + title screen.
- A dedicated **performance pass** (measured, not guessed) and an honest
  **per-column raycaster experiment that was measured slower and not shipped**
  (see `optimization.md` and `voxel-landscape.md`).
- IP: kept GPLv3, credited REZ, procedural terrain + original ship (Zepton's
  CC-BY-NC art not copied). See `porting-workflow.md`.
- Also the source of the "rebuild from a known-good tree" black-screen fix in
  `testing.md`.

## Studied ecosystem — shipped 32X ports to mine for technique

A catalogue of real, working 32X ports (haroldo-ok's, studied for technique with
attribution — reproduce the *methods*, honour each project's own upstream
licence). Grouped by what they best demonstrate.

### PICO-8 ports (see `pico8-porting.md`)
- **apex-vector-60-32x** — a PICO-8 **Mode-7 racer**. Best reference for the
  `pico8_api` compat layer, scanline depth LUT (`z_fov_table`), tile-lookup
  bitshift/LUT, fixed-point sprite stepping, 32-bit-aligned framebuffer writes,
  and attract/demo mode. https://github.com/haroldo-ok/apex-vector-60-32x
- **hit8ox-32x** — a PICO-8 **3D fighting game**; free 3D arena, 117
  keyframe-interpolated poses, per-body-part colour customization; deliberately
  silent (original never called `sfx()`). https://github.com/haroldo-ok/hit8ox-32x
- **picohot-32x** — a PICO-8 **SUPERHOT** homage; real 3D floor/walls/panes +
  box/pyramid meshes, time-scales-with-movement, 160×112→320×224 doubling.
  https://github.com/haroldo-ok/picohot-32x
- **trial-of-the-sorcerer-32x** — a PICO-8 **raycaster** dungeon crawler; strafe
  controls; source of the `rom/obj` snapshot-exclusion + `setup.sh`-reinstall
  gotchas. https://github.com/haroldo-ok/trial-of-the-sorcerer-32x
- **pico-city-builder-32x** — a PICO-8 **city-builder sim**; grid sim, BFS road
  pathfinding, day/night palette shift, 4-voice PWM, ~7 KiB SDRAM.
  https://github.com/haroldo-ok/pico-city-builder-32x

### Software-3D games (see `software-3d.md`)
- **tempest-2k-32x** — a **vector tube shooter**; 16 3D webs, translucent lane
  polys, dual-SH2 (slave = real-time PWM synth + 140 BPM techno loop).
  https://github.com/haroldo-ok/tempest-2k-32x
- **fighting-game-3D-32X** — a DirectX **3D fighter** retarget; clean-room
  flat-shaded polys, mocap-baked poses with `THIRD_PARTY_NOTICES`/`MOCAP_BAKE`,
  painter+backface no-z-buffer, slave-SH2 COMM clear.
  https://github.com/haroldo-ok/fighting-game-3D-32X
- **beachy-beachy-ball-32x** — an AGPL **3D marble** physics game; the reference
  for the **GCC 12.1 SH-2 miscompile traps**, the desktop-oracle record/replay
  determinism model, and 30 Hz fixed-tick pacing.
  https://github.com/haroldo-ok/beachy-beachy-ball-32x
- **pifox-32x** — a **rail shooter** (PiFox); 3D mesh conversion, PNG→CRAM UI,
  and PCM assets streamed/mixed through PWM.
  https://github.com/haroldo-ok/pifox-32x

### Audio players (see `audio.md`)
- **tracker-player-32x** — an **8-voice S3M-style tracker** mixed to the stereo
  PWM FIFOs at 11,025 Hz with priority SFX. The key PWM-audio reference.
  https://github.com/haroldo-ok/tracker-player-32x
- **xgm-player-32x** — a **Genesis-side XGM/SGDK** player (68000+Z80 driving
  YM2612/PSG) with the UI on the SH-2, cooperating over COMM registers.
  https://github.com/haroldo-ok/xgm-player-32x

## Verified full-source ports built *with* this skill

Three complete, statically- and emulator-verified ports whose full source was
studied here (all credit `haroldo-ok/sega-32x-skill-for-claude` as their workflow
guide — this skill, in use). They are the highest-fidelity worked examples
because the techniques below are read from shipping code, not a README summary.

- **raptor32x** — a GPLv3 DOS **vertical shmup** port (*Raptor: Call of the
  Shadows* clone). Fixed-tick state machine, fixed pools (20 enemies, 64+64
  shots, 24 explosions, no heap), packed 8bpp, a **Tiled TMX → 299 sorted
  mission-events** asset pipeline with a shared 256-colour palette (index 0
  transparent), slave-SH-2 PWM effects, 68000 pad-poll + frame-heartbeat over
  COMM, and a full `BUILD_REPORT.md` (SHA-256 + checksum + PicoDrive video/PCM
  table). Started from the MIT hexgl-32x boot foundation. See
  `porting-workflow.md` (TMX, build report, boot foundation) and `audio.md`.
- **arkanoid32x** — a **first-person tunnel breakout** (*Break Free*-style, no
  sound). The reference for **fixed-camera tunnel projection** (`software-3d.md`),
  the **7 → 60 fps** optimization story (precomputed edge slopes → rasterise-once
  → **dirty-rectangle compositing with per-framebuffer dirty lists + HUD
  content-hash caching**), the **60/n vblank-quantization** model, the
  **`make fillcount`** host profiler (all in `optimization.md`), a ~22,000-assertion
  host test with a scripted perfect-player and an anti-tunnelling swept-collision
  check, an **SDRAM liveness beacon** read by the harness, and a corner-screenshot
  geometry gate (all in `testing.md`).
- **tyrian-32x (Episode 1, Level 1)** — a GPL **OpenTyrian** port that grew from
  an M1 vertical slice into a **complete scripted level**: all **1,009 serialized
  level events** (typed opcodes: spawns, formations, linked groups, velocity/
  acceleration changes, fire overrides, flags, conditional skips, boss, end)
  walked against scroll position by an **event interpreter**, driven by the
  original's **data tables in ROM** (851 enemy, 781 weapon, 43 weapon-port
  records) for repeat-rate/multishot/damage/pierce/homing behaviour — plus a
  loadout/shop, collectibles/economy, and a 19-section ~559 KB ROM asset bank.
  The reference for: the **GPL-engine-but-proprietary-data** IP trap (Epic-EULA
  data; ship the converter, not the ROM — `porting-workflow.md`), the
  **data-driven engine + level-event VM** pattern (`porting-workflow.md`), the
  **`.sdata @progbits` vector-copy black-screen invariant** with a raw-ROM
  reset-vector verifier (`toolchain-and-build.md`), and — for testing long
  content — an **in-ROM verification accelerator** and **COMM-register telemetry**
  asserting exact end-state (`testing.md`). Seven PicoDrive scenarios run through
  boss and `LEVEL COMPLETE`.

## More studied ports — new genres & capabilities

- **warcraft-32x** (WinWar) — a native **real-time strategy** port (MIT WinWar
  reimplementation; user-supplied Warcraft `DATA.WAR`). The reference for
  `strategy-and-grid.md` (host-tested **A\*** with octile heuristic + no
  corner-cutting + footprints + replanning, **three-state fog of war**,
  deterministic-grid-with-12-frame-interpolation, RTS aggro/economy/construction)
  and for **battery-backed SRAM saves** with a pixel-compare restore test
  (`architecture.md`). `DATA.WAR` LZSS decode + IP model in `porting-workflow.md`.
  https://github.com/haroldo-ok/warcraft-32x
- **noudar-32x** (Dungeons of Noudar 3D, BSD-2) — a first-person **raycaster
  dungeon crawler**: quarter-res cast + 2×2 aligned expand, per-column DDA with
  the hardware divider, textured flats, depth-sorted billboards, and **shade-bank
  palette fog** (`software-3d.md`); turn-based grid rules with a desktop-render
  oracle + SDRAM/COMM beacon (`strategy-and-grid.md`, `testing.md`).
  https://github.com/haroldo-ok/noudar-32x
- **wave-rider-gp-32x** (HTML5 jet-ski racer) — **pseudo-3D from pre-baked sprite
  angles** (46 OBJ models → 8×48×48 sprites), **IMA ADPCM** sample banks mixed on
  the slave SH-2 (`audio.md`), 30 Hz fixed physics decoupled from a variable
  renderer, a full d32xr **optimization audit** (`optimization.md`), CC0 art +
  provenance fingerprints. 25-scenario PicoDrive suite incl. steering-polarity
  telemetry. https://github.com/haroldo-ok/wave-rider-gp-32x
- **dmar-daytripper-conversions-to-32x** — a **collection** of native PICO-8 ports
  (Pico Racer 2048, Death Dash Crash, Mr. Boom, HIT8OX): **split-screen 2P**
  (`2d-and-shmup.md`), **sprite-stacking / 64-angle** cars (`software-3d.md`),
  SRAM records/custom-tracks, Cohen–Sutherland clipping, CC-BY-NC-SA
  (`porting-workflow.md`, `pico8-porting.md`).
  https://github.com/haroldo-ok/dmar-daytripper-conversions-to-32x
- **breakfree-32x** (Break Free, DOS) — the first-person brick-breaker whose data
  (`BRKFREE.MLB`) is decoded at build time into two asset banks; source of the
  **shade-LUT fog** formula (`software-3d.md`). The clean-room `arkanoid32x` above
  is modelled on this. https://github.com/haroldo-ok/breakfree-32x
- **kiloblaster-32x** (Kiloblaster, freeware 1992) — a Galaxian-style shooter as a
  **from-scratch C rewrite with procedural sprites** (not a source paste, no DOS
  VGA assets) on the MIT hexgl boot foundation — the "freeware original" IP case
  in `porting-workflow.md`. https://github.com/haroldo-ok/kiloblaster-32x
