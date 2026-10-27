# Cave styles — design (small change)

## Intent

The caves the generator makes today are thin outline-like tunnels around round rock blobs
(rock is ~85% of the area). The user wants caves shaped like their reference image: open space
is the majority, and rock forms wide, flat, lumpy slabs with ragged edges and small holes,
linked into one connected maze. Today's algorithm is not thrown away: it becomes one selectable
cave style, and the new look is another.

## Goals

- A per-biome **cave style**: `classic` (today's algorithm, unchanged) and `slabs` (new).
- `slabs` matches the reference's rock shape:
  - air is about 50–60% of the area (below the surface taper);
  - rock masses are wide and flat, typically 3–5× wider than tall, joining into long shelves;
  - the edges crumble at a small scale (a few cells) over a smooth large-scale shape;
  - the rock has holes and windows in it;
  - the open space is connected: the largest connected air region holds ≥ 90% of all air in a
    sample window.
- Where two biomes with different styles meet, the cave shape blends smoothly. There's no seam.
- The shipped data puts **every biome on `slabs`** for now (the user's call). `classic` stays
  available.

## Non-goals / anti-goals

- Shape of rock against air only. No platforms, scaffolding, ore speckle, back wall or other
  dressing.
- `classic` must not change: a biome on `classic` generates exactly the same cells as today, so
  existing terrain tests pass untouched.
- No change to biome bands, regions, surface height, rooms or the streaming model. Each chunk
  still generates on its own, and `cave_at(wx, wy)` stays a per-cell query that agrees with
  `generate()`.

## Approaches

- **A. Stretched, warped noise with a rough edge (chosen).** One smooth fBm field, sampled with
  y scaled up so blobs come out wide and flat, is domain-warped by a second field. A fine noise
  term roughens the edge, and a sparse noise punches holes. The field is cut at a level that gives
  the air share. This is a pure per-cell function, so it fits chunk independence and `cave_at`.
  Blending styles is just mixing numbers. Connectivity is likely, though not guaranteed, at
  ~55% air, and it gets measured.
- **B. Noise plus cellular-automata smoothing.** This gives the most organic crumble. But it needs a
  computed border around every chunk at several times the cost, and it breaks the per-cell
  `cave_at` that room placement uses. Not chosen; a light smoothing touch can come later if A's
  edges look too clean.
- **C. Hand-drawn tile pieces.** Needs art assets, and the work is shape only. Dropped.

## How it works

- `BiomeDef` gains `cave_style` (`Classic` = 0, the default, or `Slabs`). `PixelBiome` exposes it
  as an enum property. A `.tres` without the field reads as `classic`.
- Each style yields a **signed cave margin** per cell (> 0 means air):
  - classic: `max(tunnel*taper − |n_tunnel|, n_cavern − (1 − 2·cavern·taper))`. This is exactly
    today's test rewritten as a margin, so a pure-classic cell gives the same answer.
  - slabs: warped, stretched fBm, plus a fine roughness term, plus a hole term, minus a cut level
    derived from the biome's air amount and the taper.
- `cave_for` sums each style's margin weighted by the biome weights already computed for the
  cell, and the cell is cave when the sum is > 0. For a cell whose biomes all share one style this
  reduces to that style alone. The classic margin is only evaluated when a classic biome has
  weight, so pure-classic worlds cost the same as today.
- The surface taper and entrance noise apply to `slabs` the same way they apply to `classic`.
- **My call:** frequencies and shape knobs (slab frequency, vertical stretch, warp strength,
  roughness frequency and strength, hole frequency) live on the generator, next to today's
  `tunnel_frequency` / `cavern_frequency`. The amount (target air share, and maybe a hole amount)
  lives on the biome, next to today's thresholds. Each new noise gets its own fixed salt.
- Liquid pools, pockets and surface skins are untouched: they keep using today's fields inside
  whatever the cave test says is air.

## Verification

- **Tuning loop:** a throwaway renderer (scratchpad, not committed) draws `cave_at` over a
  2000×900 window at a few depths and seeds, next to the reference image. It also reports air share,
  the largest-connected-air fraction and a typical rock-slab width:height. Iterate until both the
  eye and the numbers in Goals agree.
- **Tests** (core, doctest):
  - existing terrain/room tests pass unchanged (classic is untouched);
  - slabs air share lands in the target band over a sample window;
  - `cave_at` agrees with `generate()` for slabs, including across chunk borders;
  - a slabs/classic biome border has no hard seam (the cave margin is continuous across it);
  - same seed gives the same cells.
- `./build.sh --verify` passes, and the world demo looks right in game.

## Left to the builder

The exact noise frequencies, stretch, warp, roughness and cut values, plus the per-biome amounts
for the shipped `.tres` files. These are tuned in the loop above. The working tree already has
uncommitted cave tweaks in `default_generator.tres`, `desert.tres` and `forest.tres`. They're
built on top of, not reverted.
