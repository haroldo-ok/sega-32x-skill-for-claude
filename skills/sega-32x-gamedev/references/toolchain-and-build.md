# Toolchain & build

## The devkit

Chilly Willy's **32XDK** ships a POSIX-only cross toolchain with two GCC 12.1
targets under one prefix (`GENDEV`, default `/opt/toolchains/sega`):

- `sh-elf-*` — Hitachi/Renesas **SH-2** (both 32X CPUs). `sh-elf-gcc`,
  `sh-elf-as`, `sh-elf-objcopy`, `sh-elf-nm`, `sh-elf-size`, `sh-elf-readelf`.
- `m68k-elf-*` — Motorola **68000** (the Genesis side).

Install (release `20220418`, the version d32xr and the reference ports pin):

```sh
curl -LO https://github.com/viciious/32XDK/releases/download/20220418/chillys-sega-devkit-20220418-opt.tar.zst
sudo tar --zstd -xf chillys-sega-devkit-20220418-opt.tar.zst -C /
```

If `--zstd` is unavailable, `zstdcat file.tar.zst | sudo tar -C / -xa`.
The archive unpacks to `/opt/toolchains/sega`. It also ships shared linker
scripts under `$(GENDEV)/ldscripts/` (e.g. `mars-md.ld` for the 68000 side).

The devkit is large; in CI, cache `/opt/toolchains` on a key like the release
tag and only download on a cache miss (d32xr does exactly this in
`.github/workflows/build.yml`).

A Docker/VSCode dev container is also viable — see d32xr's `.devcontainer/`
(Ubuntu base + `curl zstd make jq`, then the same download-and-extract).

## SH-2 compiler flags

Minimum correct flags for the master/game code:

```
-m2 -mb -std=c11 -mtas -fomit-frame-pointer -D__32X__ -DMARS
```

- `-m2` SH-2, `-mb` **big-endian** (mandatory), `-mtas` allow the `TAS`
  instruction (used for the inter-CPU spinlocks).
- `-D__32X__ -DMARS` are the conventional 32X build defines used across the
  codebase and headers.

Size/optimization for a release build:

```
-Os -flto -fuse-linker-plugin -ffunction-sections -fdata-sections -fno-common
```

For a genuinely hot translation unit (e.g. the PWM audio mixer that must
produce a sample every 1/22050 s) compile at `-O2` and **without LTO** so its
timing is predictable — d32xr compiles `marshw.c` at `-O1 -fno-lto` for the
same reason. Keep such exceptions to individual objects.

Debug build: add `-g -ggdb` and drop `-flto`.

## SH-2 link

```
-T mars.ld -nostdlib -Wl,--gc-sections --specs=nosys.specs \
  -Wl,-Map=output.map -Os -flto
LIBS = -lc -lgcc -lnosys        # add -lgcc-Os-4-200 only if present in your devkit
```

`--gc-sections` is what makes a small ROM, but it is also a classic
black-screen trap: if the game's entry points are not reachable from the kept
roots, the linker discards the whole game and keeps only the header, producing
a valid-looking ROM that boots to black. Guard against it in `verify_rom.py`
(assert `.text` is large and known code markers survive).

Library search paths depend on the GCC version dir; for 32XDK 20220418 that is
`12.1.0`:

```
-L$(GENDEV)/sh-elf/sh-elf/lib \
-L$(GENDEV)/sh-elf/lib/gcc/sh-elf/12.1.0
```

## 68000 side

The Genesis 68000 stays resident to poll controllers, service VBlank, and play
native YM2612/PSG music (often from build-time-converted VGM). Build it as a
tiny separate ELF, `objcopy -O binary` it to a `.bin`, and **embed that binary
into the SH-2 startup** (`mars_start.s` / `crt0.s` includes/`.incbin`s it). The
SH-2 boot code hands the 68000 its program at reset.

```
MDFLAGS  = -m68000 -std=c11 -Os -fomit-frame-pointer -ffunction-sections \
           -fdata-sections -Wa,--register-prefix-optional
MDLDFLAGS= -T md.ld -nostdlib        # or $(GENDEV)/ldscripts/mars-md.ld
MDLIBS   = -L$(GENDEV)/m68k-elf/lib/gcc/m68k-elf/12.1.0 -lgcc
```

## The linker memory map (mars.ld)

```
MEMORY {
  rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000   /* up to ~4 MiB cart  */
  ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003FC00    /* 256 KiB SDRAM      */
}
PROVIDE (__stack = 0x0603FC00);   /* master SH-2 stack top (grows down)        */
```

- `.text` + `.rodata` are placed at `0x02000000` (ROM) with load address `0`.
- `.data` is *addressed* at `0x06000000` (RAM) but *loaded* right after
  `.text` in ROM; the startup code copies it into SDRAM.
- `.bss` follows `.data` in RAM and is zeroed by startup; the heap (`sbrk`)
  grows up from `__bss_end`.
- If you use the **slave SH-2**, split the stack region: master top around
  `0x0603F800`, slave top around `0x06040000`, and lower `LENGTH` to `0x3F800`.

A ready-to-edit `mars.ld` is in `assets/mars.ld`.

## The ROM header / romfix step

A 32X cartridge carries a **Genesis** header and a **Mars** module header. After
`objcopy -O binary`, pad to a sane cartridge size and write the Mega Drive
checksum:

- `SEGA 32X ` at offset `0x100`; game title string in `0x100..0x18F`.
- Mars module header block (SH-2 entry points and vector bases) around
  `0x3C0..0x3F0`. On the reference ports the SH-2 master entry is `0x06000240`,
  slave `0x06000244`, master VBR `0x06000000`, slave VBR `0x06000120`.
- ROM-end (last byte offset) at `0x1A4`.
- Checksum at `0x18E` = 16-bit sum of all big-endian words from `0x200` to end.

Use `assets/romfix.py` (writes checksum + pads), or the C `romheaderfix` that
ships with d32xr. Run it as the last build step. `verify_rom.py` re-checks all
of the above so a bad header fails the build rather than the console.

## Minimal build recipe

The full, commented template is `assets/Makefile`. Shape:

```
1. Build 68000 ELF → md_start.bin
2. Assemble mars_start.s (which embeds md_start.bin) + core + platform objs
3. Link SH-2 → game.elf   (mars.ld, --gc-sections, LTO)
4. objcopy -O binary game.elf → game.raw
5. dd pad to cartridge granularity → rom/game.32x
6. romfix.py rom/game.32x            (header + checksum)
7. check: verify_rom.py + run_tests.py
```

Always print sizes after linking (`sh-elf-size game.elf`) and grep the map for
`__bss_end` so RAM budget regressions are visible in the build log.

## Alternative: distro cross-toolchains (instead of the 32XDK tarball)

The primary path in this skill is Chilly Willy's 32XDK unpacked at
`GENDEV=/opt/toolchains/sega` (giving `sh-elf-` and `m68k-elf-` prefixes). If
that tarball isn't available, you can build 32X ROMs with **distribution cross
packages** instead — this is how God of Thunder 32X's `setup.sh` bootstraps:

```sh
sudo apt-get install -y \
    gcc-sh-elf binutils-sh-elf libnewlib-sh-elf-dev \
    binutils-m68k-linux-gnu
```

That yields an `sh-elf-` GCC for the SH-2 side and an `m68k-linux-gnu-`
binutils for the 68000 side (assemble/link the small MD boot blob with
`m68k-linux-gnu-as` / `-ld` — you only need binutils there since the 68000 code
is tiny/assembly). Wrap the install in an idempotent `setup.sh` (check
`command -v sh-elf-gcc` first) so it's safe to re-run after a cold start where
only the home dir persisted.

Trade-off: the tarball gives a known-good, matched SH-2 + 68000 pair and the
`ldscripts/` (e.g. `mars-md.ld`); the distro route is easier to install but you
supply your own MD linker script and confirm the newlib specs. Either way the
compiler flags (`-m2 -mb`, big-endian), the memory map, and the ROM header/
checksum fix-up are identical — only the toolchain *provenance* differs.

## GCC 12.1 SH-2 miscompile traps (devkit compiler)

The 32XDK's `sh-elf-gcc` is **GCC 12.1**, and it miscompiles several patterns for
SH-2 — the program is valid C, passes on a desktop host, and misbehaves only on
hardware/emulator. These were hit and worked around in a shipped physics game
(beachy-beachy-ball). Know them; they masquerade as "impossible" logic bugs:

- **12-byte struct returns** — returning a 3-word struct by value can corrupt.
  Return through a caller-supplied pointer (out-param) instead.
- **64-bit multiply chains** — sequences of `long long` multiplies can be
  miscompiled. Use the SH-2 hardware **MAC**/`dmuls` via small inline-asm or
  intrinsic macros for the fixed-point multiply, rather than leaning on the
  compiler's `long long` path.
- **Dropped stores** — under optimization the compiler can elide a store it
  wrongly proves dead. If a written value "doesn't take", make the destination
  `volatile` or route through a pointer it can't reason away.
- **Calls across mixed optimization levels** — calling between translation units
  built at *different* `-O` levels can break the ABI assumptions. **Build the
  whole game at one optimization level** (e.g. everything `-O2`), isolating only
  a well-understood module (an `-Os` audio mixer) if you must.

Practical defaults that dodge all four: compile the core at a **single `-O2`**,
prefer **pointer math and MAC macros** over struct-return + `long long` idioms,
and mark a store `volatile` the moment a value mysteriously fails to persist. If
a routine is wrong only at `-O3`/`-flto`, drop *that* routine to `-O2` first
before hunting your own logic.

## Workspace / snapshot gotcha: output directory names

In some sandboxed workspaces the directory *names* `dist/` and `build/` are on
the snapshot **exclusion** list — anything written there is discarded between
sessions, so a ROM that "built fine" vanishes. Write build output to
neutrally-named dirs like `rom/` and `obj/` instead. Relatedly, the toolchain
under `/opt/toolchains/sega` often lives **outside** the snapshotted workspace
and won't survive either; keep a `setup.sh` that reinstalls the toolchain (and
PicoDrive) so `make` can be made whole again with one command.
