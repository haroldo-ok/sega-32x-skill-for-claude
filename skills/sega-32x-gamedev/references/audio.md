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
