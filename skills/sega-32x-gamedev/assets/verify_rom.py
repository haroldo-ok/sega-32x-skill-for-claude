#!/usr/bin/env python3
"""Static structural checks for a built Sega 32X ROM and its SH-2 ELF.

These run in the build and catch the common "compiles but boots to black"
failures before the emulator ever runs: bad header/checksum, a ROM whose game
code was stripped by --gc-sections, and RAM/section layout that will not boot.

Usage:  verify_rom.py ROM.32x ROM.elf [--title SUBSTR] [--marker STR ...]

--title    a substring that must appear in the Genesis header (e.g. "SKYROADS").
--marker   a string from real game code (not the header) that must be present
           in the ROM. Pass a couple from DIFFERENT source files; if the linker
           wrongly discarded the game, only the header survives and these
           vanish. This is the single best anti-black-screen static guard.

Adjust the SH-2 entry-point/vector-base expectations to match your startup.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# SDRAM layout limits (see mars.ld). BSS must not reach the SH-2 stacks.
SDRAM_BASE = 0x06000000
ROM_TEXT_BASE = 0x02000000
STACK_GUARD = 0x0603C000     # keep __bss_end below this


def fail(msg: str) -> None:
    raise SystemExit(f"ROM VERIFY FAILED: {msg}")


def elf_sections(path: Path) -> dict:
    d = path.read_bytes()
    if d[:4] != b"\x7fELF" or d[4] != 1:
        fail("SH-2 output is not ELF32")
    endian = ">" if d[5] == 2 else "<"
    e_shoff = struct.unpack_from(endian + "I", d, 32)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(endian + "HHH", d, 46)
    headers = [struct.unpack_from(endian + "10I", d, e_shoff + i * e_shentsize)
               for i in range(e_shnum)]
    shstr = headers[e_shstrndx]
    names = d[shstr[4]:shstr[4] + shstr[5]]
    out = {}
    for h in headers:
        end = names.find(b"\0", h[0])
        name = names[h[0]:end].decode("ascii", "replace") if end >= 0 else ""
        out[name] = (h[3], h[4], h[5])  # addr, file offset, size
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("elf", type=Path)
    ap.add_argument("--title", default="")
    ap.add_argument("--marker", action="append", default=[],
                    help="game-code string(s) that must survive linking")
    ap.add_argument("--min-text", type=int, default=0x10000,
                    help="minimum plausible .text size in bytes")
    ns = ap.parse_args()

    rom = ns.rom.read_bytes()

    # --- cartridge shape ---
    if len(rom) < 0x20000 or len(rom) > 4 * 1024 * 1024:
        fail(f"implausible cartridge size: {len(rom)}")
    if len(rom) % 8192:
        fail("ROM is not padded to an 8 KiB cartridge boundary")

    # --- Genesis / Mars header ---
    if not rom[0x100:0x110].startswith(b"SEGA 32X"):
        fail("Genesis header does not identify SEGA 32X at 0x100")
    if ns.title and ns.title.encode() not in rom[0x100:0x1A0]:
        fail(f"title substring {ns.title!r} missing from Genesis header")

    stored_end = struct.unpack_from(">I", rom, 0x1A4)[0]
    if stored_end != len(rom) - 1:
        fail(f"header ROM end {stored_end:#x} != actual {len(rom)-1:#x}")

    stored_sum = struct.unpack_from(">H", rom, 0x18E)[0]
    calc_sum = sum(struct.unpack_from(">" + "H" * ((len(rom) - 0x200) // 2),
                                      rom, 0x200)) & 0xFFFF
    if stored_sum != calc_sum:
        fail(f"checksum mismatch: header {stored_sum:04X}, calc {calc_sum:04X}")

    # --- game code actually linked (anti --gc-sections black screen) ---
    for marker in ns.marker:
        if marker.encode() not in rom:
            fail(f"linked game-code marker missing: {marker!r} "
                 f"(did --gc-sections strip the game?)")

    # A real ROM has a meaningful amount of non-zero content. Use an absolute
    # floor rather than a ratio, so a small program legitimately padded into a
    # large cartridge is not flagged (the --marker checks guard a stripped game).
    nonzero = sum(x != 0 for x in rom)
    if nonzero < 2048:
        fail("ROM payload is essentially empty (no code linked?)")

    # --- ELF section sanity ---
    s = elf_sections(ns.elf)
    for name in (".text", ".data", ".bss"):
        if name not in s:
            fail(f"ELF section missing: {name}")
    text_addr, _, text_size = s[".text"]
    data_addr, _, _ = s[".data"]
    bss_addr, _, bss_size = s[".bss"]
    if text_addr != ROM_TEXT_BASE:
        fail(f".text not at ROM base: {text_addr:#x}")
    if text_size < ns.min_text:
        fail(f".text implausibly small ({text_size:#x}); game likely stripped")
    if data_addr != SDRAM_BASE:
        fail(f"initialized data not in SDRAM: {data_addr:#x}")
    if bss_addr + bss_size >= STACK_GUARD:
        fail(f"BSS collides with SH-2 stacks: end={bss_addr + bss_size:#x} "
             f">= {STACK_GUARD:#x}")

    print(f"ROM verify OK: {len(rom):,} bytes, checksum {stored_sum:04X}, "
          f".text {text_size:,}, .bss end {bss_addr + bss_size:#x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
