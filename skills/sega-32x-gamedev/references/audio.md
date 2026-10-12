# Audio on the 32X (PWM)

The 32X produces sound through a **stereo PWM** unit — you feed it amplitude
samples and it reconstructs the waveform. There's no wavetable/FM on the 32X
side; anything richer than a beep is a **software mixer** you write. A complete,
working reference is haroldo-ok's **PWM Tracker 32X**
(https://github.com/haroldo-ok/tracker-player-32x), an eight-voice S3M-style
player. Study it for technique; don't redistribute its bundled music (those
tracks are third-party copyrighted).

## The PWM registers

```
0x20004030  PWM control
0x20004032  PWM cycle        (sets the sample rate: cycle = PWM clock / rate)
0x20004034  PWM left FIFO
0x20004036  PWM right FIFO
0x20004038  PWM mono FIFO    (writes both L+R)
```
Pick a rate you can sustain (11,025 Hz is a good, robust choice) and write
one L and one R amplitude per sample. The amplitude range is **1..CYCLE, centred
at ~CYCLE/2** — e.g. with `CYCLE = 0x415` (≈22 kHz NTSC) silence is ~517 — so
size mixer gains to the cycle you chose, not to a fixed bit depth.

## Software mixer

Model N **virtual voices**, each with: a pointer/position into a sample, loop
start/end, a pitch (position increment, usually fixed-point), a volume, and a
stereo pan. Each output sample = sum over voices of `sample[pos]*volume`, split
L/R by pan, clamped. Advance each voice's `pos` by its pitch increment and wrap
at the loop. Eight voices mixed to the two physical outputs is very doable on
one SH-2.

- **SFX without a hidden channel:** give priority SFX a voice by *stealing* an
  existing one (e.g. voice 8) rather than adding a ninth. The next music note on
  that lane reclaims it. Keeps the voice budget fixed and behavior deterministic.
- **Validate sample data at conversion time.** Convert source instruments to the
  one format your mixer expects (e.g. 8-bit mono, unpacked) and *reject* packed,
  16-bit, or stereo samples in the build tool rather than mis-decoding them at
  runtime.

## Feeding the FIFO — cadence is everything

The PWM FIFO is **tiny (a few entries)**. Ways to keep it fed, in order of how
much you should trust them on a real console:

- **Slave SH-2 DMA from ping-pong buffers — the real-hardware-proven default.**
  The slave mixes ahead into SDRAM buffers and **SH-2 DMA channel 1**, paced by
  the PWM's DREQ, streams them to the FIFO; the DMA-end IRQ flips buffers. This
  is the Doom 32X Resurrection scheme, adopted by a shipped Tempest 2000 port
  after its first design proved silent on hardware. Details in "Real-hardware PWM
  via slave DMA" below. Use this for anything that will run on a real 32X.
- **Poll-fill / per-sample IRQ** (simplest, deterministic, emulator-friendly): a
  tight loop, timer, or PWM interrupt writes the next sample every ~1/rate. PWM
  Tracker and several ports do this and **it verifies fine in PicoDrive** — but a
  Tempest port found that driving PWM one sample at a time from the master's
  PWM-timer interrupt **played in emulators and was silent on a real 32X**.
  Treat it as *emulator-verified only* until it has been heard on hardware; a
  polled single-voice player (FULL polled on the mono FIFO) is the safer variant.

### The gotcha that turns music into buzz

**Redrawing the whole 32X framebuffer every frame starves the FIFO** — the long
framebuffer write blocks FIFO refills and you get a frame-rate *buzz* instead of
music. Fixes that matter:
- Initialize **both** frame buffers before starting PWM.
- Only redraw the framebuffer **on an actual change** (a pad action, a dirty
  region), not every VBlank. A music/UI screen that repaints on demand keeps the
  FIFO fed; one that blindly repaints every frame will buzz.
- If you must draw continuously *and* play audio, budget the work (partial
  updates, or push audio to a timer/DMA path) so the FIFO never runs dry.

## Which core owns audio

Simplest split: the **primary SH-2 owns the mixer and the display**, the
secondary SH-2 idles (or does unrelated work). You *can* put the mixer on the
slave, but then the shared sample/state buffers are in cached SDRAM and need
cross-core cache care (see architecture.md) — do that only if the master is
genuinely too busy.

## Verifying audio — it IS possible (see testing.md)

The *video* screenshot harness can't hear anything, but audio is still
testable: capture the emulator's **PCM output** and compare a spectral/tonal
fingerprint against a reference render. See `testing.md` ("Verifying audio").

## An 8-voice tracker mixer (S3M-style) → stereo PWM

Beyond a handful of SFX voices, a full **software tracker** runs comfortably on
one SH-2. Reference: a shipped PWM tracker player that mixes **eight** virtual
tracker voices down to the two physical PWM outputs at **11,025 Hz**.

- Keep exactly N `Voice` structs (8), each with independent **sample position,
  loop bounds, pitch, volume, and stereo pan**. Tracker row events (from
  S3M-derived data) address channels 1–8 and update those fields.
- Each output tick, advance every active voice's fractional sample position,
  fetch+scale its sample, sum to left/right accumulators, clamp, and push one
  amplitude to each FIFO. Eight simultaneous notes on row 0 is the stress test.
- **Priority SFX policy**: reserve the ability for a triggered SFX to steal or
  duck a voice so laser/impact effects cut through the music, then restore.
- The only registers used are the PWM ones — no YM2612/PSG/CD:

```
0x20004030  PWM control      0x20004034  PWM left FIFO
0x20004032  PWM cycle        0x20004036  PWM right FIFO
```

Keep the mixer fed every frame regardless of render load (the FIFO-starvation
gotcha above). If a heavy renderer can stall it, mix on the **slave SH-2**.

## Alternative: native Genesis music (XGM/SGDK) with the UI on the SH-2

The 32X still contains a full Genesis: **YM2612 + PSG driven by the 68000/Z80**.
For chiptune/VGM music you can skip PWM entirely and let the **Genesis side play
the music** while the SH-2 does the UI/game. Reference: an XGM player cartridge
using **SGDK's XGM driver**.

- **68000 side** (built with SGDK, `libmd.a`, linked into the 32X's 68000 ROM
  window at `0x880800`): loads the XGM/Z80 driver, calls `XGM_startPlay` /
  `XGM_pausePlay` / `XGM_stopPlay`, polls the pad with `JOY_readJoypad`, and
  reports status/elapsed.
- **SH-2 side**: the UI — track list, now-playing metadata, progress bar,
  equalizer — rendered to the 32X framebuffer.
- **They cooperate over the COMM registers** (SH-2 at `0x20004020`, 68000 at
  `0xA15120`): the SH-2 posts play/pause/stop/next commands; the 68000 posts
  status back. The 32X boot blob enables the adapter (ADEN), releases the SH-2s
  (nRES), and jumps the 68000 to the player.

Choose **PWM** (mixer/tracker/PCM, all on SH-2) when you want sample playback and
full SH-2 control of the sound; choose **XGM/SGDK** when you want authentic
YM2612/PSG Genesis music with minimal SH-2 audio code. Streamed **PCM through
PWM** (a rail shooter embedded a dozen PCM clips and mixed them through the PWM
FIFO) is the third option for voice/SFX-heavy games.

## Compressed samples: IMA ADPCM decoded on the slave SH-2

Full sample banks (music beds, engine loops, impacts) don't fit as raw PCM. Store
them as **IMA ADPCM** (~4:1) and decode on the fly while mixing. A shipped racer
(`wave-rider-gp`) converts **26 sounds to 11,025 Hz mono IMA ADPCM** and mixes
them to the PWM FIFOs on the **slave SH-2**.

- Convert offline to 4-bit IMA ADPCM at the mixer rate; store nibble streams +
  each clip's start predictor/step index in ROM.
- The slave decodes per-voice (predictor + step-index state machine, 4 bits →
  one 16-bit sample), sums active voices, clamps, and pushes L/R to the FIFO.
- **Batch the gain math** (compute per-voice gain once per block, not per sample)
  and keep the decode/mix routine **cache-line-aligned in SDRAM** (see
  `optimization.md`).
- **Dedicate the slave to audio** so master render spikes can't starve the FIFO;
  route the master→slave command block **cache-through** (see the cross-core note
  in `architecture.md`) or the slave mixes stale commands on hardware.

## Generating Genesis music from MIDI (a MIDI→VGM pipeline)

Several RPG ports leave music unimplemented because "MIDI → YM2612/PSG is a
project of its own." It is a real project, but a **build-time MIDI→VGM converter**
makes it tractable: convert the game's MIDI to a **VGM** register-log for the
YM2612 (6-ch FM) + SN76489 (3 tone + noise PSG) + YM2612 DAC, then play the VGM
**Genesis-side** (the XGM/SGDK path above, or a VGM player) while the SH-2 runs
the game. Reference tool: `midi2vgm` (Python, `Ingest → IR → Map+Allocate →
Emit`). The reusable knowledge — most of it hard-won and non-obvious:

**Pipeline stages.**
- *Ingest*: build a tempo map to absolute seconds; sustain pedal (CC64) extends
  notes; volume/expression (CC7/CC11) fold into a loudness curve; pitch bend +
  RPN range → a cents curve; MIDI channel 10 → GM drum map.
- *IR*: a chip-agnostic `Note`/`DrumHit`/`Part`/`Curve` contract.
- *Map + Allocate*: assign each **part** a role (lead/bass/harmony/arp/pad), route
  it to FM or PSG, and greedily schedule voices.
- *Emit*: time-sorted register writes + waits, deduped, GD3 tags, DAC stream
  commands, gzip to `.vgz`.

**The channel budget is brutal — 6 FM + 3 PSG tone + 1 noise + 1 DAC.** Dense
MIDI is oversubscribed, and *silence is the worst error*. Degrade gracefully:
- **Route per part, not per note** — timbral consistency matters more than any
  single note's fit, so an instrument never flips FM↔PSG mid-phrase.
- **Demand-driven routing.** Parts the PSG would ruin claim FM first (see the two
  PSG limits below), then fill FM, then **spill** the rest to PSG. Naïvely
  preferring FM leaves the three PSG channels idle while FM steals its own voices.
- **PSG can't sound below ~C2** (the tone divider floors out): fold sub-floor
  notes up whole octaves *and* penalise routing bass to PSG (keep bass on FM) —
  clamping instead produced ~890-cent errors.
- **PSG pitch resolution collapses in the high register**: compute a per-part mean
  **cents error** if it went to PSG and use it as a routing cost.
- **Thin chords to root + top voice** *before* allocation, but **only on pools
  that are actually oversubscribed** — a pad shouldn't swallow three channels when
  the melody needs them, and inner voices shouldn't be discarded when channels are
  free.

**Two FM lessons that only show up on a real chip:**
- **FM retrigger needs a gap.** Emitting key-off and key-on at the *same*
  timestamp means the envelope generator never sees the release and the note never
  restarts — it goes silent. Insert a ~**1.5 ms** gap (`KEY_GAP_S`).
- **Mix by musical role or the melody drowns.** A 3-note pad radiates ~3× the
  energy of a single lead note at equal velocity; offset levels per role (e.g.
  lead −6 dB, pad +12 dB) spread across each part's voice count.

**Percussion goes through the DAC (FM channel 6), pre-mixed offline.** Because
conversion is offline you can **pre-mix overlapping hits into one mono PCM
stream**, so the DAC is effectively polyphonic and drums never compete with
FM/PSG for voices; the only limits are stream rate (~13.75 kHz default) and 8-bit
depth. One sample per distinct GM key (kick/conga/crash are spectrally different),
long tails on cymbals, closed-hat chokes open-hat.
- **The 0x2B ordering trap:** a global YM2612 init that writes `0x2B = 0x00` at
  t=0 lands *after* the DAC-enable and silences every drum on hardware. Don't
  touch `0x2B` in init when drums are present, and **reserve FM channel 6 for the
  DAC** (exclude it from the key-off init *and* from voice allocation). Regression-
  test both.
- **PSG noise transient layer:** 8-bit ~13 kHz PCM dulls the attack, so also fire
  the SN76489 **noise channel** (short attenuation decay) for hats/snares/cymbals/
  toms over the DAC body (+~44% energy in 6–15 kHz). The noise channel is mono, so
  collapse hits within ~20 ms (loudest wins).

**VGM DAC-stream emission** (needs VGM ≥ 1.61): `0x67 0x66` PCM data block →
`0x90` stream control (chip YM2612=0x02, port 0, reg 0x2A) → `0x91` stream data
(bank) → `0x92` stream frequency → `0x2B=0x80` DAC enable → `0x93` start → song →
`0x94` stop, `0x2B=0x00`. Use NTSC clocks `YM2612 7,670,453 / SN76489 3,579,545`
(PAL `7,600,489 / 3,546,895`).

**Verify against a real core, and calibrate first.** Check the VGM against a
**cycle-accurate YM2612** (Nuked-OPN2) + SN76489 renderer, not an approximation —
the retrigger-gap and buried-melody bugs were *only* found once a real core was in
the loop (the same "validate your instrument" rule as `optimization.md`).
Calibrate before trusting anything: A4 → fnum 1083 at block 4, FM pitch error
< 1 cent across MIDI 24–107, SN76489 period `f = clock/(32n)`; then FFT the
rendered audio back and confirm it matches the source MIDI to ~0.1 semitone.

**Licensing.** A GM percussion sample set (e.g. FreePats, GPLv3+ **with a sample
exception** permitting use in a composition without licensing the composition)
should be **downloaded at build time, not bundled**, so the tool itself carries no
GPL obligation; the cycle-accurate core used only for verification (Nuked-OPN2,
LGPL) stays out of the shipped ROM. Convert the *user's own* MIDI; don't
redistribute the game's music (see `porting-workflow.md`).

## Streaming BGM as IMA-ADPCM on the slave, concurrent with SFX

The ADPCM note above is SFX-focused; a full RPG needs **streaming background
music** too. A shipped RM2K JRPG (Raintown Slickers) runs its whole soundtrack
this way: 6 tracks encoded at build to **11 kHz 4-bit IMA-ADPCM**, and the slave
SH-2 runs **one uninterrupted streaming ADPCM decoder plus a 2-voice PCM SFX
mixer**, both feeding the stereo PWM FIFOs at **~22 kHz**. Key properties:

- **Stream music straight from cartridge ROM** (uncached, read-only) — zero SDRAM
  cost for audio, leaving SDRAM for game state. A ROM **BGM directory**
  `(offset, num_samples, loop_flag)` indexes the tracks.
- **Play 11 kHz samples twice for a ~22 kHz PWM rate** (`PWM_CYCLE ≈ NTSC_SH2 /
  1045`): halves ROM footprint and mixer work for the same output rate.
- **Music and SFX mix concurrently** on the slave — the streaming decoder never
  stops when an SFX fires; SFX are a separate 2-voice PCM layer summed in.

### The cross-core audio command protocol (COMM)

A clean, reusable master→slave protocol (an instance of the COMM-allocation
discipline in `architecture.md`):

- **COMM2 = BGM command, COMM4 = SFX command, COMM6 = slave heartbeat, COMM0 =
  a video-ready magic** the slave waits on before PWM init so audio init can't
  race boot.
- **Edge-detect with a sequence number, not the value.** Each request packs a
  7-bit incrementing `seq` in the high byte plus the track/effect id (and a loop
  bit / a stop sentinel like `0x7F`). The slave re-triggers when `seq` changes,
  so **playing the same track or effect id twice in a row still fires** — a plain
  value compare would swallow the repeat. Bump the slave heartbeat every loop so
  the harness can prove the audio core is alive (a frozen COMM6 = slave crashed).
- Keep the master side tiny: `S_Play(id)` / `BGM_Play(id, loop)` just publish the
  next `seq|id` word; all decoding/mixing is on the slave.

### Match the source engine's music semantics

An RPG's scripts expect more than play/stop. Wire the event opcodes and game
events to real BGM control: **loop**, **fade-out** over N frames, and
**memorize / restore** — `MemorizeBGM` before a battle and `PlayMemorizedBGM` on
return so the field theme resumes where the map expects it (hook it into the map
loader, battle start, and victory/defeat/return paths).

## Which music path? Streaming ADPCM vs MIDI→VGM

Two viable, very different routes for music — pick per project:

- **Streaming IMA-ADPCM from ROM** (this section): plays the *actual recorded/
  rendered audio*, dead simple, zero SDRAM, concurrent with SFX on the slave. Cost
  is **ROM space** (minutes of 4-bit 11 kHz audio is megabytes) and it's PCM, not
  chip music. Best when you have the audio as files and ROM to spare (a ~4 MB
  cart comfortably holds a handful of looping tracks).
- **MIDI→VGM chiptune** (the pipeline above): authentic YM2612/PSG music at a
  *tiny* data size, played Genesis-side. Cost is needing the **MIDI** and a
  Genesis-side VGM player, and the conversion work. Best when source is MIDI, ROM
  is tight, or you want the genuine FM sound.

A game whose soundtrack is a huge MIDI (see the ROM-budget note in
`architecture.md`) usually wants MIDI→VGM; one with a few recorded loops and room
to spare is simplest with streaming ADPCM.

## Real-hardware PWM via slave DMA (the proven recipe)

The recipe a shipped Tempest 2000 port copied from Doom 32X Resurrection after
its interrupt-per-sample design turned out silent on a real console:

- **PWM**: `CTRL = 0x0185` (TM=1, RTP=1, L/R routing); `CYCLE` from the NTSC/PAL
  clock for your rate (22.05 kHz there).
- **DMA**: slave SH-2 **DMA channel 1**, request source = PWM DREQ, destination
  `0x20004034` — **32-bit long writes fill L and R in one transfer**;
  `CHCR1 = 0x18E5`; the DMA-end interrupt flips the ping-pong buffers. Its
  vector and level are **yours to choose** (programmed through the DMAC vector
  register) — Tempest uses vector 66 / IPRA level 4, Fight For Life vector 40 /
  level 14 — so match whatever your crt0 vector table installs rather than
  copying a number. Kengeki computes `CYCLE = 23,011,361 / 22,050 + 1` (NTSC) and
  streams 368-sample stereo blocks.
- **Read the buffers through the `0x2000_0000` cache-through mirror** — the slave
  wrote them through its cache; DMA reads physical memory (the cross-core cache
  rule from `architecture.md`, applied to a DMA engine).
- **Ramp from 0 to the centre value before starting**, so the speaker doesn't
  thump.
- **Never fetch audio from the cartridge bus on the slave while the master
  renders.** Copy the tune's sample data **ROM → SDRAM at load** (Tempest copies
  up to 136 KiB) and mix **several buffers ahead of the DMA** (up to four
  368-frame buffers). Cart-bus contention between the cores is a hardware effect
  emulators under-model.
- **On underrun, repeat the current buffer** rather than skipping ahead, so time
  doesn't shift; count underruns and show the counter in a `-DSHOW_FPS` build.
- The **68000 runs from Genesis work RAM**, not ROM, for the same reason — keep
  it off the cart bus — and the **Z80 is held in reset** when all audio is PWM.

### A polled single-voice player (Chilly Willy's recipe)

For SFX-only games, a much smaller real-hardware-rules player (a shipped Pacamaze
port): the slave runs **one preemptive voice**, polls **FULL on the mono FIFO**,
and emits each 11 kHz byte **twice** into a **22,050 Hz** clock
(`CYCLE 0x415` NTSC / `0x40B` PAL, `CTRL 0x185`), with samples **centred on 517**
and a **non-blocking MIN→CENTRE anti-click ramp** at start. The master triggers
via a COMM word whose `bit15` is a **toggle** and low bits are the sample id —
another edge-detect-not-value scheme (cf. the sequence-number protocol above).
Note the emulator artefact: **PicoDrive renders a ~100 ms filtered thump during a
black-screen boot** (its empty-FIFO model); on hardware the ramp keeps boot
silent — don't "fix" a sound that only exists in the emulator.

### If you do drive PWM from an interrupt: the routing details

A shipped Alien Assault II port mixes four 11 kHz PCM voices from a per-sample
PWM interrupt on the slave (emulator-verified; see the caveat above). The
plumbing is easy to get wrong:

- The slave's interrupt mask is a **byte register at `0x20004001`** (bit 0 = PWM).
- The pending bit is cleared by a **write to `0x2000401C`**.
- Install the handler in **both vector slots 0x43 and 0x44** — PicoDrive and the
  hardware documentation number the PWM interrupt differently.

## A ProTracker MOD player on the slave

Tempest's 8 MiB build plays the original Jaguar soundtrack (seven ProTracker
tunes) with an on-SH-2 MOD player (effects 0–6, 9, A–D, E1/E2/E9/EA–EE, F) mixed
with four SFX voices at 22.05 kHz. Lessons that transfer to any tracker player:

- **Tick at 50 Hz, not at the console's vblank.** ProTracker's default tempo
  (125 BPM, speed 6) already *is* 50 ticks/s — a row every 6 ticks — and that is
  what both the Jaguar replayer (every VBL on PAL, 5 of every 6 on NTSC) and a
  Mr. Boom port's player reproduce. The trap is ticking the player from the 60 Hz
  NTSC vblank, which plays every tune **20% fast**. Drive the tick from a 50 Hz
  accumulator (or the 5-of-6 rule), and check the original replayer's tick source
  if the format isn't plain ProTracker.
- **Pre-decode patterns and fold the period maths.** Unpack patterns at build
  time into `(sample, period-index, effect, param)` so the player never unpacks
  nibbles at 22 kHz; convert a period to a 16.16 playback step with **one divide
  per note** (e.g. `10542000 / period`) and nothing per sample. Mr. Boom plays four
  of the original's 4-channel modules this way (`.XM` files would need a second
  parser).
- **Headroom with a soft knee, not hard clipping.** Choose per-channel gains so
  4 MOD channels + SFX stay inside the PWM range, and apply a **soft knee above
  ±384** instead of clamping.
- **Rate-limit SFX per id and make speech exclusive**, so a multi-kill doesn't
  stack ten identical explosions or overlap two voice clips.
- **Select tunes exactly as the original does** (Tempest reproduces the
  Jaguar's `webtunes` table per 16-level group), and fall back to a small synth
  sequencer when a tune isn't in the ROM — the 4 MiB build only fits the title
  theme, the 8 MiB build carries everything (see SSF banking in
  `architecture.md`).

## More real-hardware audio lessons (five more shipped ports)

Fight For Life, Blade Arena: Kengeki and WipEout all feed PWM by **slave DMA**,
independently confirming the recipe above. What they add:

- **Mix at the source's native rate when you can.** Fight For Life mixes at
  **20,774 Hz — the Jaguar DSP's rate** — so the original's 8.8 pitch values play
  1:1 with no conversion table.
- **Hand-write the inner mix loop if GCC won't use `muls.w`.** Fight For Life's
  `mix_sh2.s` mixes in 5 instructions per sample (10 per 2, unrolled); GCC would
  not emit the 16×16 multiply.
- **On underrun, keep DREQ fed** — either repeat the current buffer (Tempest) or
  queue a short **silence block** (Fight For Life: 16 samples). Never let the DMA
  stall waiting for the mixer.
- **The slave can render *and* mix.** Kengeki runs its 12-voice mixer inside the
  **DMA-complete IRQ**, so the slave's main loop is free to rasterise half the
  screen; Fight For Life's slave draws the backdrop, floor and shadows and calls
  `audio_service()` every 8 floor quads and every 16 backdrop rows. A *music OFF*
  switch that skips the sequencer entirely hands the time back (+0–2 fps).
- **Use the slave for audio without sharing SDRAM.** Mr. Boom's slave never
  touches SDRAM the master writes — its only input is a COMM word in uncached
  peripheral space — so long framebuffer writes can't starve it, and its FIFO wait
  is bounded so it can't wedge.

### Song clocks overflow — never store `ticks << 16` in 32 bits

Fight For Life's title music froze for good at **66 seconds**: its sequencer kept
time as a 16.16 `uint32`, which can only count to tick 65,535 (66.2 s at 990
ticks/s). The 117 s title theme's next event was never reached; held and looping
notes kept sounding, so it read as "stuck", not "stopped". Keep **whole ticks and
a separate fraction**. Test with a host render longer than the longest song (FFL's
`make soundcheck` renders 250 s and requires both loop jumps — and fails with the
clock forced back to 16 bits).

### Read the sample-loop format exactly

The same port had read a patch's loop *length* as the loop *start*, so a held
guitar note replayed nearly the whole riff instead of its last 3,416-sample
sustain tail. Decode loop fields straight from the original replayer's code, and
gate it with a host render that measures the RMS across the loop point.

### Fitting ROM: compress long streams, mind the 512 KiB page

On an SSF cart, content that doesn't divide into **512 KiB pages** wastes the rest
of each page. City Carnage's eight 40-second radio stations (440 KB each as 11 kHz
8-bit PCM) each burned a whole page; storing them as **4-bit IMA ADPCM** put two
per page and took the cartridge from 5.5 MiB to **3.5 MiB** with nothing cut. The
decode is *cheaper* on the slave than PCM fetches: two table lookups, shifts and an
add per nibble, and one banked-ROM read per two samples instead of one. Encoder
details that matter:

- **Beam-search the encoder**, scoring whole trajectories on the error that reaches
  the speaker, instead of greedy per-sample IMA quantisation: **+2.2 dB** for free,
  same bitstream format and decoder.
- **Break ties on the 16-bit error.** Scoring only in the 8-bit output domain is
  full of ties; resolving them toward a smaller step index lets the step collapse
  to its floor and the track decodes to near-silence while still looking like a
  valid stream.
- **Prime the loop.** A decoder restarting at predictor 0 / step 0 fades in for
  ~70 samples every lap. Store the state the decoder ends in (a 4-byte header) and
  restore it at the loop seam.
- **Test the decoder that ships**: lift every stream back out of the built ROM and
  decode it with the decoder **compiled from the SH-2 source**, checking
  sample-for-sample against the encoder's model and SNR against the original. The
  emulator's "RMS in range" check passes a swapped nibble order, a byte-swapped
  header or an off-by-one step clamp; this check failed all six such bugs when
  they were tried.

Short one-shot SFX can stay raw PCM: if they already fit their page, compressing
them saves nothing.

### Audio assertions that catch real failures

- **Music never goes silent** (quietest block RMS above a floor) and **SFX cut
  through** it (peak ≥ 1.4× the floor) — Mr. Boom.
- **No DC offset**: a wrong PWM centre value shows up as a DC thump long before
  you'd hear it.
- **Exactly silent when off**: Fight For Life's music-OFF test requires RMS 0.0
  while off and > 100 when switched back on.
- **Not clipping**, over every song, rendered on the host with the shared mixer
  (Kengeki's `song_render.c`, FFL's 300 s per song).

### Original music without licensed samples

Kengeki ships 13 songs whose every instrument is **synthesised at build time** in
Python — Karplus-Strong plucked strings, cabinet-filtered distortion, formant
choirs, additive organ, inharmonic bells — written in a small pattern DSL, with a
`LINT=1` pass that flags on-beat melody notes a semitone off the chord. It's a
fully IP-clean route to a full soundtrack when you can't ship the original's.
