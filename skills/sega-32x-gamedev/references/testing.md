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

## The black-screen / hang debugging ladder

A 32X ROM that boots to a solid dark frame is the most common failure, and it
has many causes (hang before first flip, palette never uploaded, bad build,
render off-screen). Diagnose it as a **ladder**, cheapest first, instead of
guessing — this is the exact sequence that resolved a multi-hour Zepton black
screen:

1. **Read the actual frame, don't trust the "lit" metric.** Dump the captured
   PPM and print the top colours. One colour filling 320×224 means "nothing drew
   / hung"; several colours means it's rendering and your detector is wrong.
2. **Palette-index-0 tell.** If `GFX_Clear(C_SKY)` yields pure black `(0,0,0)`
   rather than your sky colour, the palette wasn't uploaded **or** the ROM hung
   before `Mars_SetPalette` took effect — it is *not* a geometry problem. Chase
   boot, not the renderer.
3. **Hang vs. mis-palette.** Cycle the clear colour by frame
   (`GFX_Clear(frame & 3)`) and capture several frames. If they all stay one dark
   colour, the loop is **hung**; if they change, the loop runs and the bug is in
   palette or draw coordinates.
4. **Reduce to a minimal boot.** Replace `main` with `Mars_Init;
   Mars_SetPalette; loop { GFX_Clear(bright); FillRect; Flip; }`. If the minimal
   boot is *also* black, the fault is the HAL/build, not your game code.
5. **Isolate render vs. logic** (see above): unconditional draw / force one
   entity alive.
6. **Compare the ROM bytes.** If a minimal ROM is black while a known-good ROM
   isn't, `cmp -l` the two `.32x`. Differences confined to the header/title are
   benign; differences in the **SH-2 vector table or code** from identical
   sources mean the *build* is producing bad output — a corrupt tree/build state.
6b. **Check the vectors are in the *raw ROM*, not just the ELF.** If the ELF
   links clean but boots black, verify the module-data payload actually contains
   the reset vectors (a non-loadable `.sdata` emits padding into the ROM); see
   the `@progbits` invariant in `toolchain-and-build.md`.
7. **Rebuild from a known-good tree** (the reliable fix for step 6): `cp -r` a
   booting project, confirm it still boots, then swap in your code file-by-file.

Each rung is a few minutes and eliminates a whole class of cause; don't skip to
rewriting game logic before rung 4 has cleared the HAL/build.

## Read game state from SDRAM in the harness (a liveness beacon)

Pixel assertions prove *something* is on screen; a **beacon struct** proves the
game logic is actually turning over and lets tests assert on real state. Have the
master SH-2 publish a small struct at a fixed SDRAM address every frame:

```c
typedef struct { u32 magic; u32 frame; u16 state; u16 score; s16 ball_x, ball_y; } beacon_t;
/* magic = 'ARK3' so the harness can find/validate it */
```

The libretro harness reads it out of the core's SDRAM pointer each frame. Two
gotchas:

- **PicoDrive keeps 32X SDRAM byte-swapped on a little-endian host** — read bytes
  as `p[off ^ 1]` (and compose 16/32-bit from those swapped bytes). Reading it
  straight gives garbage that looks like a dead beacon.
- Validate `magic` before trusting the rest; if it's absent the loop hasn't
  reached its publish point (still booting, or hung).

With the beacon you can assert "reached `GS_PLAY`", "frame counter advanced N
ticks", "score increased", "the ball travelled the length of the arena and came
back" — mechanical checks that a screenshot can't give you.

## Make the whole game core host-testable, and test it hard

Because the game core is HAL-free (`porting-workflow.md`), compile it for the
host and hammer it — this is what lets you tune physics/feel without flashing a
ROM. A shipped breakout's `game_host.c` ran **~22,000 assertions**: collision on
every axis, the ball never escaping the arena, tough/solid brick behaviour,
power-up effects, life loss, game-over, determinism, and a scripted **"perfect
player"** that must actually clear level 1. Patterns worth copying:

- **Scripted bot that must win.** A deterministic input sequence that clears a
  level is the strongest regression test — it exercises the whole simulation and
  fails loudly if feel/physics drift.
- **Swept-collision / anti-tunnelling assertion.** Explicitly assert a
  *full-speed* ball cannot pass **through** a brick wall in a single tick. Fast
  objects + thin walls + per-tick position updates = tunnelling; test the
  continuous check, not just the resting case.
- **Determinism check.** Same seed + same inputs ⇒ identical state, so desktop
  and ROM can be diffed (see the oracle method above).

## Geometry correctness gated by corner screenshots

When projection interacts with playfield bounds, a "looks fine" centre frame can
hide an edge bug. A breakout clamps its paddle to the arena wall; if the tunnel
mouth projects even slightly wider than the 320×224 viewport, the widest paddle
slides **off-screen in the corners** and the player aims something invisible. The
fix was a host `make shots` step that renders the widest paddle against all four
corners and **fails the build** if a projection retune breaks the match. Tie such
geometric invariants to an automated check, not to eyeballing the middle of the
screen.

## Make long content headlessly testable: an in-ROM verification accelerator

A full level that takes minutes to play is impractical to verify frame-by-frame
in the harness — at the emulator's real rate a ~8,100-position level would need
tens of thousands of captured frames. Build a **verification accelerator into the
ROM**, gated behind a normally-unused input, so an automated run can fast-forward
through the whole thing deterministically. The Tyrian ep1-l1 port does exactly
this on the B button:

- **Tick multiplier** — while held, run **N simulation ticks per rendered frame**
  (`FAST_TICKS 12`), so one emulated frame advances the game 12 ticks. A ~1,450-
  frame scripted `hold b` run then covers the entire level to its boss and ending.
- **Temporary protection** — make the verification pilot invulnerable during the
  accelerated run so the automated playthrough reaches the end **deterministically**
  instead of dying to content it isn't dodging.

This costs a few lines, keeps normal play untouched, and is what turns "we think
the level completes" into a mechanical assertion. (It also interacts with the
"run N ≠ N game iterations" caveat: with the accelerator the ratio is *known and
large*, so scripts can be short — but note that the emulator may still present
~2 video callbacks per SH-2 game-frame, so budget script `run` counts against
that, not against game ticks.)

## Telemetry via the COMM mailbox (a lighter beacon)

Instead of (or alongside) an SDRAM beacon struct, publish per-frame test state
through the **COMM registers** the 32X already exposes — simpler for the harness
to read (no SDRAM byte-swap), and free if the 68000 isn't using those slots:

```c
/* master SH-2, once per frame */
MARS_SYS_COMM0  = ty.cash;
MARS_SYS_COMM4  = (front_weapon << 8) | rear_weapon;
MARS_SYS_COMM6  = TY_SIGNATURE | (ty.state & 0xFF);   /* 'T'<<8 | state */
MARS_SYS_COMM8  = ty.level_pos;
MARS_SYS_COMM10 = ty.event_index;
```

The harness reads those words each frame and the test asserts **exact end-state**,
not just "something changed": e.g. state == `TY_STATE_COMPLETE`, `level_pos >=
8100`, and `event_index == 1009` prove the entire event stream ran to the ending;
an equipment word == front-13/rear-11 proves the loadout took; a cash/pickup/
weapon-power triple proves collectibles *mutated state*, not merely drew. Reserve
a couple of COMM slots as pad-owned (the 68000 writes input there) and use the
rest for telemetry. This is the mailbox counterpart to the SDRAM beacon above —
pick whichever your input path leaves free.

## Catalog: ROMs that verify clean but boot black (or wrong-coloured)

The black-screen ladder finds *which* class you're in; this is the catalog of
concrete 32X causes seen across shipped ports (a single racer hit nine of them).
Most compile, link, and pass static verification. Keep it as a checklist:

- **`.sdata`/startup section not loadable.** GAS defaults `.sdata` to READONLY, so
  the linker keeps LMA==VMA and `objcopy -O binary` drops it — the ROM ships with
  **no startup code / no vector tables** and the BIOS copies padding into SDRAM.
  Fix: `.section .sdata,"aw",@progbits` (see `toolchain-and-build.md`); verify the
  reset vectors are present in the **raw ROM**, not just the ELF.
- **CRAM bit 15 clear = transparent.** A perfectly rendered frame is invisible if
  palette entries don't set `0x8000`.
- **CRAM byte order.** Entries are `0x8000 | (B<<10) | (G<<5) | R`; swapping R/B
  renders a blue sky as dark red — "wrong colours", not black, but the same class
  of "looks broken, verifies fine."
- **VDP register at the wrong address** (e.g. `FBCTL`/`DISPMODE` off by a couple
  of bytes) — the page flip or mode set silently never happens.
- **VDP left in direct-colour instead of 256-colour packed-pixel** → each row
  eats 640 bytes not 320, so you get **two half-width copies** with indices
  reinterpreted as RGB555. Spell out every DISPMODE field and **re-assert the mode
  on each flip**.
- **Uninitialised frame-buffer line table** (on *either* buffer) — the classic
  blank screen; both buffers need a valid line table at init.
- **FM granted after the SH-2 handshake.** `hw_init()` waits for FM before
  touching the VDP, but if the 68000 sets FM only *after* waiting for the SH-2's
  `M_OK`, both sides wait forever. **Grant FM before the handshake.**
- **Security-block checksum handshake unmet.** The Sega boot block spins until the
  SH-2 publishes the ROM checksum in **COMM8**; a minimal 68000 side must satisfy
  that or all three CPUs deadlock.
- **VBlank enabled with no handler**, plus an **unbounded flip-wait spin** — hangs
  before first present.
- **Stock MD header ROM-end word** (`0x1A4`) hardcodes 4 MiB and fails static
  verification / confuses loaders; `romfix.py` must rewrite it to the real size.

## COMM-register allocation is a real hazard

The COMM mailbox is shared by the boot handshake, the 68000 pad path, telemetry,
*and* any slave-job protocol — and a **collision produces bizarre, non-obvious
bugs**. One racer drove the slave-job command through **COMM4**, the same word
the slave's `S_OK` boot handshake uses, so a job value was read as a handshake
and the slave ran before FM was granted and scribbled the VDP registers (the
doubled-image bug above). Assign COMM slots deliberately, document the map, and
guard it: a **static `check_comm.py`** that greps for overlapping slot use, and a
**left/right-symmetry assertion** in the emulator suite that detects the doubled
image directly.

## Oracle against the original's *own* code

Stronger than a hand-written expected-value test: run the **original game's
actual code** as the oracle and diff. A racer's `make oracle` runs the shipped
JavaScript physics (`stepPlayer`/`stepAI`, copied verbatim) and the C core from
the same grid slot with the same inputs, then diffs trajectories — max `dpos`
0.004u over 220 frames on a 187u circuit. Note the honest caveat: **chaotic
stretches** (guardrail contact, where a sub-LSB difference amplifies
exponentially) are compared on **aggregate behaviour** (distance/speed within a
few %), not frame-exact position. Use frame-exact where the system is stable,
aggregate where it's chaotic.

## A test that re-derives the maths cannot catch the maths being wrong

Two handedness bugs (a car body rotating backwards; reversed steering) survived
because the test recomputed the same rotation the renderer used. **Put shared
math in one function both the code and its test call** (`r_model_to_world()`), so
the test pins the *actual* behaviour, not a copy of the suspect formula. And size
your LUT precision to the *consumer*: whole-brad `atan2` (1.4°) is fine for
gameplay headings but **one brad of camera pitch moves the horizon 4–5
scanlines**, so the camera needs an interpolated 16.16 `atan2` (257-entry) —
worst-case error 0.99 → 0.0002 brads, zero horizon jumps over 3000 frames.

## Distinguish a crashed CPU from a logic deadlock (heartbeat + state overlay)

When a game *hangs* (as opposed to boots black), you need to know **which kind**
of hang it is. Build a debug overlay (behind a `-DRT_DEBUG` flag) with two parts:

- A **per-frame heartbeat square** that toggles/advances every rendered frame.
  If it **freezes**, the SH-2 itself crashed (exception, bad pointer, tight infinite
  loop with interrupts off). If it **keeps animating** while the game is stuck, the
  CPU is fine and you have a **logic deadlock** (a VM waiting on a flag that never
  clears, a message box awaiting input that isn't wired). One glance splits the
  search space in half.
- An **interpreter-state readout** — active event, current command opcode, pending
  move route, message/movement flags. For a bytecode-VM game this turns "it froze
  in the opening cutscene" into "it's parked on opcode 0x6C waiting on switch 12."
  A shipped RPG tracked down an opening-cutscene freeze exactly this way.

This is the hang counterpart to the black-screen ladder above: the ladder is for
*nothing drew*; the heartbeat/overlay is for *it drew once and then stopped*.

## Build debug warps into deep content

Long games hide the interesting states behind hours of play the harness can't
sit through. Add **debug warps** — title-screen button combos that jump straight
into a test map, a specific scene, or a battle (a shipped RPG uses hold-UP/DOWN
at START for two map zones and B for a test battle). Harmless in normal play,
they let a PicoDrive script reach a battle or a late map in a handful of frames.
Pair them with the verification accelerator (fast-forward long waits) so any
scripted scenario is reachable and fast.

## A third telemetry channel: MD work RAM

Besides an SDRAM beacon and the COMM mailbox, the **68000 can mirror a telemetry
word into MD work RAM** that the harness reads out of the Genesis-side address
space. A shipped RPG uses this so a test can assert the game reached the **right
map** by map id. Useful when the SH-2 side's COMM slots are all spoken for and
the 68000 already owns a value you want to observe; pick whichever of the three
channels (SDRAM beacon / COMM / MD work RAM) your bus ownership leaves cleanest.

## Distinguish a crashed CPU from a logic deadlock (heartbeat + state overlay)

When the game "freezes", you need to know *which* freeze it is: the SH-2 crashed
(bad opcode, wild jump) or it's alive but the game logic deadlocked (a script
waiting on a flag that never sets). Build a debug mode (`-DRT_DEBUG`) that draws:

- A **per-frame heartbeat square** that changes every frame from the top of the
  main loop. If it **stops**, the CPU/main loop is dead (a crash); if it **keeps
  animating while the game is stuck**, the loop is fine and the bug is in game
  logic — look at state, not the CPU.
- An **interpreter-state overlay**: the active event, current command opcode,
  pending move route, and the message/movement flags. A shipped RPG tracked down
  an opening-cutscene freeze this way — the heartbeat proved the SH-2 was alive,
  and the overlay showed the interpreter parked on a command waiting for a flag.

This two-part tell (heartbeat = liveness, overlay = why) turns "it hangs" into a
located bug in minutes, and complements the black-screen ladder (which is for
*boot* failures; this is for *mid-run* freezes).

## Debug warps into deep content

Long games hide bugs deep in the content. Build **debug warps** behind harmless
input combos so tests (and you) can jump straight to the interesting scene: a
shipped RPG uses title-screen **hold-UP+START** to enter a late map, **hold-DOWN**
for a street zone, and **B** to drop into a test battle. Like the verification
accelerator (above), it reuses the real code paths, costs nothing in normal play,
and lets a PicoDrive script reach a battle/cutscene without playing through hours
of story to get there.

## A third telemetry channel: MD work RAM mirrored by the 68000

Besides the COMM mailbox and an SDRAM beacon, the **68000 can mirror a telemetry
word into Genesis-side work RAM** that the harness reads out — a shipped RPG uses
this to assert the game reached the **right map**. Handy when the SH-2's COMM
slots are all spoken for or you want the value on the 68000 side anyway; pick
whichever of the three channels (COMM regs / SDRAM beacon / MD work RAM) your
CPU-role split leaves free.
