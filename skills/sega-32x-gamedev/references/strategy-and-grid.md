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
