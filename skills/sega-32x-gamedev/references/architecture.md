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
  (`SEGA 32X` vs `SEGA SSF`).

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
