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
