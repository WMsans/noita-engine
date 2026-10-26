# Lighting — Design

Date: 2026-10-26
Status: approved
Parent spec: `docs/superpowers/specs/2026-10-25-noita-engine-design.md` (its "Lighting"
section, the `PixelLight` line of the game-facing API and the lighting performance budget).
This doc replaces those parts. Where the two disagree, this one wins. Roadmap: this is
stage 6, the last one in the particles/bodies/creatures spec's renumbering.

## Intent

Darkness is a gameplay element. A cave is dark until something lights it. A pool of lava
or a player's torch gives off a warm glow that fills the open cave around it and lights the
walls it can reach. Rock between you and the light casts a real shadow (user).

Lighting is purely visual. Game code never asks how lit a spot is (parent spec, user).

**Success:** walking a creature with a torch through a dark generated cave past a lava
pool and burning wood looks like the picture above, light changes the same frame a wall is
blown open, and the benchmark still holds 60 fps on the M1 with lighting on.

## What changed from the parent spec (and why)

- **Everything in the world layer is lit, not just terrain** (user). Bodies, creatures,
  particles and game code's own sprites get darker in shadow.
- **Only the pixel grid blocks light** (user). Bodies and creatures don't cast shadows.
  This also means it doesn't matter that bodies are lifted out of the grid after each tick
  and creatures are never in it.
- **Light sources are narrowed** (user): emissive materials, burning cells, `PixelLight`
  nodes and `@light` room markers. Hot-but-not-burning cells and particles don't glow.
- **No bounce light** (my call, accepted by the user). Radiance cascades lights everything
  with a line of sight to a source, with soft shadows, but light doesn't reflect off walls
  around corners. It can be added later by feeding last frame's light back as emission.

## Goals

- Emissive materials (lava, fire) light the world in their emission colour and energy.
- Burning cells of any material glow with one fixed fire colour while burning (user: burning
  wood). The cell's burning flag already exists; no new material data.
- A `PixelLight` node game code can place or parent to a creature (the torch): colour,
  radius, energy, optional flicker.
- `@light` room markers produce lights in generated rooms.
- Solid pixels block light according to their material's `light_opacity`; air, gas and thin
  liquids let it through as their opacity says.
- Lit areas show the world's colours; unlit areas fall to a low ambient level.
- A bloom halo around the brightest emitters.
- Light reflects terrain from the same tick that's drawn — never a frame behind.

## Non-goals

- Bounce / indirect light (see above).
- Shadows cast by bodies, creatures, particles or sprites.
- Glowing particles (sparks from `explode` stay unlit) and temperature glow on hot cells.
- Sunlight / sky light. The world stays night, as Plan 2 decided.
- Normal maps or any per-sprite lighting.
- CPU readback or light queries from game code.
- The `@body` marker. It stays reserved and generates air.

## Anti-goals

- Light lagging the terrain: a wall dug, burned or blown open changes the light in the same
  frame (parent spec).
- Missing 60 fps on the M1 or the Iris Xe in the benchmark with lighting on (parent spec).
- Lighting that only works in a demo: game code must be able to place lights and turn the
  knobs below without touching C++.
- Light that "swims" or flickers as the camera scrolls (cascade probes must stay fixed to
  world cells, not to screen pixels — see Design).

## Constraints

- Godot 4.7.2, Forward+ renderer, Vulkan on Linux and Metal on macOS. Compute runs on
  Godot's main `RenderingDevice` (`RenderingServer.get_rendering_device()`), on the render
  thread, and the result reaches the canvas as a `Texture2DRD`.
- The project renders at 480×270 (`window/stretch/mode="viewport"`) and upscales with
  nearest filtering, so one world cell is one rendered pixel at default zoom.
- Budget on the M1: lighting plus world rendering ≤ 4 ms GPU; the CPU side of lighting
  (building its inputs) ≤ 1 ms, since the CPU is the binding constraint there.
- Needs a real window. Lighting does nothing under `--headless`, and the core unit tests
  don't depend on it.
- Two machines: the Mac agent builds and benchmarks; the plan needs a Mac checkpoint, as
  earlier plans had. The M1 is the gate. The Iris Xe run is owed, as Plans 1 and 5's Linux
  runs are (my call, following how earlier plans were handled).
- Headroom is thin: the Plan 5 M1 run has frame p99 at 15.13 ms against 16.6 ms, so
  lighting has about 1.5 ms of frame time to spend.

## Approaches considered

- **Radiance cascades in compute shaders (chosen by the user).** The CPU writes emission
  and opacity for the lit area; a few compute passes on the `RenderingDevice` produce a
  light texture; one full-screen pass multiplies the world by it and adds bloom. Cost is
  fixed by resolution, not by how many things glow, so a cave full of burning wood costs
  the same as an empty one, and it sees terrain changes the same frame. Trade-off: the most
  new code (compute pipelines, cascade merging) and the hardest to debug.
- **Radiance cascades as canvas shaders in SubViewports (rejected).** Same light model with
  no compute code, easier to inspect in the editor, but one viewport per cascade, slower,
  and Godot decides when the passes run, which makes "same frame as the terrain" hard to
  guarantee.
- **Distance-field ray marching (rejected).** Simpler, but grainy unless it blends over
  several frames, so light would trail behind a wall that was just blown open (anti-goal).
- **Godot's 2D lights with occluders, and CPU flood fill (rejected),** for the parent spec's
  reasons: occluder polygons for all terrain every frame, and CPU cost on the M1.

## Design

### The lit area

- Light is computed over the camera's visible rect **plus a margin** on every side (default
  half a screen, so about 960×540 cells at default zoom), so lava just off-screen lights the
  screen edge (user agreed the default).
- The lit area's origin is **snapped to whole cells, and to the cascade's coarsest probe
  spacing**, so probes stay fixed to the world as the camera scrolls and light doesn't
  shimmer. The full-screen pass maps screen pixels to lit-area texels with the canvas
  transform, so sub-cell camera motion still lines up.
- Full world-cell resolution (user agreed the default). The resolution scale is a setting
  (1 or 1/2) in case the Iris Xe needs it; that's the builder's call to expose or not.
- Cells outside any loaded chunk count as opaque and non-emissive (like the simulator,
  which treats missing chunks as solid).

### Inputs (CPU, each frame, after the tick)

- `PixelWorld._process` already runs after the frame's tick(s). After `upload_dirty_chunks`,
  it builds two lit-area-sized buffers from the grid:
  - **emission** (RGB, HDR-capable, e.g. RGBA16F or RGB scaled into RGBA8 with energy) — a
    cell's material emission colour × energy, or the fixed fire glow if the cell is burning
    (whichever is brighter);
  - **opacity** (one channel) — the material's `light_opacity`; air is 0.
- Material emission and opacity are resolved into a per-material table at `_ready`, next to
  the existing render palette, so the loop is a table lookup per cell.
- The fire glow colour and energy are one `PixelWorld` setting (default warm orange-yellow,
  close to the `fire` material's emission) (my call: one knob until a material needs its
  own burn colour).
- The 1 ms CPU budget **includes** getting the inputs to the GPU (my call: on unified
  memory the copy is CPU time). So inputs should be compact — e.g. emission in RGBA8 with a
  shared energy scale, opacity in one byte — rather than RGBA16F.
- My call: rebuild the whole lit area every frame, split across the existing `JobPool`
  workers by rows. About 500k cells of table lookups fits the 1 ms CPU budget, and it makes
  camera scrolling free. Only switch to per-dirty-chunk updates if the benchmark says so.
  How the buffer reaches the GPU (one `texture_update` per frame, or a storage buffer the
  first compute pass reads) is the builder's call.

### Game-placed lights

- **`PixelLight` node** (a `Node2D`): `color`, `energy`, `radius` (cells), `flicker`
  (0 = steady; higher = more random dimming, smoothed so it reads as a flame, not strobing),
  `enabled`. It registers with the `PixelWorld` it finds (the `pixel_world` group, as bodies
  do) and is read each frame at its current global position. Parenting it to a creature is
  how the torch works.
- Lights are added into the emission input as a small soft disc at their position (size
  scaling with `radius`), before the cascades run, so they cast soft shadows exactly like
  emissive pixels do. `radius` controls how far the light reaches: the light's emission
  is scaled so it falls to roughly nothing at `radius`. The exact falloff model is the
  builder's call, within "a torch at the default radius lights a small cave room".
  Whether discs are drawn on the CPU into the emission buffer or by a compute pass from a
  light list is the builder's call.
- **`@light` room markers.** Each marker pixel in a room image becomes one light at that
  cell when its chunk is generated, using a default colour/energy/radius set on
  `PixelWorld` (warm torch-like). The generator records marker positions per chunk as part
  of its deterministic output (still stamping air there); they live and die with the chunk
  and are not nodes. Lights from markers are only placed by generation — a regenerated chunk
  gets them back.

### Light pass (GPU compute, radiance cascades)

- Port of 2D radiance cascades (Sannikov). Cascade 0 has a probe per cell (or per 2×2 —
  builder's call) with short rays; each cascade up doubles probe spacing and quadruples
  ray count and ray length, until rays span the lit area. Each cascade ray-marches through
  the opacity texture, accumulating emission and attenuating by opacity; cascades merge
  top-down; cascade 0 integrates into the light texture.
- Opacity is used as an absorption per cell, so `light_opacity` 1.0 (rock) stops light
  in a cell or two and 0.05 (fire) barely dims it.
- Pass structure, ray marching method (plain march vs a distance-field skip), number of
  cascades, and fixes for known radiance-cascade artifacts (ringing, light leaking through
  thin walls) are the builder's call, within the budget and anti-goals. Reference: the
  TypeGPU radiance-cascades package, as the parent spec named.
- Runs every frame from `PixelWorld._process`, scheduled on the render thread with
  `RenderingServer.call_on_render_thread`, so it consumes this frame's inputs.

### Applying the light

- One full-screen canvas item, drawn **after the whole world layer and before UI**,
  multiplies the framebuffer by `ambient + light` (my call: `render_mode blend_mul`, which
  needs no screen-texture copy). Everything already drawn in that canvas layer — chunk
  textures, particles, bodies, creatures' sprites, game sprites — is lit, with no opt-in
  (user). UI on its own `CanvasLayer` above stays full brightness.
- Something meant to glow at full brightness (a magic bolt sprite) opts out by drawing
  after the light pass: a higher `z_index` or a later `CanvasLayer`. The builder picks which
  of those the light pass uses (high fixed `z_index` on the world's layer, or its own
  `CanvasLayer`) and documents it.
- Lit areas show the world's own colours; the multiply clamps at 1 (no over-brightening
  of albedo) — the "hot" look comes from bloom. (My call; HDR 2D isn't needed.)
- **Air has a backdrop.** Air cells draw transparent and there are no background walls, so
  today whatever the viewport clear colour is shows through. The world draws a solid
  `backdrop_color` behind the chunks (default a very dark warm grey), which is lit
  like everything else. A torch or lava pool then tints the open air around it, which is
  what reads as glow filling the cave (user: air should glow faintly). No extra shader term.
- **Ambient:** a `PixelWorld` setting, default 0.05 — dark enough that you need a light, but
  rock outlines are faintly there (user agreed the default).
- **Bloom:** emitted light above a threshold is blurred (a few downsampled blur passes or
  a single wide blur) and added on top with `blend_add`. Only the brightest emitters bloom
  (user agreed). Threshold and strength are settings; implementation is the builder's call.
- A `lighting_enabled` setting turns the whole thing off (no buffers built, no passes, no
  overlay) — for headless runs, debugging and A/B in the benchmark. **Default on**, since
  darkness is the default experience. The existing demos (`world_demo`, `paint_demo`,
  `bodies_demo`, `creatures_demo`) place no lights, so they set it off in their scenes
  (my call).
- **Brightness:** a global `light_exposure` gain, and the plan may retune the emission
  values in `materials/*.tres` (my call). Tuned by eye at the Mac checkpoint against: a
  torch at default radius lights a small cave room of about 40 cells; a lava pool lights
  the walls around it out to roughly a quarter screen.

### Settings on `PixelWorld`

`lighting_enabled`, `ambient`, `backdrop_color`, `light_exposure`, `light_margin`, `fire_glow_color`, `fire_glow_energy`,
`marker_light_color`, `marker_light_energy`, `marker_light_radius`, `bloom_threshold`,
`bloom_strength`. All live (changeable while running). Exact names are the builder's call.

### Benchmark and stats

- `get_stats()` gains the CPU time to build the light inputs and the GPU time of the light
  and bloom passes (read back a frame late from `RenderingDevice` timestamps).
- `bench_world` runs with lighting on, adds a lava pool and a moving `PixelLight` near the
  camera, and gets budgets: lighting inputs ≤ 1 ms CPU mean; light + bloom GPU ≤ 3 ms mean
  (leaving the rest of the 4 ms for world drawing); frame p99 still ≤ 16.6 ms.
- If frame p99 fails while each lighting budget passes on its own, the fallback order is:
  half-resolution light first, then per-dirty-chunk inputs. The bench workload is not
  trimmed to make room (my call).
- **Last fallback, if half resolution still misses the budget (user):** move input building
  onto the GPU. A pass on the `RenderingDevice`, run on the render thread before the frame
  draws, builds the emission and opacity textures from per-chunk data the GPU already has
  (uploaded only when a chunk is dirty, as chunk colours are today) and a small GPU table
  of per-material emission and opacity. That removes the per-frame CPU build and most of
  the copy. Burning cells and `PixelLight` discs still have to reach that pass. Not built
  unless the benchmark needs it.
- A lighting demo scene: a dark generated cave, a creature carrying a `PixelLight` torch,
  a lava pool, wood that can be set on fire, and a key to blow holes in a wall so the
  same-frame update is visible.

### Testing

- Pure-C++ unit tests for what has no GPU in it: building the emission/opacity buffers from
  cells (materials, burning cells, missing chunks, lit-area snapping) and marker lights
  coming out of generation deterministically.
- The GPU passes are checked by the demo and the benchmark, not unit tests.

## Left to the builder

- Cascade parameters, ray marching method, artifact fixes, and texture formats.
- How inputs and lights reach the GPU (texture uploads vs storage buffers; CPU vs GPU light
  discs).
- Light falloff model behind `radius`, and the flicker curve.
- Bloom implementation.
- Which mechanism places the light pass after the world (z_index vs CanvasLayer).
- Names of settings, stats and the demo scene.
