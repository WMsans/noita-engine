# Sandstone crust — design

## Intent

Since slabs caves shipped, the desert's surface sand pours straight down into the caves below
and the dunes sink. A thin shelf of a new solid material, `sandstone`, under the dunes holds
them up. Sand running down a cave entrance is fine; the goal is only that the terrain as a whole
does not drain into the caves.

## Goals

- A new `sandstone` material: Solid class (never falls), sandy-but-darker colour.
- A thin sandstone band directly beneath the desert's surface sand, following the dune contour.
- Ordinary caves (classic and slabs) do not carve the band.
- Cave entrance columns still cut through it, as they cut through the surface today.

## Non-goals

- Stopping sand from flowing down an entrance.
- Holding up the sand *below* the crust (desert base_material is sand; slab walls deeper down
  may still crumble into caves — the crust only shields the dunes above it).
- Lining cave walls or ceilings elsewhere.

## Approaches

1. **Per-biome crust layer, protected from caves — chosen.** New `crust_material` and
   `crust_depth` biome fields. Cells at depth `[surface_depth, surface_depth + crust_depth)` take
   the crust material and are skipped by cave carving except on entrance columns. Keeps the
   current cave tuning; reusable by other biomes.
2. Push caves deeper in the desert and fill the gap with sandstone. Simpler, but removes the
   near-surface caves the slabs tuning was built around.
3. Turn only sand directly above cave air into sandstone. Least material, but needs neighbour
   lookups across chunk borders and leaks wherever a cave opens later.

## Decided

- Approach 1.
- Entrances keep breaking through the crust.

## Design

- `BiomeDef` gains `std::string crust_material` (`""` = none) and `int crust_depth = 0`;
  `BiomeIds` gains `crust`; `Blended` gains `crust_depth`, blended by biome weight like
  `surface_depth`. Resolved and validated in `compile()` like the other material names.
- In `generate()`: after the surface check, a cell with `top <= depth < top + crust` and a
  picked biome that has a crust becomes the crust material. Cave carving skips a crust cell unless
  its column is an entrance column (same `n_entrance_` test `taper_at` uses — factor that test
  out so both share it).
- `PixelBiome` exposes `crust_material` and `crust_depth` to Godot.
- `materials/sandstone.tres`, added to `default_set.tres`.
- `worldgen/desert.tres`: `crust_material = &"sandstone"`.

## Builder's calls (Claude's, open to change)

- `crust_depth = 6` for the desert — "thin" but thick enough that the dithered biome border
  doesn't leave pinholes.
- Constant thickness, no noise. Add a wobble later if it looks too tidy.
- Sandstone colour `Color(0.72, 0.6, 0.4)`, density 2.4, light_opacity 1.0.
- At a desert/non-desert border, cells whose dithered pick is a crust-less biome get no crust;
  a little sand may trickle through there. Acceptable at a transition.

## Tests

- Core: in a desert-only config with slabs caves at high air, every cell in the crust band is
  sandstone except on entrance columns; a biome without a crust is unchanged (classic output
  byte-identical to before).
- Godot: the new `PixelBiome` properties round-trip into the generator.
