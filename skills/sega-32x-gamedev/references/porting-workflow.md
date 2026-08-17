# Porting workflow

Goal: take a game written for some other platform (DOS/x86 real-mode, C,
Turbo Pascal, a Genesis title, an emulator core, a modern C/C++ engine…) and
produce a playable, verified `.32x`. The method below is what the reference
ports (SkyRoads, Pong Kombat, God of Thunder, etc.) actually use.

## Principle: platform-clean core + thin 32X shell

Split the game into two layers on day one:

- **`src/core/`** — portable C11 with **no OS calls, no floating point in the
  hot path, endian-clean**. Game state, physics, AI, level logic, and the
  software renderer that fills an indexed-color buffer. This layer must build
  and run on a normal PC too.
- **`src/platform/<target>/`** — thin shells that provide the core with input,
  a framebuffer, audio, timing, and file/asset access. You will have at least
  the `32x` shell; add an `sdl` desktop shell as a **test oracle**.

Why the split matters: the SDL build lets you debug game logic on a fast
machine with a debugger, and lets you diff the port against the original with
identical inputs. Bugs that would be black screens on hardware become ordinary
assertions on desktop. The 32X shell then stays small and hardware-focused.

## Principle: one small, verified milestone at a time

The reliable rhythm for building *or* porting a 32X game — the one that carried
the shmup and voxel builds — is a tight loop, one shippable slice per iteration,
never a big-bang integration:

1. **Write the feature as a HAL-free module** (`feature.c/.h`): pure C11, no
   `mars.h`, no framebuffer — just the game rules (spawn, move, collide, score).
   Rendering happens later in `main`; the module only computes.
2. **Unit-test it on the host** (`tests/test_feature.c`, compiled with the
   system `cc`): assert the rules — an entity moves the right way, clamps at
   bounds, a hit kills and scores, the fast path matches the reference within
   1px. This runs in milliseconds and catches logic bugs the emulator can't show
   you clearly.
3. **Wire it into `main`** and draw it (the only place that touches the HAL),
   keeping event-driven sound and per-object distinct palette colours.
4. **Verify in the emulator**: a `tests/scripts/*.txt` input script + PPM capture
   + a pixel/colour assertion, then *look at a screenshot* to confirm it reads
   right.
5. **Update the README, package, and present** — then start the next slice.

Each milestone ends green (host tests + `verify_rom` + PicoDrive) before the next
begins. When something breaks, only the last slice can be responsible. This is
also what keeps claims honest: you only call a thing "done" once its host test
and its on-screen capture both pass. See `testing.md` for the harness and the
black-screen debugging ladder.

## Step 1 — Get the source and understand the target

- Fetch the original source and/or data. State the license situation honestly:
  ship code and build-time converters, but **do not commit the original game's
  copyrighted data**; have the user supply it and document where it goes
  (this is exactly how the reference ports handle SkyRoads/Pong Kombat data).
- Identify the **tick rate** and the **timing model**. DOS games often
  reprogram the PIT; e.g. SkyRoads advances at ~36.0036 Hz derived from a
  180 Hz interrupt. Capture the exact rational (num/den) and drive the core at
  that average rate with an accumulator — do not just run "once per frame".
- Identify the **rendering model** (tile? raster? 3D? framebuffer blit?) and
  the **asset formats** (compression, palettes, sample formats, endianness).

## Step 2 — Stand up the portable core + desktop shell first

Before touching the 32X:

1. Port the game logic into `src/core/` as portable C11.
2. Write `src/platform/sdl/main_sdl.c`: create a window, feed the core inputs,
   present its indexed framebuffer through a palette, drive the tick with the
   rational accumulator.
3. Get it *playable on desktop*. This is your oracle and your fast iteration
   loop. If the original ships a demo/replay recording, feed it in and confirm
   the run reproduces — that is a strong regression test.

If the engine can't be brought up all at once, extract a **self-contained
slice** (one subsystem plus its real data — e.g. an animation/state-machine
interpreter) as HAL-free C that compiles for both host and target, and pin its
behaviour with a host unit test before cross-compiling. Real logic verified on
the host is logic you don't have to debug through the emulator later.

## Step 3 — Convert assets at build time

Write Python (or C) tools under `tools/` that turn the original data into
32X-ready, big-endian blobs decoded into ROM:

- Graphics → indexed pixels + a CRAM palette (256 entries, or your subframe).
- Music → **VGM 1.50** for the 68000 YM2612/PSG player (map channels, envelopes,
  loop points; keep the original tick cadence).
- Sampled audio → raw PCM with known sample rate for the slave-SH-2 PWM mixer;
  normalize disparate levels.
- Level/data tables → packed, byte-swapped structs.

Emit these as `.c`/`.s` blobs (or `.incbin` binaries) linked into ROM so there
is **no runtime file I/O and no big heap allocation**. Make regeneration a make
target guarded by a stamp file.

## Step 4 — Bring up the 32X shell incrementally

Do these in order and run the emulator test after each — resist the urge to
wire everything at once, because a black screen with ten new subsystems is
un-debuggable.

1. **Boot + solid color.** `Mars_Init`, `Mars_InitVideo`, fill the framebuffer
   with a non-zero palette color, flip. The emulator test should now see a lit,
   single-color frame. If it is black, the problem is boot/VDP/palette, not your
   game — fix it here.
2. **Palette + a static image.** Seed the real palette, blit one converted
   image. Confirm colors (not silhouettes). If shapes are black, the palette is
   not loaded.
3. **The core's renderer → framebuffer.** Point the core at the 32X back buffer.
   Confirm the title/first screen matches the desktop oracle.
4. **Input.** Wire `Mars_ReadController` (masked to 3-button) to the core's
   input. Add a scripted test that presses Start/d-pad and asserts state
   transitions.
5. **Timing.** Drive the core tick from `mars_vblank_count` with the same
   rational accumulator as the SDL shell so game speed matches the original.
6. **68000 music.** Embed the VGM blob + player; verify audio is non-silent in
   the test (peak/energy thresholds).
7. **Slave-SH-2 audio (PWM) and/or a render phase.** Offload via COMM. Verify
   SFX presence and that VBlank service never stalls.

## Step 5 — Verify, don't hope

Wire up `assets/verify_rom.py` (static) and `assets/run_tests.py` +
`harness.c` (headless PicoDrive) as described in `references/testing.md`. Add a
point-to-point script per real state: boot → title → menu → gameplay, plus
regressions for anything you fixed (e.g. the d-pad-as-jump masking bug).

## Step 6 — Optimize

Only after it is correct. See `references/optimization.md`. Common wins for a
fresh port: move audio mixing to the slave SH-2, switch float→fixed-point,
precompute tables, and shrink the internal render resolution.

## Common porting pitfalls

- **Endianness**: DOS/x86 data is little-endian; the SH-2 is big-endian.
  Byte-swap at conversion time, not scattered through the game.
- **Floating point**: fine on desktop, disastrous in 32X hot loops. Convert to
  fixed-point in the core so both shells share it.
- **`malloc` of large buffers**: 256 KiB SDRAM disappears fast. Keep big data
  in ROM.
- **Assuming one frame == one tick**: decouple rendering from the fixed logic
  tick or the game will run at the wrong speed.
- **6-button pad bits**: mask them (see architecture.md, Controllers).
- **Shipping copyrighted data in the repo**: don't; convert at build time from
  user-supplied originals.

## Converting original game data (DOS/other) to a ROM archive

When porting a game that ships a data file (levels, tiles, sprites, tables),
convert it **once at build time** into a linear, ROM-embeddable archive rather
than parsing the original format at runtime. This is how God of Thunder 32X
handles its resource file (`tools/mkassets.py`); the pattern generalizes:

1. **Decompress / unpack** any compressed payloads (e.g. LZSS) in the build tool.
2. **De-plane bitmaps.** DOS Mode-X and EGA/VGA planar art is stored in 4 bit
   planes; convert it to **linear chunky** pixels (one byte per pixel) matching
   the 32X's 8bpp packed framebuffer, so blitting is a straight copy.
3. **Byte-swap 16-bit fields to big-endian.** The SH-2 is **big-endian**; DOS
   data is little-endian. Any `s16`/`u16`/`s32` field you want to read directly
   as a C struct/array from ROM must be byte-swapped when packing. If you skip
   this, values read as byte-swapped garbage on hardware — and it won't show on
   a little-endian host test. Swap the halfwords at pack time and the C side can
   read the archive in place with no runtime conversion.
4. **Lay it out for read-in-place.** Emit a simple header + offset table so the
   game reads assets straight from ROM (`0x02000000`, cacheable, read-only).

**Memory payoff:** assets that live in ROM cost **zero** of the 256 KB SDRAM.
Only *mutable* state (the current level's changeable tiles, actor state, etc.)
goes in SDRAM. This is what lets a multi-megabyte game fit — the ROM can be up
to a few MB while RAM stays tiny. Budget RAM for mutable state only.

**IP note:** this describes the *technique*. Whether you may embed a specific
game's converted assets in a distributed ROM depends on that game's license —
some of these examples use public-domain source and/or shareware data the author
had rights to. The conservative default for new work remains: ship clean-room
assets, and provide a build step so a user converts original data **they own**.
The clearest model is WinWar / `warcraft-32x`: it ships **no** copyrighted
executable code or assets and reads a **user-supplied** `DATA.WAR` at build
time. Follow that pattern — reimplement the engine, and have the user bring the
data file they legally possess.

## Licensing & IP when porting someone else's game

Ports touch other people's copyrighted work. The mechanics of a game aren't
copyrightable, but its **code, art, music, and specific level data** are. Sort
the target before writing code:

- **Open-source code (GPL/MIT/etc.)** — legitimate to port. For **GPL**, the
  port is a derivative work and must **stay GPL** and credit the author. Study
  the source and **reimplement the algorithm in your own C** (a port), rather
  than pasting the original verbatim into your output. Examples ported this way:
  a GPLv3 PICO-8 voxel shmup (`zepton32x`, Zepton by REZ).
- **Asset licenses are separate from the code license.** Zepton's code is GPLv3
  but its assets are **CC BY-NC 4.0**. For CC-BY-NC: attribute, keep the port
  **non-commercial**, and prefer **shipping original/procedural assets** over
  copying the licensed art. Zepton's terrain is procedural and its ship is an
  original voxel model — no CC-BY-NC art was copied.
- **Sanctioned / user-supplied** — the clearest case: the rights-holder gave you
  the game and asked for the port (e.g. an author who DM'd their HTML5 game for a
  console conversion, `shmup32x`). Reproduce it faithfully; still ship no
  third-party assets you don't have rights to (often there are none — canvas/
  procedural games have no asset files).
- **Proprietary, no license, ripped assets** — decline. A faithful port of an
  active-IP commercial game bundling ripped art/audio is not something to build;
  offer a clean-room original in the same genre instead.

Practical rule: **ship only assets you have the right to redistribute** — either
original/procedural, or faithfully reproduced when the source is open/sanctioned
and its license is honoured (keep GPL, attribute, respect NC). State the licenses
and attribution in the project README.

## Third-party assets inside a port (mocap, fonts, sampled audio)

Ports sometimes need assets from a *different* source than the game itself — free
mocap for a fighter's animations, a font, sampled audio. Keep provenance clean:

- **Don't ship the raw source assets** you only used at build time. A DirectX
  fighter port baked its walk/attack poses from free CMU/RancidMilk **mocap** but
  ships only the derived keyframes, not the motion-capture files, and documents
  the pipeline in `docs/MOCAP_BAKE.md` + a `THIRD_PARTY_NOTICES.md`.
- **Bundle no textured/copyrighted menu art**; reconstruct UI with clean-room
  polygons/fonts matching the original's layout.
- **AGPL** targets (a marble game) behave like GPL for distribution — keep the
  port under the compatible copyleft licence and publish source.
- Record every third-party asset's licence and attribution in the repo
  (`THIRD_PARTY_NOTICES.md` / `CREDITS.md`), and never treat an example ROM that
  embeds someone's music as a licence to redistribute that music separately.

The through-line with the open-source rules above: **ship only what you have the
right to ship**, derive-and-document when you transform third-party data, and
prefer clean-room reconstruction whenever the licence is unclear.

## A GPL engine does NOT make the game's data redistributable

The single most important licensing trap when porting: **the engine's licence and
the game's *data* licence are independent.** OpenTyrian's engine is GPL, but
Tyrian 2.1's data ships under a restrictive Epic MegaGames EULA — so a Tyrian
port can be GPL *source* while its **converted asset bank and the final ROM must
not be redistributed**. Practical policy (as a shipped Tyrian M1 does it):

- Ship the **converter and code**; have the user run `make setup-data` to fetch
  the original archive themselves, and **build the asset bank + ROM locally**.
- **Exclude the generated data blob and the `.32x` from source control and
  releases** (`obj/generated/*.bin`, `rom/*.32x`) — they contain transformed
  proprietary data.
- Say so in `LICENSES.md`: "do not publish the generated asset bank or ROM merely
  because the engine source is GPL." Point users at the data's own `license.doc`.

This is the same principle as CC-BY-NC assets under GPL code, stated at full
strength: **redistribute only what each individual input's licence permits**, and
when the data is proprietary, ship the pipeline, not the product.

## Converting Tiled (TMX) maps into a level/event stream

Tile-based ports frequently start from a **Tiled `.tmx`** map. A clean build-time
conversion (as a Raptor port does): parse the CSV **tile layer** into a compact
tilemap array, and parse the **object layer** into a sorted **event stream** —
each object becomes `(trigger_row, column, type, difficulty, gang)`, sorted by
trigger then group, emitted as a `const LevelEvent[]` in ROM. The scrolling game
then just walks the event list against the scroll position to spawn waves. Keep
the map/events in ROM (`const`), not RAM.

## Ship a verified-build report

For a release, emit a `BUILD_REPORT.md` / `VERIFICATION.md` recording exactly
what was verified, so "it works" is auditable: ROM **size + SHA-256**, the
Mega Drive/32X **header checksum**, `.text`/`.bss` sizes and the **`.bss` end vs
the SDRAM stack guard**, the toolchain revision, the static-verification
checklist, and a **table of PicoDrive scenarios** with their video metrics
(distinct colours, lit ratio, frames-changed) and, where relevant, **PCM audio
metrics** (peak amplitude, non-zero sample ratio). Two shipped ports (Raptor,
Tyrian) include exactly this; it's the difference between "verified" as a claim
and as a record.

## Start from a known-good boot foundation

Rather than scaffold SH-2/68000 startup from scratch, copy a **known-good,
permissively-licensed boot foundation** and adapt it — e.g. the MIT
`haroldo-ok/hexgl-32x` crt0/startup/linker/ROM-fixup, or the Pong Kombat 32X
scaffold. This is the constructive version of the "rebuild from a known-good
tree" fix in `testing.md`: begin from bytes that already boot, then bring your
game up on top, and record the foundation + revision in `SOURCE_PROVENANCE.md`.

## Data-driven engines: reproduce the original's tables, interpret its scripts

Many classic games are **data-driven** — their behaviour lives in tables and a
level-script bytecode, not in code. The faithful (and compact) way to port them
is to **convert the original's data tables verbatim into ROM `const` arrays** and
write a small **interpreter** that consumes them, rather than hand-porting every
behaviour. The Tyrian ep1-l1 port does this at scale:

- **Tables in ROM, not logic in code.** The converter emits the original's
  records unchanged — 851 enemy definitions, 781 weapon-pattern records, 43
  weapon-port records — into a sectioned ROM asset bank. Weapon behaviour
  (repeat rate, multi-shot pattern, damage, acceleration, animation, piercing/
  freezing, homing) is *read from the record*, so adding a weapon is adding data,
  not code. Immutable → stays in cartridge ROM; SDRAM holds only the live pools.
- **A serialized level-event interpreter.** The level is a **sorted event stream**
  keyed on scroll position (Tyrian: 1,009 events from location 0 to 8,100), each
  event a small typed struct `{time, type, dat…}`. A `switch(type)` VM executes
  them as the playfield scrolls past their trigger: spawn/formation/linked-group,
  background-speed and slow-scroll regions, global velocity/acceleration changes,
  per-enemy fire overrides, bank-selection validation, messages/flags/conditional
  skips, boss setup, and an explicit **ending opcode** that drives the
  level-complete transition. This is the Tiled-TMX→event-stream idea (above)
  generalised to the original engine's full authored timeline.
- **Interpret, don't reimplement, unknown opcodes safely.** Handle the event/
  record types the level actually uses; for ordering/presentation opcodes the 32X
  port doesn't reproduce (multi-layer draw order, starfield toggles, streamed
  music fades), make the case an explicit no-op with a comment rather than
  guessing — honest partial fidelity beats a wrong behaviour.
- **Expose progress as telemetry.** Publish `level_pos` and `event_index` (see
  `testing.md`) so a headless run can assert the whole stream executed
  (`event_index == total`) — the interpreter's own counters double as the test
  oracle.

The payoff: content scales as *data* (more levels/enemies/weapons = more
converted records) with fixed code and fixed RAM, and the port stays close to the
original's actual behaviour because it runs the original's actual tables.
