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
