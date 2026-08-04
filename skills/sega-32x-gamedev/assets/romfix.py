#!/usr/bin/env python3
"""Pad a 32X ROM to a valid cartridge size and write the Mega Drive checksum.

The Mega Drive header checksum at 0x18E is the 16-bit sum of every big-endian
word from 0x200 to the end of the ROM. The ROM-end offset (last byte) is stored
big-endian at 0x1A4.

Usage:  romfix.py ROM.32x [--title "GAME TITLE"] [--gran 524288]

This tool assumes your startup / mars_start.s already emitted a correct
Genesis + Mars module header (SEGA 32X id, SH-2 entry points, vector bases).
It only fixes the size-dependent fields. verify_rom.py re-checks everything.
"""
import argparse
import struct
import sys


def fix(path: str, title: str | None, gran: int) -> int:
    with open(path, "rb") as f:
        data = bytearray(f.read())

    if data[0x100:0x108] != b"SEGA 32X":
        print(f"warning: {path} does not start its header with 'SEGA 32X' "
              f"at 0x100 (got {bytes(data[0x100:0x108])!r})", file=sys.stderr)

    # Optional: stamp the domestic/overseas title fields (0x120 and 0x150,
    # 48 bytes each, space-padded). Only if requested.
    if title is not None:
        t = title.upper().encode("ascii", "replace")[:48].ljust(48, b" ")
        data[0x120:0x150] = t
        data[0x150:0x180] = t

    # Pad up to the granularity boundary.
    if gran > 0 and len(data) % gran:
        data += b"\x00" * (gran - (len(data) % gran))

    # ROM end = last valid byte offset.
    struct.pack_into(">I", data, 0x1A4, len(data) - 1)

    checksum = 0
    for i in range(0x200, len(data) - 1, 2):
        checksum = (checksum + struct.unpack_from(">H", data, i)[0]) & 0xFFFF
    struct.pack_into(">H", data, 0x18E, checksum)

    with open(path, "wb") as f:
        f.write(data)

    print(f"{path}: {len(data):,} bytes ({len(data)/1024/1024:.2f} MiB), "
          f"checksum 0x{checksum:04X}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--title", default=None)
    ap.add_argument("--gran", type=int, default=512 * 1024,
                    help="pad granularity in bytes (0 = no padding)")
    ns = ap.parse_args()
    return fix(ns.rom, ns.title, ns.gran)


if __name__ == "__main__":
    raise SystemExit(main())
