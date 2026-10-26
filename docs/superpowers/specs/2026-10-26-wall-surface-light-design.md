# Wall Surface Light — Design

Date: 2026-10-26
Status: draft
Parent spec: `docs/superpowers/specs/2026-10-26-lighting-design.md`. This adds one pass to its
light pipeline; everything else there stands.

## Intent

With lighting on, every solid material is pitch dark, even right next to the player's torch.
Walls a light reaches should show their colour and texture, while places far from any light stay
dark, the way Noita keeps both mystery and readable lighting (user). Darkness depends only on
distance from light: no fog of war, no memory of explored areas (user).

**Why it happens today:** each cell's light is measured at the cell itself. A rock cell sits
inside rock (`light_opacity = 1.0`), so every cascade ray it casts is blocked by the next cell
over (`LIGHT_CASCADE_COMPUTE`'s march). Air gets lit; every solid stays at `ambient` (0.05) ×
its colour.

## Goals

- Solid cells pick up light from the open space next to them and fade with depth into the
  material: a lit skin **8–10 cells deep** (user), dark beyond it.
- Shadows stay hard: a wall with only dark air in front of it stays dark.
- Works at `light_resolution` 1 and 2.

## Non-goals / anti-goals

- Fog of war or explored-area memory (user).
- Per-material skin depth (one depth for every solid, for now).
- Anti-goal: light leaking through thin walls, or open-air shadows softened by the spread.

## Approaches

- **A. Spread light into surfaces after the light pass (chosen by the user).** A compute pass
  after `LIGHT_OUTPUT_COMPUTE`, before bloom: each solid cell takes the brightest light of its
  neighbours, dimmed per cell of depth. Keeps hard shadows, cheap, least code. Trade-off: same
  depth for every solid.
- **B. Rays ignore the first cells of the solid they start in (rejected).** Skin falls out of
  the light model, but any wall thinner than the skin leaks light, and at 8–10 cells deep the
  demo's 6-cell wall would leak.
- **C. Lower solids' `light_opacity` (rejected).** One-line change, but light soaks through
  walls everywhere and shadows go soft.

## Design

- **Only solid cells receive spread light.** A cell is solid when its input opacity is at least
  a threshold (my call: 0.5, so rock, dirt, wood, grass, sand qualify; water, lava, acid, oil,
  gases don't — they're already lit by the rays). Open cells keep their light untouched, so the
  spread can't blur shadows in open air.
- **Spread:** solid cell light = max(own light, brightest neighbour's light × falloff). Repeating
  this pushes the light one cell deeper per step, out to the skin depth. Light only flows
  solid-ward from lit open cells, never back out.
- **Falloff (my call):** brightness at the surface (the first solid cell) is close to the light
  in the open cell in front of it, and fades to roughly nothing at the skin depth.
  The exact curve (per-step factor vs linear) is the builder's call, tuned by eye in
  `lighting_demo`.
- **Setting:** one live `PixelWorld` property for the skin depth in cells (my call:
  `light_surface_depth`, default 10, 0 turns the pass off). At `light_resolution` 2 it covers
  the same depth in cells (half as many texels).
- **Inputs:** the cell-resolution input texture already holds opacity in alpha; the pass reads
  it alongside the light texture. Bloom runs after, so it sees the spread light (walls stay
  below the bloom threshold in practice).

## Constraints

- Fits the parent spec's GPU budget: light + bloom ≤ 3 ms mean on the M1 in `bench_world`, frame
  p99 ≤ 16.6 ms. Measure with a local `RenderingDevice` on Metal (timestamps are stubbed there).
- No CPU work or readback added.

## Testing

- GPU-only, so checked by eye in `lighting_demo` (torch next to the wall, the 6-cell wall still
  casts a hard shadow, lava room walls lit) and by the `bench_world` budgets.

## Left to the builder

- Iterated 3×3 passes (ping-pong, one cell per pass) vs one wider-kernel pass, and which
  scratch texture to use.
- The falloff curve, and whether diagonal neighbours count as one step or √2.
- Final setting name.
