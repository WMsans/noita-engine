# Light Reach and Back Wall — Design

Date: 2026-11-01
Status: approved
Parent specs: `docs/superpowers/specs/2026-10-26-lighting-design.md` and
`docs/superpowers/specs/2026-10-26-wall-surface-light-design.md`. This changes their light model
(reach, surface depth, ambient, bloom) and replaces the flat `backdrop_color` with a generated
back wall under the ground. Where they disagree with this doc, this one wins.

## Intent

With lighting on, a lava pool shows as the lava itself plus a big yellow-orange blur; the ground
around it is black (user's screenshot of `world_demo`). The user wants light to light up the
environment the way Noita does: a fire or lava pool lights the terrain around it deeply and the
cave wall behind it, the light stays local, and the rest of the cave is dim but readable (user's
Noita reference screenshots).

**Why it looks like that today** (found in the code):

- **Bloom covers the scene.** Lava's emission energy is 4 and `bloom_threshold` is 1, so a wide
  blur sits over the pool and everything near it.
- **Solid ground only gets a thin rim.** `light_surface_depth` is 10 cells; rays starting inside
  rock are blocked by the next cell, so below the rim solids sit at `ambient`.
- **Light has no reach.** Radiance cascades give a slow, never-ending falloff, so a large lava
  pool lights a whole chamber up to its ceiling.
- **Unlit is black.** `ambient` defaults to 0.05.
- **Nothing behind the caves catches light.** Air shows one flat `backdrop_color` (near black), so
  lit air shows almost nothing.

**Success:** in `world_demo` with lighting on (the working tree already turns it on), a lava pool
at the bottom of a cave lights the ground under and around it dozens of cells deep and the back
wall near it, its light dies out well before the cave ceiling, unlit rock and back wall are dim
blue-grey shapes you can read, and the back wall shows hollows like the Noita references. The M1
budgets from the parent spec still hold.

**Visual targets** (in `docs/superpowers/specs/assets/2026-11-01-light-reach/`):
`today-lava.png` (the problem), `ref-noita-lit-cave.png` (lighting target),
`ref-backwall-blue.png` and `ref-backwall-grey.png` (back wall target), and `mockup-v2.html`
(the browser mockup the user agreed to). The mockup shows the layers and the reach/depth
behaviour only; where its colours or maths differ from this doc (one fixed wall colour, an
additive ambient, near-pure-black hollows, a spread that doesn't reach zero), this doc wins
(my call).

## Goals

- **Readable darkness (user).** Places no light reaches are dim, not black. My call: the ambient
  floor gets a cool blue-grey tint so warm light stands out.
- **Light reaches deep into terrain (user: "dozens of pixels").** About 40 cells by default, with a
  smooth fade (user agreed to the v2 mockup at ~40).
- **Every light has a finite reach (user).** Light fades to zero at a set distance instead of
  trailing off forever. A lava pool on the floor lights its surroundings, not the ceiling.
  Reach is set per source kind (user agreed): one value for emissive materials and burning cells,
  `PixelLight.radius` for each placed light, `marker_light_radius` for `@light` markers.
- **A generated back wall under the ground (user).** Behind every air cell below the surface:
  - a darker, desaturated version of the local biome's rock colour (my call, so it matches every
    biome with no new art);
  - **procedural hollows (user)**: near-black pockets cut into it, shaped like the Noita
    references: wide and horizontal, with jagged stalactite teeth hanging from their top edges
    and shorter stalagmites rising from their bottom edges;
  - surface detail on the wall: crack lines, and pebble clusters along hollow edges (from the
    references);
  - lit by the same light as everything else, and dimly visible under ambient.
- **No back wall above the surface (user).** Open sky stays dark air (`backdrop_color`).
- **Bloom no longer hides the scene (my call).** Only the brightest pixels glow, with a small halo.

## Non-goals

- Bounce / indirect light (still out, as in the parent spec).
- Parallax on the back wall. It is fixed to world cells and scrolls 1:1 with terrain (my call:
  the light texture is in world cells, so a parallax wall would get light in the wrong place).
- The back wall blocking light or casting shadows. Only terrain cells block light.
- Hand-drawn back-wall art. Procedural only for now; per-biome textures could replace it later.
- Back wall reacting to the simulation (it isn't burned, dug or stained).
- Per-material reach or per-material surface depth.
- Fog of war / explored-area memory (still out, as in the wall-surface spec).

## Anti-goals

- Light leaking through walls into the open air behind them, or open-air shadows going soft.
  Deep terrain light spreads only into solids, from lit open cells, never back out into air.
  (A solid thinner than the surface depth does glow on its far face; the user accepted that,
  see Deep terrain light.)
- A back wall that looks like solid terrain. It must read clearly as background: darker and
  lower-contrast than the terrain in front of it, under any light.
- Missing the parent spec's budgets (light + bloom ≤ 3 ms GPU mean on the M1 in `bench_world`,
  frame p99 ≤ 16.6 ms, light inputs ≤ 1 ms CPU mean).
- Seams in the back wall at chunk edges, or a back wall that changes when a chunk is unloaded and
  regenerated.

## Constraints

- Everything in the parent lighting spec's Constraints still applies (Godot 4.7.2, Forward+,
  compute on the main `RenderingDevice`, 480×270 render, M1 as the gate, Mac checkpoint).
- Chunks are 64×64 cells. Chunk textures are rebuilt from cells whenever `render_dirty` is set,
  which is often (anything moving). Air cells draw transparent today.
- Generation is deterministic from the seed (`TerrainGenerator`, FastNoiseLite fields, `hash5`
  salts). The back wall must be too.
- `lighting_demo.tscn` sets `ambient = 0.4`; scenes that set lighting properties must keep
  loading.

## Approaches

Both picked by the user in conversation.

**Light reach**

- **A. Reach inside the existing radiance-cascade pass (chosen).** Each input texel carries a
  reach alongside its emission. As a cascade ray marches, a sample of emission at distance `t`
  is weighted by a falloff that reaches zero at that emitter's reach. One system, fixed cost,
  soft shadows unchanged. Bonus: with every reach capped, the coarse cascades that span the
  whole lit area add nothing, so fewer cascades run, which pays for the deeper surface pass.
  Trade-off: more input data per texel.
- **B. Two systems (rejected).** Emissive cells keep cascades with one shared reach; each
  `PixelLight` gets its own shadow-ray pass. Exact per-light reach, but two lighting systems to
  maintain, and cost grows with light count.

**Back wall**

- **A. Built by the world generator per chunk (chosen).** When a chunk is generated, the
  generator also computes its back-wall colours with the same C++ noise and biome lookups the
  terrain uses. It's deterministic, unit-testable and seam-free. Trade-off: memory per chunk and
  some generation time.
- **B. A GPU shader every frame (rejected).** No memory, but the biome and noise logic would be
  duplicated in shader code and must stay in sync with the C++ generator.

## Design

### Light model changes

- **Reach.** Every emitter has a reach in cells:
  - emissive materials and burning cells: one `PixelWorld` setting, `emission_reach` (my call:
    default about 40 cells, tuned by eye so a lava pool lights roughly a quarter screen around
    it, not the ceiling of a tall cavern);
  - `PixelLight`: its existing `radius` now means reach (default 48);
  - `@light` markers: the existing `marker_light_radius`.
- **Falloff.** Light from an emitter fades smoothly to exactly zero at its reach (the mockup used
  `(1 - d/reach)²`). The curve is the builder's call, tuned by eye; it must hit zero at the reach,
  not just get small.
- **Carrying reach to the GPU** (texture layout, how mip reduction combines mixed reaches, and
  whether `PixelLight` spots keep being stamped into the input texture) is the builder's call.
  Constraint: the CPU input build stays within the 1 ms budget.
- **Cascade count follows the largest reach.** A level whose interval starts beyond every reach
  in use contributes nothing and is skipped, or the count is capped by a max reach. The builder
  picks how (dynamic per frame, or a fixed cap with reaches clamped to it), and documents the
  largest supported reach.
- **Deep terrain light.** `light_surface_depth` default goes from 10 to about 40 cells, and its
  max (`LIGHT_SURFACE_MAX_DEPTH`, now 32) rises to at least 64. The rule from the wall-surface
  spec stays: only solids receive spread light, it only flows from lit open cells into solids,
  and it fades smoothly to nothing at the depth. The spread is **on top of** each light's reach and doesn't know
  which way the light came from (user picked this cheaper version): any solid thinner than the
  depth glows on its far face too, and light can climb that far up through a cave's side walls.
  The open air behind a thin wall still stays dark. Ten-plus ping-pong 3×3 passes at full resolution
  probably won't fit the budget at 40+; the method (fewer wider passes, a distance-transform /
  jump-flood approach, or spreading at reduced resolution) is the builder's call, inside the GPU
  budget.
- **Ambient.** `ambient` stays a float (brightness, so existing scenes still load). A new
  `ambient_tint` Color multiplies it (my call: default a cool blue-grey). Defaults are tuned by
  eye so unlit terrain and back wall read as dim shapes, roughly the brightness of the Noita
  reference's unlit areas. The mockup used about (0.16, 0.18, 0.26) as the effective floor.
- **Bloom.** Retune `bloom_threshold` / `bloom_strength` defaults (and lava/fire emission energy
  in `materials/*.tres` if needed) so bloom is a small halo on the brightest emitters and never
  hides the terrain next to them. Tuned by eye in `world_demo` against the user's first
  screenshot.

### Back wall

**Where it is.** Behind every cell at or below the local surface (`surface_height(wx)`), across
the whole underground, including behind rock. It shows only where the terrain cell in front is
air (or see-through), so caves show it, and digging or blasting a hole shows it too (my call).
Above the surface there is no back wall; the flat `backdrop_color` stays there.
The cut-off follows the generator's original surface line, so a hole dug down from the surface
shows sky down to that line and wall below it. Surface rooms (placed above `surface_height`) get
no wall. To avoid a ruler-straight edge at entrances, the wall's top edge is roughened by a few
cells of deterministic noise (my call; amount is the builder's).

**What it looks like** (the Noita references are the target):

- **Base colour.** The biome's `base_material` colour (chosen with the same dithered `biome_at`
  pick as the terrain, so biome borders blend the same way), darkened and desaturated so it is
  clearly behind the terrain. Amount is a setting with a sensible default (builder's call), tuned
  by eye.
- **Hollows.** Near-black pockets (a much darker version of the base colour, never pure black).
  Shape: a horizontally stretched noise field thresholded into wide pockets, then toothed edges:
  stalactites hang from the top edge of a hollow (longer) and stalagmites rise from its bottom
  edge (shorter). Teeth are triangular, a few cells wide, varied length. Hollow amount, size and
  stretch, and tooth lengths are generator settings with defaults tuned to look like the
  references.
- **Detail.** Thin darker crack lines across the wall (e.g. ridged noise) and lighter pebble
  clusters along hollow edges. A small per-cell shade variation like terrain cells have. The
  exact techniques are the builder's call.
- **Never confusable with terrain** (anti-goal): lower brightness and contrast than terrain at the
  same light.

**How it is made and drawn.**

- Generated per chunk by `TerrainGenerator`, deterministically from the seed (new noise fields
  with their own salts), at the same time as the chunk's cells. A regenerated chunk gets the same
  wall.
- Drawn behind the chunk's cells, in the same world layer, so the light overlay lights it like
  everything else. It never changes after generation, so it is uploaded once per chunk, not on
  every `render_dirty`.
- Whether the wall is its own per-chunk texture under the cell texture or composited into the cell
  texture's air pixels, and whether the CPU keeps a copy after upload, is the builder's call.
  Constraint: see-through cells (gases, liquids with alpha < 1) must show the wall behind them
  correctly, and chunk upload time in `bench_world` must not regress noticeably.
- It has no effect on simulation, physics or light inputs (only terrain cells block light).
- Settings live on `PixelWorldGenerator` (generation) and `PixelWorld` (an on/off switch, so the
  existing flat backdrop is still available). Names are the builder's call.
- With lighting off, the back wall still draws (it's part of the world's look), unlit.

### Demos and benchmark

- `world_demo` with lighting on is the scene the user judges by. The working tree's change to
  `demo/world_demo.tscn` (lighting on) is kept.
- `lighting_demo` keeps working with the flat backdrop: its arena is painted on an empty world
  with no generator, so it gets no back wall (my call). Its overrides (`ambient = 0.4`,
  `light_surface_depth`, `bloom_threshold`) are retuned with the new defaults. The back-wall
  checks below apply to `world_demo` only.
- `bench_world` keeps its lava pool and moving `PixelLight`, now with the back wall on and the new
  surface depth, and must meet the parent spec's budgets. The Mac checkpoint is owed as in earlier
  plans.

## Testing

- **Pure C++ unit tests:** back wall generation is deterministic (same seed and chunk, same
  colours), has no seams across chunk edges, is absent above the surface, and has hollows covering
  a sane share of the area for the default settings. Reach packing in the light inputs (materials,
  burning cells, spots, markers) if it lives in `light_input`.
- **By eye** in `world_demo` and `lighting_demo`: lava on a cave floor lights ground under it
  ~40 cells deep and does not reach a far ceiling; the open air behind `lighting_demo`'s 6-cell wall stays
  in hard shadow (the wall itself may glow through); unlit rock and wall are dim but readable (`world_demo`); the back wall's hollows look like the
  references; bloom doesn't hide terrain.
- **Benchmark:** `bench_world` budgets as above.

## Left to the builder

- Reach encoding, falloff curve, mip handling of mixed reaches, cascade count / max reach.
- The deep-surface-light method within budget.
- Back-wall noise techniques, settings names and defaults, darkening amounts, and how it is drawn
  (separate texture vs composited).
- Exact default values for ambient, tint, bloom and reach, tuned by eye.
