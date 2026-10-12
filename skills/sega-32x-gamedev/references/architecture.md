# 32X hardware architecture (for programmers)

The 32X ("Mars") sits on top of a Genesis/Mega Drive. Three CPUs cooperate.
Design your game around who does what, or you will fight the hardware.

## The three CPUs

| CPU | Clock | Job in a typical game |
| --- | --- | --- |
| **Master SH-2** | ~23 MHz | Game logic, world/physics, driving the frame. The "main" CPU. |
| **Slave SH-2** | ~23 MHz | One heavy parallel job: PWM audio mixing, or a rendering phase (wall prep, plane fill, sprite draw). Idle-waits on a command from the master. |
| **68000** | ~7.6 MHz | Controller polling, VBlank/timing service, native YM2612 FM + PSG music, SRAM access, some VDP/CD chores. Stays resident from boot. |

The two SH-2s share the same code in ROM and the same 256 KiB SDRAM. They
coordinate through the **COMM registers** (see below). Naive ports use only the
master SH-2; that works but leaves half the compute on the floor — moving audio
or a render phase to the slave is the single biggest 32X speedup.

## Memory

- **SDRAM: 256 KiB total** at `0x06000000`, shared by both SH-2s. This holds
  `.data`, `.bss`, the heap, and both stacks. It is the scarce resource. Track
  `__bss_end` every build.
- **Cartridge ROM** at `0x02000000` (a ~4 MiB window; banking extends beyond).
  Immutable data — decoded graphics, palettes, converted music, sound samples,
  level data — belongs here, read directly, not copied into RAM.
- **Cache**: each SH-2 has a small cache. When a buffer is touched by DMA or by
  the *other* CPU, the reading CPU may see stale cache. Clear the relevant
  cache line(s) after such writes (`Mars_ClearCacheLine`, `Mars_ClearCache`).
  Align DMA/shared buffers to 16 bytes and mark hot shared routines with a
  cache-aligned section attribute (d32xr's `ATTR_DATA_CACHE_ALIGN` →
  `__attribute__((section(".sdata"), aligned(16), optimize("O1")))`).
- **ROM banking (SSF/SEGA mapper)**: for ROMs past the direct window, page ROM
  banks in with `Mars_SetBankPage`. Choose the mapper in the ROM header
  (`SEGA 32X` vs `SEGA SSF`). The switch is a **cross-CPU protocol, not a
  register poke** — see "SSF bank switching" below.

## Video: the 32X VDP framebuffer

The 32X has its own VDP with a **packed-pixel framebuffer** that overlays the
Genesis VDP output. Two common modes:

- **8-bit / 256-color** ("packed pixel"): each byte is a palette index into a
  256-entry CRAM palette. Easiest for ports; one byte per pixel.
- **direct-color 15-bit RGB**: each pixel is an RGB555 word. No palette, but
  double the bytes and no cheap palette tricks.

There are **two framebuffers**; you draw to the back buffer and flip. Typical
32X game resolutions are narrow — 320×224 is the full frame, but many engines
render a smaller internal buffer (e.g. 128–252 px wide) and let the hardware
line-double for speed.

Reference hardware calls (from d32xr's `marshw.h`):

- `Mars_Init()`, `Mars_InitVideo(lines)`, `Mars_InitLineTable()`
- `Mars_BackBuffer()`, `Mars_FrameBufferLines()`, `Mars_FlipFrameBuffers(wait)`,
  `Mars_WaitFrameBuffersFlip()`
- `Mars_SetPalette(const uint8_t*)`, `Mars_SetBrightness()`
- `mars_vblank_count` (frame tick), `Mars_IsPAL()`, `Mars_RefreshHZ()`

**Palette is the #1 silent black-screen cause.** In 8-bit mode every nonzero
pixel index that has not been given a color shows as entry 0 (usually black).
Load/seed the palette *before* the first flip, and reseed it whenever a new
scene's palette should apply. A frame can be "drawn" and still look black
purely because the palette is zeroed.

## Audio

Two independent paths, often used together:

- **32X PWM** (stereo, ~22 kHz): the 32X's own DAC. Dedicate the **slave SH-2**
  to resampling/mixing PCM samples into the PWM ring at a fixed rate. Values
  must stay inside the safe hardware range (roughly 2..1032). This is how you
  get sampled SFX and streamed audio. See d32xr's `sh2_mixer.s`,
  `marssound.c`, and `Mars_InitPWM`.
- **Native Genesis FM/PSG** via the **68000**: the YM2612 (6 FM channels) and
  SN76489 PSG. Best for music. A common, high-fidelity approach is to convert
  the original score to **VGM 1.50** at build time and run a small Work-RAM
  68000 VGM player clocked by YM2612 Timer A (matching the source's tick rate).
  Keep YM busy-waits and per-tick command batches **bounded** so a bad music
  stream can never stall controller/VBlank service (another black-screen trap).

Mixing note: reserve a few dB of headroom on FM carriers so music does not mask
PWM effects; normalize wildly different sample levels at build time.

## Controllers

- `Mars_ReadController(port)` returns the pad state. The 32X can also read the
  **Sega Mouse** (`Mars_PollMouse`, `Mars_ParseMousePacket`).
- **Mask to the 3-button subset** (U/D/L/R, A/B/C, Start) unless you truly
  support 6-button pads. Several emulators mirror the d-pad into the 6-button
  extended nibble during the handshake, so reading X/Y/Z/Mode makes every
  direction also read as Jump/Back. This is a real, shipped bug in ports that
  forgot to mask — see the reference ports' `read_pad` masking.

## Inter-CPU communication (COMM registers)

The master and slave SH-2 (and the 68000) talk through a bank of shared
**COMM** registers (`MARS_SYS_COMM0..7`). The idiom used throughout d32xr:

- The slave runs a dispatch loop (`Mars_Secondary`) that spins until the master
  posts a command id into `COMM4`, runs the corresponding routine, then clears
  `COMM4` back to `MARS_SECCMD_NONE`.
- The master posts work with a small "begin/end" wrapper that waits for the
  slave to be idle (`Mars_R_SecWait()` = `while (COMM4 != NONE);`), writes
  parameters into another COMM register, then writes the command id.
- Commands seen in d32xr: clear cache, wall-prep, draw planes, draw sprites,
  animate fire, init sound DMA, sight checks, screen-melt wipe, RoQ stream.

When you offload a job to the slave, model it the same way: a command id, a
parameter word, a wait-for-idle on both sides, and a deliberate cache clear if
the slave wrote a buffer the master will read.

## Boot sequence (why order matters)

1. Genesis powers up running the 68000 from cartridge; the Mars header points
   it at the 32X handoff.
2. The SH-2s start from their Mars header entry points; startup copies `.data`
   from ROM to SDRAM, zeroes `.bss`, runs C constructors.
3. The master releases the slave and the 68000 to their resident loops via COMM
   handshakes. **Reload the handshake register from a fresh read before
   clearing the release flag** — releasing through a stale register value is a
   known startup stall that boots to black on some hardware/emulators.
4. Master initializes video/palette/audio, then enters the game loop.

The 68000's initialized Work-RAM image (and anything it copies at boot) should
sit in the **fixed low ROM window**, *before* any large data blob (e.g. music).
If the 68000 has to reach past a big blob to fetch its boot image, some
emulators black-screen.

## Dual-core: the slave SH-2

The master and slave SH-2 boot from the same `crt0.s`. The master runs `main`;
the **slave is released and then sits in a spin loop**. To use it, replace that
loop with a **COMM-based job dispatcher**:

- Pick a free COMM register as the *command* mailbox. Watch what's already used:
  the release handshake and telemetry may occupy some, the 68000 puts pad state
  in COMM12/COMM14, and COMM8 tends to be reserved by the boot blob. COMM2 is a
  good free command slot in practice. Don't reuse a register two subsystems
  write.
- Protocol: master writes a non-zero command (e.g. `0x8000 | color` for a clear)
  into the mailbox; the slave sees it, does the work, then writes 0 back (ack).
  The master waits for the ack — **bounded** (e.g. ≤200k spins) so a
  non-responding slave can never hang the frame.
- COMM registers live in **uncached** peripheral space, so both cores see
  writes immediately — no cache sync for the mailbox itself.

The slave dispatcher is small assembly appended in `crt0.s` where the idle loop
used to be (read mailbox → if non-zero, do job, ack → loop). Keep interrupts
masked and use only registers.

## Memory coherency gotchas

- **Frame buffer (`0x24000000`) is uncached I/O.** Writes are visible to the
  VDP and the other SH-2 immediately. Two cores writing **disjoint** pixel
  ranges is safe with no cache work. This is what makes slave-side clearing /
  half-screen fills easy.
- **SDRAM (`0x06000000`) is cached** on each core independently. Anything one
  core writes there and the other reads needs explicit cache handling — this is
  what makes slave *rasterization* (reading shared geometry) the hard step.

## SH-2 integer sizes (a real bug source)

On the SH-2, **`int` and `long` are both 32-bit** (only `long long` is 64-bit).
Distance-squared, area, and fixed-point accumulators overflow 32 bits fast:
a `dx*dx + dz*dz` with world coords in 16.16 will silently wrap if typed
`long`. Use `long long` for such intermediates. Host x86 has 64-bit `long`, so
this class of bug **passes on the host and only shows on hardware** — grep for
`long ` in any distance/nearest/area code when porting host-tested logic to the
SH-2.

## Battery-backed cartridge SRAM saves

Persisting progress (an RTS campaign, track records, custom levels) uses the
cartridge's **battery-backed SRAM**, which the 68000 sees in its ROM/save window;
the SH-2 reaches it through the same 68000-side access path. Shipped examples: an
RTS with a full save/continue flow (`warcraft-32x`) and racers saving records +
custom tracks (`dmar-daytripper-conversions`).

A robust format is small and defensive:

- **Bounded, fixed layout** (e.g. ≤2 KiB): a magic/version header, a compact
  encoding of the mutable state (entities, resources, construction/training
  queues, campaign position), and a **checksum** over the payload.
- **Encode/decode explicitly** — don't `memcpy` live structs; serialize only the
  mutable fields so the format survives code changes, and keep it endian-defined.
- **Sparse storage for big-but-mostly-default state** (e.g. only the forest cells
  that were depleted), so a 64×64 world's diffs fit in a couple of KiB.
- **Validate on boot**: check magic/version/checksum, detect corruption, and only
  then light up a **Continue** menu item. A bad save must fail safe to "no save",
  never crash.
- **Save/Load UI** on the pause menu; write on request, not every frame (SRAM
  writes are slow and finite).

**Test saves through the real SRAM path, not a stub.** A strong point-to-point
test writes live game state through the actual 68000 SRAM protocol, quits to the
freshly-enabled **Continue**, reloads, and compares the restored scene
**pixel-for-pixel** against the pre-save frame. That proves the encoder, the
decoder, and the hardware access path all agree.

## Dual-SH-2 role splits and cross-core data

Beyond "slave clears the framebuffer", shipped games assign the second SH-2 a
standing job:

- **Slave = uninterrupted PWM audio.** Dedicate the slave to feeding the PWM
  FIFOs so heavy master-side rendering can never starve the mixer into buzzing
  (`wave-rider-gp`, the tracker player). Keep unsynchronised render work *off* it.
- **Slave = command/cache service.** An RTS runs the master for gameplay+render
  and uses the slave for a command/cache service (`warcraft-32x`).
- **Cross-core cache coherency is on you.** SDRAM is cached per-SH-2; data one
  core writes for the other must be flushed/read **cache-through** or the reader
  sees stale bytes — a bug that only bites on *hardware*, not in some emulators.
  Route cross-core buffers (audio command blocks, job descriptors) through
  uncached access or explicit cache handling, and test on hardware where possible.

## Assign COMM slots deliberately (collisions are vicious)

The COMM mailbox is shared by the boot handshake (`M_OK`/`S_OK`), the security
checksum (COMM8), the 68000 pad path, test telemetry, and any slave-job protocol.
**Two subsystems on the same word cause non-obvious corruption** — a slave-job
command sharing COMM4 with the `S_OK` handshake let the slave run before FM was
granted and scribble the VDP registers (a doubled-image black-screen). Keep a
documented slot map (e.g. COMM0/2 = pad, COMM4 = boot handshake, COMM6+ =
telemetry, a *different* slot for job commands), and guard it with a static
grep-check plus a symptom assertion in the emulator suite (see `testing.md`).
When you dedicate the slave to a job over the framebuffer, having the two cores
touch **disjoint rows** of the uncached framebuffer avoids cache coherency work
entirely — and keep the master's wait on the slave **bounded**, so a silent slave
degrades to a slower frame, never a hang.

## ROM size, banking, and the 32X-CD decision for large games

The cartridge address window is finite — a stock low-ROM window is roughly
**4 MiB**. Small ports fit easily (a 512 KiB–2 MiB ROM: engine + a ~1.2 MB asset
bank). Large content does not: one RPG's **source soundtrack alone was ~170 MiB**.
Consequences to settle **before you freeze the asset compiler's address format**:

- **Pick the platform/layout up front.** Either a **cartridge banking scheme**
  (a banked, lazily-activated map/asset directory rather than one flat blob) or a
  **32X-CD build** (streaming from CD). Retrofitting banking after the converter
  emits absolute offsets means rewriting the converter — decide first.
- **Transform, don't embed, oversized audio.** Replace MP3/WAV/MIDI streams with
  legally-redistributable converted audio: short **IMA-ADPCM** cues for SFX, and
  for music either **MIDI→VGM** (YM2612/PSG, played Genesis-side — the
  MIDI-to-VGM pipeline in `audio.md` makes a game's MIDI score tractable rather
  than "a project of its own") or aggressively reduced/banked ADPCM.
- **Grow the ROM to a valid power-of-two** as content lands (one port auto-expanded
  512 KiB→1 MiB) while keeping the header, checksum, and SDRAM budget valid — and
  keep `romfix.py` rewriting the ROM-end header word so static verification passes.

Rule of thumb: budget the asset bank against ~4 MiB early; if the honest total
blows past it, the answer is banking or 32X-CD, not "compress harder later."

## Robust SRAM saves: versioned checksums + two-phase commit

Extending the SRAM-save format above for a real game: tag the payload with a
**format version** (so old saves are detected and rejected/migrated, not
misread), checksum it, and write with a **two-phase commit** — write the new save
to a spare slot, verify it, then flip an active-slot marker — so a power-loss
mid-write can never corrupt the only good save. Validate on boot before offering
Continue; a bad save fails safe to "no save."

## SSF bank switching: park the SH-2s, then switch

Past the ~4 MiB window, an **SSF mapper** ROM (`SEGA SSF` at header `0x100`)
splits the cartridge into **512 KiB pages**. Page 0 is the fixed window (header,
68000 program at `0x800`, SH-2 code + rodata, the `.data` image); pages 1…N are
the banked asset area. A shipped Tempest 2000 port (4 MiB and 8 MiB builds) uses
the same protocol as d32xr/32XDK — the one **real carts need**, because **the
SH-2s must be off the cartridge bus while the mapper latch changes**:

1. The SH-2 calls `Hw_SetBankPage(bank, page)`, which posts a command word
   (`0x16xx`) in COMM0.
2. The **68000 raises the CMD interrupt on both SH-2s**; each parks in a spin loop
   **running from SDRAM** and acknowledges (`0xA55A` on COMM0/COMM4).
3. The 68000 sets **RV**, writes the page to the mapper register
   **`0xA130F1 + bank*2`**, clears RV, and writes `0xFFFE` to release.
4. The SH-2s resume. Battery-SRAM reads/writes use the **identical park/RV
   sequence** (different command words).

Make it observable: **self-test every page at boot.** The ROM builder plants a
tag at the end of each page; at boot the game switches every page into a bank,
reads the tag back, and prints the result on the title (`SSF BANKS 07/07` /
`15/15` in green). A wrong mapper header, a bad page order in the image, or a
broken park protocol then shows up on the first screen instead of as corrupted
music twenty minutes in.

**Design for both sizes.** Tempest ships a 4 MiB build (only the title tune fits;
synth SFX fallback) and an 8 MiB build (full soundtrack + sample bank). Have the
asset loader degrade gracefully when an asset isn't present rather than assuming
the big build — then the ROM-size decision (above) is a build flag, not a fork.

## 68000-side gotchas that hide as "the pad doesn't work" or "nothing draws"

- **Set port A's TH bit (`0xA10009`)** in the 68000 init. Without it the pad
  never answers the TH-toggling (6-button) read protocol in PicoDrive, and input
  looks dead (Alien Assault II port).
- **The adapter FM bit (`0xA15100` bit 15) must be set before the SH-2 touches
  the VDP or CRAM** — PicoDrive **silently drops** VDP/palette writes from the
  SH-2 until it is. Pairs with the "grant FM before the handshake" deadlock in
  the black-screen catalog (`testing.md`).
- **Run the 68000 service loop from work RAM** and hold the **Z80 in reset** if
  you don't use YM2612/PSG — fewer agents on the cart bus (see `audio.md`).

## Logic rate vs render rate: decide it, don't inherit it

Shipped ports split the two deliberately rather than letting the renderer set
the game speed:

- **Fixed logic tick with catch-up** — Tempest keeps logic at **60 Hz** while
  rendering at 30 fps, running extra ticks when a frame is late.
- **Original tick model, vblank-normalised** — Pacamaze keeps the DOS **70 Hz**
  tick model and advances it by elapsed vblanks, so game speed is correct at any
  render rate (~12–16 fps).
- **Fewer, heavier ticks** — Alien Assault II runs combat at a steady **3 frames
  per tick (~20 Hz)**, deterministic across runs, after moving the framebuffer
  flip inside vblank so menus hold one tick per frame.

All three keep the simulation deterministic per tick; pick by how sensitive the
original's feel is to tick rate, and **measure** the frames-per-tick (as
`optimization.md` describes) rather than assuming it.

## Compositing the 32X layer over the Mega Drive

The 32X image is laid over the Mega Drive's, pixel by pixel. Three shipped ports
made three different-sounding claims about how, and PicoDrive's compositor
(`pico/32x/draw.c`, packed-pixel path) reconciles them:

1. **Wherever the Mega Drive pixel is the backdrop colour, the 32X pixel is drawn
   — whatever its CRAM entry says.** The Genesis *backdrop* never shows through
   palette index 0.
2. **Wherever the Mega Drive draws a real plane or sprite pixel**, the 32X pixel
   wins only if **`(CRAM bit 15) XOR (DISPMODE PRI bit, 0x0080)`** is set;
   otherwise the Mega Drive pixel shows.

What the three ports saw, read through that rule:

- **City Carnage** packs its palette with bit 15 clear on every entry and sets
  `MARS_VDP_PRIO_32X`; over a blank Mega Drive plane rule 1 applies and the game
  displays normally. Its first hardware probe (HUD strip = index 0 over a **green
  backdrop**) came back black on hardware *and* in PicoDrive — exactly what rule 1
  predicts, so it showed the backdrop can't be used, not that compositing is
  impossible. Its second probe (magenta tiles on plane A, PRI toggled every 2 s)
  reportedly stayed black in both modes; that contradicts rule 2 if plane A was
  really being displayed, and it appears to have been judged in PicoDrive only —
  suspect the Mega Drive plane setup before the hardware.
- **WipEout** sets bit 15 on every entry *and* PRI; over a blank plane rule 1 again
  makes it visible. Its README credits bit 15 for that, which only matters if the
  Mega Drive is drawing non-backdrop junk (e.g. uninitialised VRAM).
- **Mr. Boom** ships index 0 with bit 15 **clear** and the rest set, so index 0
  becomes the window through which its Mega Drive tilemaps and sprites show (rule
  2). That is verified in PicoDrive; it has not been reported on hardware.

**Practical rule:** decide per entry. For a 32X-only game, initialise the Mega
Drive planes to blank and any consistent bit-15/PRI choice works. To show Mega
Drive graphics *under* the 32X, they must be real plane/sprite pixels (not the
backdrop) and the transparent entries need `bit15 XOR PRI = 0`. Prove it on
hardware with a colour-coded probe ROM that varies **both** bit 15 on index 0 and
PRI (see `testing.md`) before building on it. A hard alternative that needs no
compositing: switch the 32X display **off for some scanlines** from an HBlank
interrupt (City Carnage's proposed HUD split; not built).

## A hybrid renderer: Mega Drive VDP for the world, 32X for the overlay

The 32X sits *on top of* a working Genesis, and the Genesis VDP is a good 2D chip.
A shipped Mr. Boom port draws its arena with **Mega Drive tilemaps and hardware
sprites** and uses the 32X framebuffer only for the HUD, banners and menu panel —
cutting SH-2 pixel writes from ~16 KiB + changed cells to ~4 KiB per frame at
60 fps. Both renderers (`RENDERER=md` and `=fb`) run one hardware-free core and are
tested to agree cell for cell. How it is wired:

- **Only the 68000 can reach the MD VDP; only the SH-2s can reach SDRAM.** The one
  memory both see is the 32X framebuffer, so each frame the master SH-2 writes a
  **delta display list** into the framebuffer's **off-screen tail (`+0x18000`)**,
  hands the framebuffer to the 68000 by **clearing the adapter's FM bit**, and
  waits for an ack. A steady-state packet is a few dozen cells.
- **Fetch promptly, apply at VBlank, and never drop a packet.** Applying sprite
  attribute table writes while the VDP scans out makes Mega Drive sprites
  **tear**; deferring the apply naïvely lets the next packet overwrite a pending
  one, losing deltas the SH-2 has already folded into its shadow (the board drifts
  out of sync). The two-stage handshake that fixes both: `COMM2` SH-2→68000 "packet
  waiting"; `COMM12` 68000→SH-2 "I still hold an unapplied one — don't build the
  next". Skipping a build just leaves the MD layer a frame stale.
- **Mega Drive sprite tiles must be contiguous.** A sprite names one tile and the
  VDP reads the rest sequentially (column-major). Feeding sprite tiles through the
  same **dedup pool** as name-table tiles breaks those runs and the VDP draws
  whatever follows — heads detached from bodies, fragments of neighbours. Dedup
  whole sprites, never tiles inside one.
- **Apply a screen offset once.** A vertical offset supplied by plane scroll *and*
  added to the name-table row shifted the arena by a cell — it looked like
  different level generation until the boards were diffed cell by cell. Keep
  offsets a multiple of the 8-px cell.
- **Still prove it's a 32X cartridge.** A `check_32x.py` asserts distinct master
  and slave entry points in SDRAM and both SH-2 reset vectors present in the raw
  ROM, and that the SH-2 side carries the game (the 68000 resident here is ~2 KiB
  of VDP driver and pad poller).

## Framebuffer facts that cost real debugging time

- **Zero bytes can be silently dropped.** The framebuffer's *overwrite image*
  (`+0x20000`) ignores writes of byte value 0 by design — that's what makes it
  useful for transparent sprite blits for free. A Mr. Boom port also found
  byte-wise clears to index 0 having no effect where it expected them to, leaving a
  menu panel burned in. **Clear with aligned 32-bit stores through the normal
  image**, never byte loops.
- **Pace on the 32X VDP's own VBLK bit.** The Mega Drive VDP status flag is a
  *level*, not an edge — a loop that polls it ticked ~200× per frame and left a
  game completely unpaced (City Carnage). Ask the 68000 for exactly one pad sample
  per frame.
- **Latch pads into a ring buffer in the VBlank IRQ** (Kengeki: 16 entries) so
  presses aren't lost when the game renders below 60 fps.
- **32X VDP auto-fill** draws long single-colour spans in hardware (Fight For Life
  uses it for floor runs). Its registers are shared: **never let both SH-2s use
  fill at once, and don't write the framebuffer while a fill runs** — during
  two-CPU phases, fall back to CPU span loops.

## The boot BIOS copies `.data` — and underflows if there is none

The master SH-2's boot BIOS (offset `0x28C`–`0x2A2`) reads `__data_size` from the
Mars module header and copies initialised data from ROM to SDRAM with a
decrement-by-4, compare-with-zero loop. **If `__data_size` is 0** — a program with
no initialised globals — the first `sub` underflows to `0xFFFFFFFC` and the loop
runs ~2³⁰ times, overwriting all of SDRAM and the bus registers before your code
starts. A WipEout port guarantees a non-empty `.data` with a small initialised
anchor (`g_mars_boot_anchor[4]` = 16 bytes). Cheap insurance for any project;
`verify_rom.py` can assert `__data_size >= 4`.

## Bank switching must never pull ROM out from under running code

The SSF protocol above parks both SH-2s in SDRAM — but code that is *already
executing from ROM* elsewhere can still be cut off. City Carnage booted silent at
random: the slave's ~187 ms PWM start-up ramp ran from ROM and never parked, while
the master's first `Bank_Set()` waited only ~87 ms before switching, so the slave
fetched garbage instructions and the mixer never reached its loop. Intermittent
(depends on where the cores line up at power-on) and **invisible in PicoDrive,
which does not model RV bus contention.** The fix pattern:

- anything that may run during a switch lives in **SDRAM** (or is parked first);
- the slave publishes **`slave_alive` before** its ramp, so the master can tell
  "not booted" from "running, must park";
- `Bank_Set()` **refuses** to switch out from under a live, unparked core and
  returns failure instead;
- **every wait is bounded**, and a **music watchdog** restarts a track that should
  be playing but isn't, so a transient becomes a hiccup rather than a silent run.

Keep everything a frame needs (sprites, palettes, fonts) in the **fixed page**, so
no gameplay draw depends on which bank is mapped.

## Battery saves on an SSF cartridge (and on a Mega EverDrive)

City Carnage's saves worked in every emulator and never on hardware, for reasons
worth knowing before you ship:

- **Under the SSF mapper every bank window is a paged ROM window.** The
  conventional save address `$200001` is bank 4, so on a real mapper (the
  EverDrive implements it faithfully) writes there land on ROM and are discarded.
  Emulators hide this by keeping a separate SRAM buffer keyed off the header. The
  port moved saves to **`$300001` (bank 6), odd bytes**, left bank 6 deliberately
  unmapped, and declared `$00300001-$00303FFF` in the header (`$1B4`/`$1B8`), which
  is what emulators and the flash cart size their save buffer from. On the
  EverDrive the matching per-ROM setting is **Save Type `SRM3M`**.
- **On an SSF cart `$A130F1` is the bank-0 page register**, not just an "SRAM
  enable" latch — careless writes there can remap the page holding the 68000
  vectors and the SH-2 image (the port's own changelog disagrees with itself about
  exactly what `$01` does, so keep bank 0's page explicit around it). Yet removing
  the write broke saving in PicoDrive, which watches it to allocate a save buffer.
- **EverDrive Pro/X: battery RAM is SSF page 31** — word-wide, 512 KiB on the Pro,
  **only the upper 256 KiB on the X3/X5/X7** (the lower half is PSRAM and dies with
  the power). Saves written below `$340001` silently vanished on X-series carts.
  Register order to map it: write `$A130F0` (CTRL0) with write-protect on, then
  `$A130FC` (CTRL6) to select page 31, then CTRL0 again with **W=1** (W gates BRAM
  writes too); keep CTRL0's bank-0 page field at 0.
- The Pro has **no battery**: BRAM reaches the SD card only when the cart's OS gets
  control back, so power-cycle into the menu before pulling the card. Its save file
  is 16-bit wide (data on odd offsets); Krikzz's `16to8`/`8to16` convert to and
  from emulator layout. **In-game menu and save states don't work with a 32X
  attached.**

## Cache coherency in two-CPU rendering

The master's SDRAM cache is write-through, so what it writes is in SDRAM — but the
**slave's cache may still hold stale lines** for those addresses. Before reading a
job the master posted, the slave purges its cache (Fight For Life: `CCR = 0x11`;
Kengeki: `CCR = 0`, then `CCR = old | CP`). Both CPUs write the framebuffer only
through the **cache-through mirror** and only on **disjoint rows**, and the master
waits for the slave's done flag before drawing the HUD and flipping.

A related trap: when the 68000 writes a 32-bit counter across a COMM pair, the
lower-numbered register holds the **high word** (big-endian) — Fight For Life read
`COMM12` expecting a frame counter and got its high half; read `COMM14`.
