## What it does

Point Claude at any of these and this skill drives the work:

- **Make a new 32X game** in any of five rendering families:
  - **Software-3D polygon** — fixed-point transform, reciprocal-divide
    projection, flat-triangle rasterizer (a ready-made engine ships in the
    skill). Games built: a rally racer, a rail shooter, kart racers.
  - **Voxel landscape** — a Comanche-style scrolling heightmap of
    perspective-scaled cells (cell-billboard *and* per-column-raycaster methods,
    with the compute-vs-fillrate trade documented). Game built: a voxel shmup.
  - **Raycaster (first-person)** — quarter-resolution cast with hardware-divider
    DDA per-column, 2×2 aligned expansion, depth-sorted billboards, and
    shade-bank palette fog. Game built: a dungeon crawler.
  - **Strategy/grid-based** — host-tested A* pathfinding, fog of war, RTS state
    machines, data-driven level events, and deterministic grid logic with
    interpolated rendering. Games built: Warcraft-style RTS, city-builder sim.
  - **2D sprites** — packed 8bpp framebuffer plus scanline shape fills
    (triangle/circle/polygon), menu/flow state machines, faithful-graphics
    reconstruction, and split-screen 2P. Games built: a faithful vertical shooter,
    Arkanoid-style breakout.
- **Pseudo-3D from pre-baked sprite angles** — sprite stacking and rotation
  techniques lifted from shipped 32X ports.
- **Port a game to the 32X** — from DOS, Genesis/Mega Drive, a **PICO-8** cart,
  an HTML5/JS game, a C/Pascal codebase, or a reverse-engineered original —
  including the **licensing model** for open-source (GPL, AGPL, BSD-2, CC0,
  CC-BY-NC, CC-BY-NC-SA) and proprietary targets with reverse-engineered data.
- **Use the second SH-2** — dual-core work via the COMM mailbox (offloading the
  framebuffer clear, audio mixing, IMA ADPCM sample decoding, or a render phase).
- **Add sound** — PWM audio with a software voice mixer, 8-voice S3M-style
  tracker playback with priority SFX, and Genesis-side XGM/SGDK (YM2612 + PSG
  via 68000+Z80 with SH-2 UI over COMM).
- **Save and restore game state** — battery-backed cartridge SRAM with
  checksummed format, corruption detection, and boot-validated Continue.
- **Optimize SH-2 code** — fixed-point, table lookups, killing divides,
  divide-hoisting, fillrate/overdraw reduction, vblank-quantized pacing,
  dirty-rectangle compositing with per-framebuffer dirty lists, cache handling —
  with a method to **measure effective framerate** through the video test
  harness first.
- **Debug a black-screen or crash**, verify static ROM structure, or **set up
  automated PicoDrive tests** with deterministic replay, oracle record/replay,
  and in-ROM fast-forward verification.

It triggers on things as short as "port X to 32X", "make a 32X game",
"3D/voxel/raycaster/strategy on 32X", porting a PICO-8/HTML5/DOS game, a
mention of a `.32x` ROM, `mars.ld`, dual-SH2 / 68000 Mars hardware, VDP
framebuffer/palette issues, PWM audio, or save/battery SRAM.
