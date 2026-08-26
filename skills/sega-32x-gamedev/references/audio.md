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
one L and one R amplitude per sample. Amplitude is ~12-bit into the FIFO.

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

The PWM FIFO is **tiny (a few entries)**. Two ways to keep it fed:

- **Poll-fill at the sample rate** (simplest, deterministic, emulator-friendly):
  a tight loop / timer writes the next mixed sample every ~1/rate. This is what
  PWM Tracker does.
- **DMA** (more complex): let the SH-2 DMA controller stream a buffer; refill on
  the half/full interrupt. More headroom, more moving parts.

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
