# Reusable 32X software-3D engine (clean-room)

Drop-in fixed-point 3D for a new 32X game. See `references/software-3d.md`.

- `r3d.h` / `r3d.c` — fixed-point (16.16) math, camera transform, perspective
  projection with a **reciprocal-table divide** (no runtime divide), and a
  flat-shaded triangle rasterizer that writes into a caller-supplied
  `{u8 *px; int w,h;}` buffer (so it host-tests unchanged).
- `gen_tables.py` — generates `sintab.inc` (256-entry 16.16 sine) and
  `recip.inc` (4096-entry reciprocal). Run at build time:
  `python3 gen_tables.py src/sintab.inc src/recip.inc`.

Author your own clean-room `mesh_t` meshes (verts + colored tris). Give distinct
objects distinct palette colors. Two shipped games (a rally racer and a rail
shooter) were built on this exact engine.
