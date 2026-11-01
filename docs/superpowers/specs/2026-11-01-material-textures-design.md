# Material textures and generated crust — design

## Intent

Terrain today is a flat colour per material with per-cell brightness jitter. Make it look like
Noita: solid and powder materials show a pixel-art texture, and generated cave walls carry a
lighter, chunkier crust where they meet open space (see the user's reference screenshot: dark
speckled rock with a bright irregular rim along the cave edge).

How Noita does it (from `data/materials.xml` in the community data dump, vexx32/noita-data):

- A material's `<Graphics texture_file="…">` is sampled at the pixel's world position when the
  pixel is created; the colour then travels with the pixel (falling rubble keeps its texture).
- The crust is `<Edge><EdgeGraphics type="EVERYWHERE" percent="0.3">` with six ~16×20 blob
  images (`edge_rock_1..6.png`), randomly rotated, stamped along air-facing edges at ~30% of edge
  positions. Inference (not documented): stamping happens at world generation and is baked into
  pixel colour, so dug tunnels have no crust and broken crust falls as light rubble.

## Goals

- Opted-in materials draw a 16×16 texture tiled in world space, 1 texel = 1 cell.
- Colour is fixed when a cell is created and moves with the cell.
- Solid materials with a crust texture get blob-shaped crust along generated cave edges, seamless
  across chunk borders and deterministic for a seed.
- The generator gains a general way to run passes that need neighbouring-chunk context, so the
  next cross-chunk terrain feature is a new pass, not a generator rewrite.

## Non-goals

- Crust on faces created after generation (digging, explosions, melting). Noita doesn't either.
- Textures on liquids, gases, fire, `acid_rock`, `acid_wood`.
- Texturing the back wall.
- Textures larger than 16×16, or texel scales other than 1.
- Hand-drawn edge decals, `CARDINAL_DIRECTIONS`-style directional edges, `pixel_*` edge colours.

## Anti-goals

- Visible seams or cut-off blobs on the 64-cell chunk grid.
- Any per-frame cost: drawing a chunk must stay one table lookup per cell, as today.
- Growing `Cell` beyond 8 bytes.
- Crust that differs between two runs of the same seed, or pops in after a chunk appears.

## Approaches

1. **Generate each chunk with a margin — chosen.** `generate()` resolves materials for the chunk
   plus a margin on every side, runs passes over that padded buffer, keeps the centre 64×64.
   Neighbours recompute the same margin and agree, so blobs cross borders seamlessly. Costs ~1.7×
   generation time (84² vs 64² cells), all on the background generator threads.
2. Clip blobs at the chunk border. Free, but leaves a seam pattern every 64 cells. Rejected:
   violates an anti-goal.
3. Stamp crust in the streamer once all 8 neighbours exist. No recompute, but the chunk is already
   simulating and drawn, so crust pops in, and it adds ordering to the threaded streamer. Rejected.

## Decided (by the user)

- Approach 1, structured so future cross-chunk features slot in as more passes.
- Crust only on generated terrain (not on faces made later by digging etc.). Applied uniformly:
  sky-facing surfaces and rooms included.
- Crust art is a second db32 texture per material, shown through procedural blob masks.
- 1 texel = 1 cell.
- Texture assignments:

  | Material  | texture         | crust_texture      |
  |-----------|-----------------|--------------------|
  | rock      | stone_generic   | cobblestone        |
  | dirt      | dirt            | gravel             |
  | sandstone | sandstone       | sandstone_bricks   |
  | sand      | sand_ugly       | —                  |
  | grass     | grass_top       | —                  |
  | wood      | oak_planks      | —                  |

- Untextured materials keep today's flat colour + `shade_amount` jitter. Burning tints a textured
  cell orange with today's rule. Crust only on Solid-class materials.

## Design

### Cell encoding (no layout change)

- **Texel.** For a material with a texture, `spawn_cell` sets
  `shade = (wy & 15) * 16 + (wx & 15)`: the shade byte *is* the texel index. Untextured
  materials keep the hash. Everything that already copies `shade` (movement, particles landing)
  carries the texture fragment for free.
- **Crust.** New `FLAG_CRUST = 1 << 2` in `Cell::flags` (bits 2–3 are free today). Moves with the
  cell like `shade`. Cleared naturally by `spawn_cell` on any material change (phase change,
  reaction), so melted crust becomes plain lava.
- **Bodies.** `PhysicsWorld::stamp` calls `spawn_cell` at the world cell each tick, which would make
  a moving body's texture swim. It passes the body-local pixel coords `(px, py)` for the texel
  index instead (builder's call on exact plumbing — e.g. a `spawn_cell` overload taking the texel
  coords separately).

### Materials

- `MaterialDef` gains `std::vector<uint32_t> texture_rgba` and `crust_rgba` (each empty or exactly
  256 RGBA words). `CompiledMaterial` carries them plus `bool textured`.
- `PixelMaterial` gains `texture` and `crust_texture` (`Texture2D`). `to_def()` reads each image,
  requires 16×16 (else `push_error` and treat as untextured), converts to RGBA8, fills the vector.
- The six db32 textures used plus three crust textures are copied into `textures/blocks/` in this
  project (Godot can't load from the other project's tree), imported with filter off.

### Colour table

- `cell_rgba` grows from `[mat][burning][shade]` to `[mat][burning][crust][shade]`
  (`palette.size() * 4 * 256` words). For a textured material, entry =
  `(crust && crust_rgba non-empty ? crust_rgba : texture_rgba)[shade]`, then the burning tint;
  untextured materials ignore the crust bit and use today's formula.
- `write_chunk_image` index becomes
  `((mat * 2 + burning) * 2 + crust) * 256 + shade`. Same cost per cell.
- `cell_color()` (used by material particles) follows the same rule so a falling crust grain
  looks the same in flight as on the ground.

### Padded generation and passes (the extension point)

`TerrainGenerator::generate()` splits into three stages:

1. **Resolve** — the existing per-cell body (biome pick, surface/crust band/pocket/base, caves,
   pools, rooms, light markers) moves into a function that fills a `GenRegion`: a padded grid of
   `MatId` plus a parallel `uint8_t` flags grid and per-cell biome, covering
   `[base - M, base + CHUNK + M)` in both axes. `M` = the largest margin any pass declares.
   Rooms: the padded area can touch up to four room-placement cells, so room lookup moves from
   once-per-chunk to once per placement cell touched (cache the ≤4 lookups).
   Light markers are recorded only for centre cells.
2. **Passes** — an ordered list of generation passes, each a small struct with
   `int margin` and `void apply(GenRegion&, …) const`. A pass may read anywhere in the region and
   may write only cells at least `margin` in from the region edge — this rule is what makes
   neighbours agree. Passes run in list order; with more than one, the region margin is the *sum*
   of margins of the passes that read a predecessor's output near the edge (simplest correct rule:
   `M = sum of all margins`; builder may tighten later).
3. **Emit** — copy the centre 64×64 into the chunk via `spawn_cell`, OR-ing in the region's flags;
   back wall fill unchanged (it gets the centre biome slice it gets today).

Adding a cross-chunk feature later (vines hanging from ceilings, ore veins following walls, moss,
stalactites) = write one pass struct, declare its margin, append it to the list.

### Crust pass (the first pass)

- **Edge cell:** a Solid cell whose material has a crust texture, with at least one 4-neighbour
  of class Empty, Liquid or Gas (cave air, sky, a pool floor all count; Powder does not, so the
  desert's sandstone shelf under its sand gets no continuous band).
- **Uniform (user's decision):** the rule makes no exceptions — the sky-facing world surface and
  hand-authored rooms (and natural walls bordering a room's interior) get crust exactly like cave
  walls. No "authored" or "above surface" marker in the region.
- **Stamp centre:** an edge cell where `hash5(seed, SALT_CRUST, wx, wy) < crust_chance`.
- **Blob:** radius `r = crust_radius * (0.7 + 0.6 * hash)`; a cell at offset (dx, dy) is inside if
  `hypot(dx, dy) + (value_noise(wx/2.5, wy/2.5) - 0.5) * 4 < r`. (This is the mask in the approved
  mockup.)
- **Write:** every Solid cell inside a blob whose material has a crust texture gets `FLAG_CRUST`
  (blobs never colour air, and never paint crust onto a crust-less material they overlap).
- **Margin:** `ceil(max radius) + 1` so a stamp centre one cell outside the margin can't reach in,
  and edge detection at the margin boundary has its neighbour. With defaults that's 10 → 84×84.
- Settings on `TerrainConfig` / `PixelWorldGenerator`: `crust_chance = 0.3`, `crust_radius = 6`.
  World-level, not per material (per-material can come later if wanted).

## Builder's calls (Claude's, open to change)

- `FLAG_CRUST` lives in `flags`, not a material variant, so reactions/physics/density don't fork.
- Edge test counts Empty/Liquid/Gas neighbours as open space; Powder is not open.
- `crust_chance` and `crust_radius` are world settings with the mockup's defaults (0.3, 6).
- `M = sum of pass margins` rather than a tighter dependency-aware rule.
- Bodies sample texture by body-local pixel coords.
- `BoxGenerator` (arena border) gets no passes.

## Left to the builder

- Exact names/shapes of `GenRegion` and the pass struct; whether passes are a `std::vector` of
  structs or a fixed list in `TerrainGenerator`.
- Value-noise implementation for the blob edge (reuse an existing FastNoise instance if one fits).
- Whether to share the resolve stage's biome weights between centre and margin cells.

## Tests

- Core: a textured material's spawned cell has `shade == (wy&15)*16 + (wx&15)`; untextured
  unchanged.
- Core: crust is seamless — generate chunk (0,0) and (1,0) and also a single wide reference
  region; `FLAG_CRUST` on both sides of the border matches the reference.
- Core: crust is deterministic across two generators with the same seed.
- Core: no `FLAG_CRUST` on air, on non-crust materials, or on cells far (> margin) from any edge.
- Core: a world with no textured materials generates byte-identical cells to before.
- Godot: `PixelMaterial.texture` round-trips into `MaterialDef`; a non-16×16 texture errors and
  falls back to flat colour.
- Bench: record generation time before/after (expect ~1.7×) and confirm frame time unchanged.
