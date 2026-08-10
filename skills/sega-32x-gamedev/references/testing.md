# Testing a 32X ROM

Two layers, both required:

1. **Static verification** (`verify_rom.py`) — structural checks on the built
   `.32x` and its ELF: header, checksum, entry points, section addresses, RAM
   budget, and "the game code actually linked" markers. Fast, runs in the
   build; catches most black-screen-before-you-even-boot failures.
2. **Headless emulator point-to-point tests** (`harness.c` + `run_tests.py`) —
   boot the real ROM in **PicoDrive** (libretro), drive scripted controller
   input, capture frames, and assert they are **lit, colourful, and changing**.
   This is the assertion that the ROM is not a black screen and is playable.

Both templates are in `assets/`. Copy them into the project's `tests/`.

## Build the PicoDrive libretro core (once)

PicoDrive is the reference 32X emulator with the best accuracy for this work.
Build the libretro shared object; the harness `dlopen`s it.

```sh
git clone --depth 1 --recurse-submodules https://github.com/libretro/picodrive.git
make -C picodrive -f Makefile.libretro platform=unix -j"$(nproc)"
# → picodrive/picodrive_libretro.so
mkdir -p tests/emu && cp picodrive/picodrive_libretro.so tests/emu/
```

(The `irixxxx/picodrive` fork also works and tracks upstream fixes.)

## The point-to-point harness

`harness.c` is a minimal headless libretro frontend. It:

- `dlopen`s the core, wires the required libretro callbacks, loads the `.32x`.
- Executes a **script** (a tiny text DSL, one command per line):
  - `run <n>` — run n emulated frames.
  - `press <btn> <n>` — hold a button n frames, then release.
  - `hold <btn>` / `release <btn>` — sticky press/release.
  - `port <0|1>` — choose which controller subsequent commands drive.
  - `shot <name>` — dump the current frame to `<outdir>/<name>.ppm`.
  - buttons: `up down left right a b c start`.
- Converts the core's RGB565 / 0RGB1555 / XRGB8888 output to RGB and writes
  binary **PPM** frames the Python side can read with no dependencies.

`run_tests.py` runs each script in `tests/scripts/`, then asserts on the PPMs.

This script-driven design (vs. hard-coding inputs in Python) makes each new
game state a small text file, and keeps the emulator glue in one place.

## Black-screen & playability assertions

`run_tests.py` computes per-frame stats from each PPM and enforces:

- **Not black**: count of non-black pixels ≥ a floor (a black/uninitialized
  frame has almost none). Equivalent formulation: the single dominant color
  covers < ~92% of the frame (`visible_ratio > 0.08`).
- **Colourful**: distinct color count ≥ a small threshold (≈8–12) for screens
  that show artwork. Allow a text-on-black exception for genuine plain screens
  (GAME OVER, VS) via a per-shot flag.
- **Changing / animating**: consecutive checkpoints must differ (compare CRC or
  a per-pixel delta) — proves the game is not frozen on one frame.
- **State transitions**: distinct UI checkpoints (boot, menu, gameplay) must
  have distinct signatures — proves inputs actually advanced the game.
- **Bounded input effect** (for menu regressions): pressing Down should change
  a small fraction of pixels (moved a cursor) but **not** jump to a new screen
  — the classic 6-button-pad-mirroring bug makes Down launch the level. Assert
  `0.0001 < delta < 0.08` and that Up returns to the identical prior frame.
- **Region crops**: assert a specific rectangle (HUD, ship) has enough colors —
  catches a partially-missing palette that renders one object as a black
  silhouette while the rest of the screen looks fine.
- **Audio (optional but valuable)**: accumulate the core's audio callback and
  assert non-silence (peak/energy/nonzero-ratio) at points where music or a
  sampled SFX must be playing. A known sample can be located by a sparse
  normalized cross-correlation against the source PCM, so changing FM music
  cannot be mistaken for the effect.

Tune the thresholds to the game, but keep the *shape*: lit, colourful,
changing, transitioning, with region and audio spot-checks for anything you
fixed.

## What verify_rom.py checks (static)

- Plausible cartridge size, padded to a cartridge boundary.
- `SEGA 32X` at 0x100 and the expected title string; Mars module header
  present; SH-2 entry points / vector bases match the linker's values.
- ROM-end at 0x1A4 equals the real last-byte offset.
- Stored checksum at 0x18E equals the recomputed 16-bit word sum from 0x200.
- **Linked-code markers**: assert that known strings from *different* engine
  translation units are present in the ROM. If `--gc-sections` wrongly stripped
  the game, only the header survives and these vanish — a valid-looking ROM
  that boots to black. This single check catches the nastiest silent failure.
  Pick markers the compiler cannot constant-fold away: string literals passed
  to a function (e.g. a text-draw call) always survive, but a named
  `const char[]` read only as `arr[0]` gets folded to a constant and then
  `--gc-sections` drops the array. If you need a dedicated marker object, make
  it `volatile` so the load (and thus the object) is kept.
- ELF sanity: `.text` at `0x02000000` and non-trivially large; `.data` at
  `0x06000000` (SDRAM); `.bss` end does not collide with the SH-2 stacks.
- Any embedded blob (e.g. VGM music) sits inside its expected ROM window.

## Wiring it up

`Makefile` `check:` target:

```make
check: $(ROM)
	python3 tests/verify_rom.py $(ROM) $(BUILD)/$(TARGET).elf
	python3 tests/run_tests.py            # needs tests/emu/picodrive_libretro.so
```

Run one script while iterating: `python3 tests/run_tests.py 03_match`.
Captured frames and stats land in `tests/out/<script>/` for eyeballing.

In CI: cache the toolchain, build PicoDrive, then `make check`. Fail the job on
any assertion. This is what makes "it is playable and not a black screen" a
guarantee instead of a hope.

## PicoDrive harness gotchas (learned the hard way)

- **Face-button mapping is not 1:1.** Through the libretro PicoDrive core,
  libretro "A" commonly maps to **Genesis C**, not Genesis A. If you read
  `SEGA_CTRL_A` but the harness presses "a", nothing happens. For a fire/action
  button, accept **A|B|C** — it's robust to the mapping and better UX anyway.
  Only the D-pad and Start had been exercised before this bit; verify any new
  button through the emulator, don't assume.
- **Don't press buttons on frame 0.** Pressing Start (or anything) before the
  console finishes booting doesn't register — the input looks ignored and a
  state transition silently doesn't happen. Always `run 20-30` first to let it
  boot and show the title, *then* press. (A "game over never triggers" bug
  turned out to be Start pressed too early, not a logic bug.)

## Pixel-detection pitfalls in emulator tests

Numeric pixel checks are the backbone of the black-screen guard, but they're
easy to get subtly wrong:

- **Give game objects distinct palette colors** and detect *those*. A detector
  keyed on "blue" caught the scrolling grid instead of the enemy; giving enemies
  a unique magenta made the test unambiguous (and the game clearer).
- **Objects appear where projection puts them, not where you assume.** Enemies
  at eye-level project near the horizon/lower half, not the sky band; a sky-band
  detector found nothing. Reason about the projected screen region.
- **Far/small objects may be a pixel or two.** Sample the whole region, and
  capture the frame when the object is close/large enough to detect.
- **Byte coordinates wrap.** A position stored in a `u8` wraps at 256; an object
  "moving right" can wrap off-screen and fool an absolute-position assertion.
  Keep test windows short enough to avoid the wrap, or track signed deltas.
- **Flat-shaded 3D uses few colors.** The default black-screen color threshold
  (tuned for artwork) will false-fail a clean 3D scene of ~6–9 colors. Lower
  `MIN_DISTINCT_COLORS` (~6) for 3D projects — a black screen is still 1–2.

## verify_rom markers must be live strings

A `--marker` string only guards against gc-section stripping if that exact
string is actually linked into the ROM. When you change on-screen HUD text,
update the markers to a string that still exists — otherwise verify fails not
because the game was stripped but because the marker text is stale.

## Verifying audio (the video harness can't, but PCM capture can)

The **screenshot** harness captures video, not audio, so it can't confirm sound.
But that does **not** mean audio is unverifiable — capture the emulator's **PCM
output** and compare it to a reference:

1. Host-test the mixer's sample generator directly (silence at rest, a decaying
   tone, a noisy burst, correct voice count) — pure and fast.
2. For end-to-end proof, have PicoDrive render the ROM's audio to a PCM buffer
   for a few seconds, render the *reference* (e.g. the source module via
   libopenmpt/OpenMPT) for the same span, and compare a **normalized tonal
   fingerprint** — a Goertzel bank at the expected note frequencies, or an FFT
   band comparison — with a small alignment tolerance and a relative-loudness
   check. This catches silence, weak PWM buzz, wrong sample-address decoding,
   wrong tracker timing, and dropped voices. (PWM Tracker 32X does exactly this;
   see references/audio.md.)

So: host-verify the mixer logic always; add a PCM-capture + spectral-fingerprint
test when audio correctness matters. Only claim "audio confirmed" when one of
these actually ran — a wired-but-unrun PWM path is "wired", not "confirmed".

## Hard-won harness & debugging lessons

### A fresh scaffold can produce a black/hung ROM even from correct sources

Observed (`zepton32x`): a freshly-scaffolded project tree booted to black even
from a *minimal* boot program and even from another project's known-good `main`,
despite **byte-identical** HAL sources, an identical 68000 boot blob, and a
harness that ran a known-good ROM fine. The two ROMs differed beyond the header
(into the SH-2 vector table and code), i.e. the build produced different output
from the same sources — some subtle tree/build-state corruption that resisted
diagnosis.

**Resolution that works:** stop chasing it. Rebuild the project from a **full
copy of a known-good tree** (`rm -rf newproj && cp -r goodproj newproj`), confirm
that copy still boots, and *then* swap in the new game code file-by-file. This
sidesteps whatever differed and costs minutes instead of hours. Keep one
known-good 32X tree around specifically to clone from.

### Isolate "render vs logic" with an unconditional draw

When something "isn't showing", don't assume the logic is broken. Draw it
**unconditionally** (a fixed marker in the draw function, or force one entity
alive at init). If the marker renders, the draw path and palette are fine and the
bug is in spawn/update logic; if not, it's the draw/palette. This split
immediately located a Zepton "enemies missing" bug as a *framerate* issue (below),
not a render bug.

### "run N" emulated frames ≠ N game iterations

A heavy renderer runs the game loop at a fraction of 60 fps in the emulator (see
the frame-counter measurement in `optimization.md`). Zepton ran ~8–19 game
iterations per 60 emulated frames. Consequences for scripts and interpretation:

- Spawn timers, cooldowns, and approach speeds count **game iterations**, not
  emulated frames. If enemies spawn every 40 iterations and the loop runs at
  ~12 fps, they don't appear for ~200 emulated frames — `run 60; shot` captures
  an empty field and looks like a bug. Make scripted waits generous, or lower
  timers for the test.
- A brief visual (a laser lasting 10 iterations) can fall *between* your capture
  frames. Capture several frames across the window, not one.

### Pixel-detection false-positives — use a colour no other object shares

Detecting an object by colour is only valid if that colour is unique. A bullet
yellow `#fff03c` sat within threshold of a sand terrain colour `#d2be78`, so the
detector reported "bullets present" from terrain pixels while the real bullets
were elsewhere. Give each testable object a **distinct palette entry** and match
with a **tight threshold** (and sanity-check by scanning a region you know is
empty of that object).

### Diagnose the harness→console button mapping empirically

The libretro→Genesis button map is core-dependent. For this PicoDrive core the
script names map as **`a`→Genesis C, `b`→Genesis B, `c`→Genesis A**, and
**Start on frame 0 (pre-boot) doesn't register** — `run 20–30` before pressing
Start. Held buttons **stack** (the harness ORs a bitmask), so `hold a` + `hold b`
gives both. When a combo (e.g. "both fire buttons = laser") won't trigger, don't
guess: draw one on-screen marker per `SEGA_CTRL_A/B/C` bit and read which lights
up for each script button.

## Deterministic desktop-oracle testing (record/replay)

The strongest test for a port with a shared portable core is **bit-exact
determinism between a desktop build and the ROM**. Reference: an AGPL 3D marble
port whose PicoDrive tests replay a PC-recorded winning run tick-for-tick.

Requirements and method:

- The **core is compiled verbatim** for both the desktop oracle and the 32X (no
  `#ifdef` divergence in game logic). Same input sequence ⇒ identical run.
- The oracle shell exposes `--headless --bot --record <file>` / `--replay
  <file>`: record a known-good playthrough (a bot or a human) on the PC, save the
  input sequence, and **replay the same inputs against the ROM** in the harness.
- Assert the ROM reaches the same checkpoints/outcome the oracle produced. Any
  divergence is a real portability bug (often a compiler trap or an
  endian/`long`-width issue), caught mechanically instead of by eye.

### Fixed-tick pacing makes the input grid deterministic

Determinism needs a **fixed game-tick**, decoupled from render time. If the
master's per-frame work is ~30 ms, pace the game at exactly **3 vblanks per tick
(20 ticks/s NTSC)** rather than "as fast as it renders". Then a recorded input
grid lines up tick-for-tick between oracle and ROM. Consequence for scripts: the
harness feeds recorded input **tripled** (3 emulated frames per game tick) to
match the pacing. (This is the flip side of the "run N ≠ N iterations" caveat:
here you *fix* the ratio on purpose so it's exactly known.)
