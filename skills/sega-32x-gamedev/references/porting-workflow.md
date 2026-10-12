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

## Reverse-engineering a game's proprietary data format

Many DOS/retro games keep art/levels/audio in a custom archive. Porting
faithfully means **decoding that format at build time**, not redrawing assets. A
build-time converter (Python) that implements the original's container is the
right home for this — examples: an RTS decoding Warcraft's `DATA.WAR` (offset
table + a **4 KiB-window LZSS** decompressor, image/sprite/tileset readers, a
map/mission parser), a brick-breaker decoding `BRKFREE.MLB` into two SH-2 asset
banks, and a crawler parsing **40×40 ASCII level files** with the original
tile-property sheets. Steps that generalise:

1. Find the container's **offset/table of contents** and any per-entry
   compression (RLE/LZSS/LZW). Implement the exact decompressor.
2. Decode each resource type (palette, images, tiles, maps, sprites, audio) into
   plain arrays; **quantize into one 8-bit 32X palette** with index 0 reserved
   for transparency.
3. Emit `const` C tables / `.incbin` banks (see the data-driven-engine section);
   keep everything immutable in ROM.

Record the exact upstream revision and the container's role in
`SOURCE_PROVENANCE.md`.

## More licensing cases (know which one you're in)

Extending the IP model above with the cases these ports hit:

- **User-supplied game data, no original executable code** — an RTS ships the
  *converter and MIT-licensed reimplementation* but requires the user to provide
  their own Warcraft demo `DATA.WAR`; the ROM/asset bank built from it are not
  redistributed. Ship the pipeline, the user supplies the data (as with Tyrian).
- **BSD-2 code *and* assets** (a crawler): the cleanest case — keep the licence on
  the reimplemented rules and the converted assets, credit the author.
- **CC0 assets** (a Kenney art kit used by an HTML5 game): public domain, free to
  use/redistribute; still attribute as a courtesy and record provenance.
- **CC BY-NC-SA** (PICO-8 ports): attribute, **non-commercial**, and **ShareAlike**
  (the port carries the same licence). No paid downloads or repro carts.
- **Freeware-declared original** (a 1992 shooter released as freeware): even so,
  do a **from-scratch C rewrite of the gameplay** and use **procedural/original
  sprites** rather than pasting the original sources or shipping its VGA assets —
  cleaner rights and a better 32X fit. Keep attribution per the freeware grant.

Across all of them the rule is unchanged: **redistribute only what each input's
licence permits**; when unsure, ship the converter and the code, not the data.

## Don't port the interpreter — compile its content to bytecode + a tiny VM

When the "game" is really **content run by a big interpreter** (an RPG Maker
project under EasyRPG, a VN engine, a scripting-heavy game), do **not** port the
interpreter — a ~30 MB C++ player with SDL/audio/filesystem won't fit in 256 KiB
and won't parse chunk streams at 23 MHz. Instead, **move all interpretation to
build time and ship a compact bytecode plus a small runtime VM** — the exact
core/shell split, applied to the *script*:

1. **Read the original data format** in a build-time tool (a shipped Sonic RPG
   port wrote a from-scratch reader for RPG Maker 2000 **LCF** — `RPG_RT.ldb/.lmt`,
   `Map0001.lmu` — BER-encoded chunk streams, map layers, event pages, page
   conditions, command lists).
2. **Compile the event/command lists into a compact bytecode**: messages, face
   changes, choices with branch targets, switches/variables, conditional
   branches, item pickups, SFX cues, waits. Emit it as a `const` blob in ROM.
3. **Write a tiny interpreter** for *just the opcodes this game uses* — the Sonic
   RPG runtime is ~250 lines of C, the slice of the RPG Maker event engine the
   game actually exercises (page conditions like `switch on`/`item held`,
   action-key vs player-touch triggers, parallel-process pages).

The whole game — maps, NPCs, branching dialogue trees, conditional branches,
pickups — ends up as data + a small VM, which is testable (host-run the VM) and
tiny. This is the data-driven-engine pattern (above) taken to its conclusion:
interpret at build time, ship the residue.

### Tile/RPG asset-pipeline specifics

- **Autotile composition**: reproduce the engine's autotile rules (EasyRPG blocks
  A/B/C/E/F) in the converter so terrain matches the original; pull the chipset
  **passability** table from the database for collision.
- **One global 256-colour palette** built by **median-cut per asset group** then
  **exact nearest-colour mapping**, every image baked to 8bpp indices (index 0
  transparent). Cut character sprites (e.g. 24×32, 4 dirs × 3 frames), face
  portraits, the panorama, the title.
- **Keep the header honest**: a stock MD header's ROM-end word (`0x1A4`) hardcodes
  4 MiB and fails static verification — rewrite it to the true size in `romfix`.

## Scope a large content port with an audit *before* writing runtime

For a big data-driven game (a 176-map RPG), don't start coding the runtime — first
**audit the whole project and measure the work**. A Franzen port ran
`tools/audit_game.py` to parse **every** map and inventory every event command
before implementation: 176/176 maps parsed, 2,159 events, 2,716 pages, **15,580
command records, 45 distinct command IDs**, and crucially **what fraction the
existing tiny VM already covers** (86.88%) versus the **hard remaining systems**
that can't be skipped in a faithful port (teleport/transfer, encounters/battles,
pictures, BGM, screen effects, shops, save/menu). That report (`content-audit.json`)
turns "port this RPG" from open-ended into a bounded, prioritised backlog, and it
tells you early whether the thing even fits (see the ROM-size decision in
`architecture.md`). Do this audit pass first; it's cheap and it prevents building
an asset format you have to throw away.

**Honest definition of done for a full game** (from that port's `PORT_STATUS.md`):
"a compiling or title-only ROM is not a completed port." A full RPG is done only
when it plays title→ending with map/event fidelity, battles, party/inventory
progression, usable audio, SRAM saves, and passing emulator tests. Track fidelity
as an explicit checklist and don't call a vertical slice the finished port — the
same "verified, not compiled" honesty at project scale.

## Porting a full RPG: the subsystems beyond the map VM

A complete RM2K-class JRPG (a shipped Raintown Slickers port does all of this)
needs more than the message/choice VM from `super-sonic-rpg`:

- **Fuller event interpreter**: on top of messages/choices/switches/variables/
  teleports/fades, real games use **common-event calls**, **labels/jumps**,
  **conditional branches**, **forced move routes** (flatten them at build time,
  resolve sprite ids), erase-event, gold/items/party changes, full-heal, and
  battle/return-to-title. Compile each page's command list to compact 4-byte
  bytecode `{code, indent, params[], string}` + a sentinel.
- **Autotile → a deduplicated tile atlas.** Evaluate RM2K autotile blocks (A/B/C/D
  and the 47/50 subtile variant tables) at build time and **dedup** the result:
  one game's 13,025 map cells collapsed to **243 unique 16×16 tiles**. Bake
  per-cell passability + wall + above-hero flags into **one byte** so the runtime
  never touches chipset tables.
- **Turn-based front-view battle** as its own scene: turn order by agility;
  Attack / Skill / Defend / Item / Escape; damage & skill formulas with
  criticals; EXP/gold, level-ups off the RM2K EXP curve; backdrop + monster art.
- **Field menu / party**: actor stats, equipment bonuses, usable items, skill
  lists, currency; database (actors/items/skills/enemies/troops/terms) emitted as
  `const` C tables.
- **Animated picture/overlay layer** (RM2K `ShowPicture`/`MovePicture`/
  `ErasePicture`): dialogue portraits and cutscene art shown with runtime **zoom
  (100–1000%)** and **0–100% transparency**, tweened over a duration. There's no
  blitter or hardware alpha, so do it in software — fixed-point source stepping
  for scale, stipple or a blend LUT for transparency, advanced by elapsed vblanks
  (see "Transparency and scaling in 8bpp indexed mode" in `2d-and-shmup.md`). A
  shipped port animates 33 portraits and dramatic zoom/fade sequences this way.
- **Preserve the original's timing regardless of render rate**: advance every
  timer (waits, move routes, the message typewriter, battle pacing) by the number
  of 60 Hz **vblanks each frame actually covered**, not once per rendered frame,
  so a ~28 fps scroll still keeps RM2K's real-time pacing. (The elapsed-vblank
  form of the fixed-tick rule.)

## Large games: palettes, map banking, and inheritance

Small ports get away with one shared 256-colour palette (a whole game in ~364
colours). A big game will not — plan **per-scene / per-region palettes or
palette-remapped asset groups**, decided up front. Likewise emit a **banked
multi-map directory with lazy map activation** rather than one flat map blob, and
**resolve RM2K map-tree inheritance** (music/background/permissions/encounters
inherited down the tree) into each generated map at build time.

## Porting from a binary-only original: write the spec first

When there is **no source** — only the DOS `.EXE` and its data — reverse-engineer
into a **written mechanics specification** before writing the port. A shipped
Pacamaze port (1995 DOS "Doom meets Pac-Man") disassembled `PACAMAZE.EXE`, kept
the dumps in `re/` and working notes in `decoded/`, and wrote
**`docs/GAME_SPEC.md`**: tick model (70 Hz), ghost AI (greedy), the
newest-bullet kill aura, the ammo economy, speeds, SFX trigger points, HUD text,
the renderer's exact distance/shade maths. The port is then implemented *against
the spec*, and the spec is what host tests assert. Keep reference screenshots of
the original running (`emu/`) to compare frames against.

Why the spec step matters: disassembly answers "what does this instruction do",
not "what is the rule"; writing the rule down forces the second question, and a
reviewer (or a later you) can check the port against prose instead of assembly.

## Reimplement the engine, extract the data verbatim

When the original's code targets hardware the 32X doesn't have, **don't
recompile it — rewrite the engine natively and lift the *data* verbatim.** A
Tempest 2000 port couldn't use the Jaguar source (Tom/Jerry GPU/DSP RISC and the
Jaguar blitter don't exist on the 32X), but extracted from the annotated source
all 37 web shapes, the 48-entry level→web sequence, the per-lane orientation
tables and the player's start lane (`tools/extract_webs.py` → `webdata.h`), and
reproduced tune selection and sound cues from the original's tables. Same idea as
the data-driven-engine section, applied when the "data" lives inside assembly
source rather than separate files. Doom RPG does the same from the J2ME release:
a clean-room engine, with maps/graphics/text/sound converted from the user's own
`.jar` (which is just a zip of `palettes.bin`, `levelNN.bsp`, …).

## Modal loops become flat state machines — guard the first tick

DOS games often implement sub-screens as **blocking loops**
(`while (!confirm) { draw(); poll(); }`). Ported to a one-tick-per-frame state
machine, the button press that *opened* the screen is still held on the next
tick and **immediately closes or confirms it**. The original got correct
behaviour for free from its modal loop; the port needs an explicit
**`just_opened` guard** (or a require-release-then-press edge) so the sub-screen
stays open until a *fresh* confirm (Alien Assault II's level-computer map /
store / statistics screens). Check every modal you flatten for this.

## Third-party sound packs: keep them replaceable

Two of these ports replace PC-speaker or silent originals with clips from a
large community mod's sound bank. That bank mixes material of mixed provenance,
and the ports themselves say the sounds **remain (c) their authors** and are
embedded **for personal use — replace before any commercial use**. If you do
this:

- Keep the clips **out of the engine**: a converter (`tools/mksfx.py`) that
  decodes, trims, DC-strips, normalises and resamples from the pack, plus a
  **`SOURCES.txt` recording the archive's checksum and each clip's path** — so
  the bank can be regenerated or swapped wholesale.
- Prefer sounds you can actually redistribute (your own, CC0, or the original
  game's own bank when the user supplies the game), and say plainly in the README
  which clips are third-party and under what terms.
- Don't treat "it's a free mod" as a licence; free-to-download is not
  free-to-redistribute.

## When the "source" isn't portable: rewrite against its data model

Mr. Boom's modern source is 533 KB of **machine-translated 16-bit DOS assembly**
(asm2c), a 22.8 MB embedded memory image with segment selectors and an emulated
x86 flag register — tens of megabytes of RAM against the 32X's 256 KiB. Porting it
as code was impossible; porting it as *design* would have lost the feel. The 32X
version instead **rebuilds the code against the original's data model**: one
global `struct Mem` keeping the original's fields, French names and numeric bands
(`truc`, `truc2`, `donnee`, `liste_bombe`, the bonus bag and its cursor), helpers
transcribed value for value (`framesToCrossACell()`, `bonusInCell()`), and the
bot's original four-state priority. Fidelity lives in the data and the rules, so
it survives a total code rewrite.

Getting assets out of such a source: the original sprite sheet was a page of the
embedded DOS heap, pulled out with a small documented extractor; screen geometry
was **measured off the author's own published captures**, not guessed; facing
direction was established by measuring where the eye pixels sit, not by eye.

## Units: re-express every duration and speed for the new tick rate

Several of these ports shipped bugs that were really unit errors:

- **Tick-rate changes**: Mr. Boom's bomb fuse is 256 of the original's ticks; the
  DOS build ran its logic much faster than 60 Hz, so taken literally at 60 Hz the
  fuse was **4.3 s** and players felt stuck. Re-express it in the new rate
  (`BOMB_FUSE_FRAMES = 150`, 2.5 s).
- **Fixed-point scale mismatches**: WipEout's missiles launched at 2,400 units/tick
  but homing used an unscaled 150, so a tracking missile slowed to a crawl and CPU
  ships (600–680) outran it.
- **Integer width**: City Carnage's attract bot never fired once, because
  `g_rand()` returns `uint16_t` and `(r >> 16) % 100` is always 0.
- **Sub-unit motion**: rounding the original's per-frame speed to whole pixels
  collapsed every speed below the base rate to 1 px/frame, so the slow disease did
  nothing; carry the fraction in the original's own sub-cell accumulator (8.8).

When porting, list every constant with a unit (ticks, frames, units/tick, Q-format)
and convert each one deliberately; grep for shifts and modulos on narrow types.

## IP routes that don't copy the original's code or assets

Two more shipped patterns, both with original names and assets:

- **Reimplement documented structures, ship your own game.** Blade Arena: Kengeki
  is modelled on the game structures that a **static-recompilation project**
  documents for Battle Arena Toshinden (player records and their offsets, the
  45-entry action table, the command matcher and its desperation gate, the
  ordering-table submit) — the recomp repository itself contains no game code. A
  `tsd_symbols.h` maps each original address/structure to the new code. Name,
  cast, models, animation, music and sound are all original and generated in the
  tree; the README states it is unaffiliated and contains no original code or
  assets.
- **Clean-room reimplementation of a design.** City Carnage re-derives GTA 1's
  design from Carnage3D (an open reimplementation that itself loads the retail data
  at run time and ships none); its cartridge carries **no GTA or Carnage3D assets**
  — everything is generated by the repo — with a mapping table and feature
  scorecard in `docs/REVERSE_ENGINEERING.md`.

And when the data *is* the original's (Fight For Life's models, motion capture and
music come from the user's own copy of the Jaguar source archive), say so in a
`THIRD_PARTY_NOTICES.md`, don't redistribute the archive, label the result an
**unofficial fan port**, and remove marks that imply endorsement — that port took
the console maker's logo off its title screen and put the original author first in
its credits. Record any **allowed reductions** explicitly (both fighters document a
"up to 40%" reduction brief and what was reduced).

## Converting another engine's maths conventions

Port the conventions, not just the numbers:

- **Matrix layout and axes.** The Jaguar fighter multiplies *row* vectors
  (`p' = p·M`) in (X, Z, Y) order with a parent-chain `M_k ← M_k · M_parent`.
  Converted to Y-up column vectors, bone k's local rotation is the transposed,
  axis-swapped `R_k[i][j] = m[s(j)][s(i)]` and `W_k = W_parent · R_k`. Using the
  matrices as absolute rows produced hunched, crouching fighters.
- **Winding.** Re-wind faces outward in the extractor before enabling back-face
  culling.
- **One mesh revision everywhere.** Fight For Life's loader returned a different
  revision of the boss meshes on its first call than on later ones, so foot heights
  were measured on an old mesh and the bosses floated 15–20 px above the floor. A
  `make modelcheck` now requires every model to load the same revision every time
  and every fighter's lowest vertex to sit on the floor.

## Root motion and changing heading

A fighter's world position is its motion's **base point plus the frame's root
offset rotated by the heading**. Re-facing the opponent *mid-motion* (when fighters
cross in a jump) rotates the whole offset around the base point — a "teleport" of
up to ~1,500 units, so jump-ins never connected. Fix it where the heading changes:
`set_heading()` adjusts the base point so the body stays put. The same file shows
the companion trick for air attacks: switch to the flying motion **keeping the
current frame**, because the original's flying motions share the jump's root path
frame for frame.

## Keep render-only animation out of the simulation

Kengeki layers presentation on top of a deterministic 60 Hz simulation without
breaking its oracle lock-step: pose mixing that accepts `t < 0` or `t > 256` for
anticipation and overshoot, additive lean/flail/spin layers, and a per-fighter
cross-fade from the last *displayed* pose — none of which ever feeds back into the
simulation. That's what keeps the host oracle and the 32X byte-identical, and KO
replays reproducible.
