# Strategy & grid games on the 32X (RTS, tactics, grid crawlers)

A top-down RTS or a grid dungeon crawler needs three things the shmup/3D docs
don't cover: **pathfinding**, **visibility/fog**, and a way to make **discrete
grid logic look smooth**. Reference: a shipped Warcraft-style RTS (`warcraft-32x`
/ WinWar) and a grid dungeon crawler (`noudar-32x`).

## Keep grid logic deterministic; interpolate only the picture

The most important pattern for both genres: **the simulation runs on integer grid
cells and stays 100% deterministic; the renderer interpolates between cells for
smoothness.** Collision, pathfinding, and rules never see the in-between
positions.

- RTS: units step cell→cell on a 64×64 grid; the renderer walks the visual
  position across the cell over ~12 frames (sub-tile interpolation). Collision
  and A* still resolve on whole cells.
- Grid crawler: movement is one cell / 90° per turn, but the camera slides ⅛ cell
  and 1/32 turn per frame, so a turn-based game scrolls like a "free-look"
  90s crawler.

This keeps saves, replays, and host tests exact (integer state) while the display
looks continuous. Never let the interpolated value feed back into logic.

## A* pathfinding (HAL-free and host-tested)

Put A* in its own translation unit with no hardware dependency so the ROM and the
host tests link the *same* implementation:

- **8-direction** movement with an **octile heuristic** (`D*max + (D2-D)*min`),
  **no diagonal corner-cutting** (a diagonal step is illegal if either
  orthogonally-adjacent cell is blocked).
- **Building footprints** block multiple cells; **dynamic-unit avoidance** treats
  other units as transient obstacles; a **blocked-destination fallback** routes
  to the nearest reachable cell; **stuck-path replanning** re-runs A* when a unit
  can't advance.
- Fixed, preallocated open/closed sets sized to the grid — no heap.
- **Host tests are cheap and decisive**: assert the path on direct, detour,
  corner-cutting, and unreachable-goal cases. Pathfinding bugs are miserable to
  see in a screenshot and trivial to catch in a unit test.

## Three-state fog of war

Model each cell's visibility as **Unknown → Fog (remembered) → Visible**, driven
each frame from the player's live units/buildings:

- **Unknown**: never seen; draw blank.
- **Fog**: seen before, out of current sight; draw the remembered terrain/building
  but **hide dynamic entities** (enemies, projectiles) that are there now.
- **Visible**: in current sight; draw everything live.

Make the **minimap discovery-aware** (only reveal explored cells). Recompute
visibility from unit sight radii; a coarse per-cell byte is plenty.

## RTS combat/economy as data + small state machines

- **Autonomous AI** as per-unit states: idle **aggro scans** within a radius,
  **attack-move acquisition**, **retaliation** on taking damage, **target
  pursuit**, attack **ranges**, fixed **cooldowns**, **armor mitigation**, and
  type-specific randomized damage. Enemy and player units run the same code.
- **Timed actions** (attack windup → impact-time damage → hit flash; multi-stage
  death → persistent non-blocking corpse; ranged shots as visible projectiles
  with travel time and impact-time damage).
- **Worker economy loop**: target a resource node → harvest a carry quantity →
  auto-return to a drop-off → deposit → repeat; finite nodes deplete (e.g.
  forest tiles flagged by a passability value become traversable when exhausted).
- **Timed construction/training**: footprint validation, progressive hit points,
  completion bars, resource cost + queue timer, adjacent spawn-cell selection.

## Directional sprites via horizontal flips

Animate 8-direction units without 8× the art: store the east-facing frames and
**horizontally flip for west-facing** directions (idle/walk/attack/death
sequences). Halves sprite ROM for directional actors.

## Turn-based crawler rules (grid RPG)

For a grid dungeon crawler the rules are a compact deterministic core (reimplement
from the original, don't paste): `damage = attack − defense`; monsters act once
per player turn and chase along the dominant axis; **flood-fill** mechanics
(ropes/doors that open a whole section — "you hear mechanisms"); item/faith state
that carries across levels; parse the original **ASCII level files** (e.g. 40×40)
with the original tile-property sheets driving wall/floor/ceiling/masked flags.
All host-testable (spawns, blocking, flood-fills, drops, ammo math, level
completion, state carry-over, death) — a shipped crawler asserts ~32 such checks.

See `software-3d.md` for the first-person **raycaster** that renders such a grid,
and `architecture.md` for **SRAM saves** that persist campaign state.

### Turn-based input conventions on a 3-button pad (Doom RPG)

A grid crawler driven turn-by-turn needs a few pad conventions the original
phone/keyboard game didn't: **D-pad moves/turns with hold-repeat**; **B+D-pad
strafes**; a **tap of B passes one turn**; **bump into a door to open it**; A
fires. One action = one turn, so the simulation stays strictly turn-stepped and
host-testable (`test_dung` covers movement, combat, doors, items, dialogs on the
original 32×32 grids), while the raycaster animates the transition.

## Arena grids that are correct by construction (Mr. Boom)

A bot spawned into a pocket it could never leave — the class of bug hand-drawn
mazes create. Real Bomberman geometry is a **lattice**: an indestructible pillar
only where *both* cell coordinates are even. Then every odd row and column is a
continuous corridor, every non-pillar cell has an odd coordinate, and the open area
is **always one connected region**. Level variety comes from *which* lattice
pillars are present — and removing a pillar can never disconnect anything, so
every variant is correct by construction (a `check_connected()` pass still opens
any sealed pocket).

Prove it anyway, over seeds: Mr. Boom's `test_stages.c` runs all 8 stages × 25
seeds and checks terrain connectivity, that all 8 spawns are mutually reachable,
that every spawn has a survivable opening bomb, and that in full 8-bot matches
every bot alive after 10 s has actually moved somewhere. Run it at both ends of any
density parameter (its brick fill scales from 72% for two players down to 48% for
eight), and re-run it whenever spawn rules change — monster pens that bricked up a
corner's only exit were caught this way.

Other rules that held up:

- **One function owns each rule.** Roster limits (1–2 humans from two ports, up to
  8 bombermen) live in `mrb_settle_roster()`, called after every menu edit; a test
  pins the clamp and which slots a pad actually drives.
- **The bot keeps the original's structure**: a danger grid, a BFS travel-cost
  grid, and a fixed priority — go safe → go for a bonus → go place a bomb → idle.
- **Make the three tile classes unmistakable**: floor (flat, walkable), soft brick
  (an object on the floor with a dark halo — removable), hard wall (fills its cell
  edge to edge with a hard frame — permanent). Generate the treatment at conversion
  time and fail the build if any two classes look alike.
