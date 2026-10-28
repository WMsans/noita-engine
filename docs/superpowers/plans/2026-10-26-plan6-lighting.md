# Plan 6 — Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dark caves lit by emissive materials, burning cells, `PixelLight` nodes and `@light`
room markers, with soft shadows from the pixel grid, a faintly glowing backdrop, bloom on the
brightest emitters, same-frame response to terrain changes, and 60 fps on the M1.

**Architecture:** Godot-free core builds one RGBA8 texel per lit-area cell each frame
(emission rgb, opacity a) from the grid, then adds light spots (game lights and room markers)
as small discs. `PixelWorld._process` hands that buffer to a `LightRenderer`, which on the
render thread uploads it and runs radiance-cascade and bloom compute passes on Godot's main
`RenderingDevice`. Two canvas items over the lit-area rect apply the result: one multiplies
(`blend_mul`) by `ambient + light`, one adds bloom. A backdrop rect behind the chunks gives the
air something to light.

**Tech Stack:** C++17, godot-cpp 10.0.0-stable, GLSL compute via `RenderingDevice`, doctest,
GDScript, SCons.

**Spec:** `docs/superpowers/specs/2026-10-26-lighting-design.md`; the parent
`docs/superpowers/specs/2026-10-25-noita-engine-design.md` for budgets.

## Global Constraints

- Lighting is purely visual: no readback, no light query API.
- Lighting does nothing when `RenderingServer.get_rendering_device()` is null (`--headless`):
  no input build, no passes, no overlay, no errors. Core unit tests never need a GPU.
- Light reflects the grid of the tick being drawn: inputs are built in `_process`, after
  `_physics_process` ticked, and rendered that frame.
- Only the grid blocks light. Bodies, creatures, particles and sprites are lit, never occlude.
- Emission source: material `emission × emission_energy`; a burning cell takes the per-channel
  max of that and the fire glow; cells in unloaded chunks are opacity 1, emission 0.
- Packing: `EMISSION_RANGE = 4.0`: texel rgb byte = `lround(clamp(e / 4, 0, 1) · 255)`, alpha
  byte = `lround(clamp(light_opacity, 0, 1) · 255)`.
- `LIGHT_CASCADES = 6`, `LIGHT_SNAP = 32` (= 2^(cascades−1)); lit-area origin and size are
  multiples of `LIGHT_SNAP` so probes stay fixed to world cells.
- Light quad `z_index = 4000`, bloom quad `4001`, backdrop `-4096`, all `z_as_relative = false`.
  Anything drawn above 4001 (or on a later `CanvasLayer`) is unlit — that's the opt-out.
- Defaults: `lighting_enabled = true`, `ambient = 0.05`, `light_exposure = 1.0`,
  `light_margin = 240`, `backdrop_color = Color(0.07, 0.05, 0.04)`, `fire_glow_color =
  Color(1, 0.6, 0.2)`, `fire_glow_energy = 1.5`, `marker_light_color = Color(1, 0.75, 0.4)`,
  `marker_light_energy = 1.5`, `marker_light_radius = 48`, `bloom_threshold = 1.0`,
  `bloom_strength = 0.6`. All live.
- Budgets (M1): light inputs ≤ 1 ms CPU mean (build + copy into the upload buffer); light +
  bloom ≤ 3 ms GPU mean; frame p99 ≤ 16.6 ms. M1 is the gate; Linux Iris Xe run is owed.
- `./build.sh --test --verify` passes; `scons tests tsan=yes` stays clean.

## Review Focus

1. **Headless.** A `PixelWorld` with `lighting_enabled = true` under `--headless` runs, reports
   `light_cpu_ms == 0`, `light_gpu_ms == 0`, `light_spots == 0`, and prints no errors. (Test in Task 3.)
2. **Lights at the edge or nonsense values.** A spot half outside the lit area, one wholly
   outside, a radius of 10⁶, or a NaN/inf position never writes out of bounds and never
   crashes; non-finite spots are skipped. (Test in Task 2.)
3. **Resize and zoom.** Resizing the window or changing camera zoom changes the lit-area size;
   GPU resources are recreated and the overlay is hidden for at most one frame, never a
   stretched stale texture. (Check in Task 4's capture script.)
4. **Freeing mid-frame.** Freeing the `PixelWorld` (or changing scene) while a render-thread
   callback is queued never touches freed memory, and exit reports no leaked RIDs. (Check in
   Task 4's capture script.)
5. **Toggling.** `lighting_enabled = false` at runtime hides overlay and backdrop and costs
   nothing; setting it back restores lighting the next frame. (Test in Task 3 for stats;
   check in Task 4 visually.)

---

## File Structure

```
src/core/chunk.h                    # + light_markers
src/core/terrain.cpp                # @light pixels recorded per chunk
src/core/light_input.h / .cpp       # NEW: lit area, palette, input build, spots, markers
src/godot/light_shaders.h           # NEW: GLSL compute + canvas shader source as raw strings
src/godot/light_renderer.h / .cpp   # NEW: RD resources, cascade + bloom passes, GPU timing
src/godot/pixel_light.h / .cpp      # NEW: PixelLight node
src/godot/pixel_world.h / .cpp      # settings, backdrop, overlay, per-frame lighting, stats
src/godot/register_types.cpp, build.sh
tests/core/test_light_input.cpp     # NEW
tests/core/test_rooms.cpp           # marker positions
tests/godot/run_tests.gd            # lighting tests
demo/*.tscn, bench/bench.tscn       # lighting_enabled = false in existing scenes
demo/lighting_demo.tscn / .gd       # NEW
bench/bench_world.gd/.tscn, bench/results/macos-m1-plan6.json, docs/bench-agent.md
```

---

### Task 1: Room light markers in generated chunks

**Files:** Modify `src/core/chunk.h`, `src/core/terrain.cpp`, `tests/core/test_rooms.cpp`, maybe `rooms/shrine.png`

**Interfaces:**
- Produces: `Chunk::light_markers` — `std::vector<uint16_t>`, local index `ly * CHUNK + lx` of
  every `@light` room pixel in the chunk, in generation order. Cleared at the start of
  `TerrainGenerator::generate`. `BoxGenerator` leaves it empty.

**Decided here:** a second sentinel `MAT_LIGHT_MARKER = 0xFFFD` next to `MAT_KEEP` in
`terrain.cpp`; `PaletteKind::MarkerLight` resolves to it; `generate` turns it into air and
records the index. `MarkerBody` stays plain air.

- [ ] **Step 1: Write the failing test.** In `check_cell_stamp`, for the `C_LIGHT` pixel at
  world `(wx, wy)`: the chunk `chunk_of(wx, wy)` in cache `a` and in cache `b` both have
  `light_markers` containing `(wy - cy*CHUNK) * CHUNK + (wx - cx*CHUNK)`; and summed over every
  chunk in `raster`, `light_markers.size()` totals exactly 1.
- [ ] **Step 2:** `scons tests && build/tests/run_tests -tc="*room*"` → the new checks fail.
- [ ] **Step 3:** Implement per "Decided here".
- [ ] **Step 4:** `scons tests && build/tests/run_tests` → all pass.
- [ ] **Step 5:** If none of `rooms/*.png` has a `#ffee66` pixel, add one to `shrine.png` on its
  interior ceiling. `./build.sh --verify` → `test_default_generator_validates` still passes.
- [ ] **Step 6:** Commit: `feat(core): room @light markers recorded per generated chunk`.

### Task 2: Light inputs in the core

**Files:** Create `src/core/light_input.h`, `src/core/light_input.cpp`, `tests/core/test_light_input.cpp`

**Interfaces:**
- Consumes: `WorldGrid::find`, `MaterialTable`, `JobPool::parallel_for`, `Chunk::light_markers`.
- Produces:
  ```cpp
  constexpr float EMISSION_RANGE = 4.0f;
  constexpr int LIGHT_CASCADES = 6;
  constexpr int LIGHT_SNAP = 1 << (LIGHT_CASCADES - 1);
  struct LightTexel { uint8_t r, g, b, a; };          // a = opacity
  struct LightArea { int x0 = 0, y0 = 0, w = 0, h = 0; };  // world cells
  // The visible rect grown by `margin` on every side, then outward to multiples of `snap`.
  LightArea light_area(float vis_x, float vis_y, float vis_w, float vis_h, int margin, int snap);
  struct LightPalette {
      std::vector<LightTexel> mats;  // index = MatId
      LightTexel fire;               // a unused
      static LightPalette build(const MaterialTable& table, float fire_r, float fire_g,
                                float fire_b, float fire_energy);
  };
  // out has area.w * area.h texels, row-major from (x0, y0). pool may be null.
  void build_light_inputs(const WorldGrid& grid, const LightPalette& palette,
                          const LightArea& area, LightTexel* out, JobPool* pool);
  struct LightSpot { float x, y, r, g, b, energy, radius; };
  int spot_source_radius(float radius);  // clamp(lround(radius / 8), 1, 6)
  // Adds colour·energy (packed) to rgb of every texel within the source radius of the spot,
  // saturating at 255; opacity untouched. Clipped to the area; non-finite spots skipped.
  void add_light_spots(const LightArea& area, const std::vector<LightSpot>& spots, LightTexel* out);
  // Appends one spot per marker in every loaded chunk overlapping the area, at the marker
  // cell's centre, with colour/energy/radius from `defaults`.
  void collect_marker_lights(const WorldGrid& grid, const LightArea& area,
                             const LightSpot& defaults, std::vector<LightSpot>& out);
  ```

**Decided here:** rows split across the pool (`parallel_for` over row bands of 16); each row
walks chunk by chunk, so `find` runs once per 64 cells. `radius → source disc` is a
`ponytail:` tuning knob: in 2D radiance cascades a bigger source reaches farther; retuned at
the Task 5 checkpoint.

- [ ] **Step 1: Write the failing tests** (`test_light_input.cpp`; table: air, rock opacity 1,
  lava emission (1, 0.45, 0.1) energy 2 opacity 0.3, wood opacity 1; fire glow (1, 0.6, 0.2) × 1.5):
  - `"light area covers view plus margin, snapped"`: `light_area(10.5, -3.2, 480, 270, 240, 32)`
    → `{-256, -256, 992, 768}`; the same call with the view moved by +0.4 cells → identical.
  - `"materials pack emission and opacity"`: lava texel `{128, 57, 13, 77}`, rock `{0,0,0,255}`,
    air `{0,0,0,0}`.
  - `"burning cells glow with the fire colour"`: burning wood → rgb `{96, 57, 19}`, a = 255.
  - `"unloaded cells are opaque and dark"`: area with no chunks → every texel `{0,0,0,255}`.
  - `"negative coordinates"`: lava painted at (−100, −50) lands at the right texel of an area
    with `x0 = -128, y0 = -64`.
  - `"parallel build matches serial"`: 4-worker pool vs null pool, byte-identical buffers.
  - `"spots add a disc and saturate"`: spot at (40.5, 40.5), radius 16, energy 2 → texel
    (40, 40) rgb rises, (44, 40) unchanged; energy 1000 → 255, not wrapped.
  - `"spots at the edge or nonsense are safe"` (Review Focus 2): half-outside spot writes only
    inside; wholly outside writes nothing; radius 1e6 clamps to source radius 6; NaN or inf
    `x` writes nothing. The buffer is padded with 64 sentinel texels before and after the
    area; every sentinel is unchanged afterwards.
  - `"marker lights come from chunks in the area"`: a chunk with `light_markers = {5*64+3}` →
    one spot at (cx·64 + 3.5, cy·64 + 5.5) with the defaults; an area not overlapping it → none.
- [ ] **Step 2:** `scons tests` → fails to compile (no header). **Step 3:** Implement.
- [ ] **Step 4:** `scons tests && build/tests/run_tests && scons tests tsan=yes` → pass, clean.
- [ ] **Step 5:** Commit: `feat(core): light inputs — lit area, packed emission/opacity, spots, marker lights`.

### Task 3: `PixelLight`, settings, backdrop and the per-frame input build

**Files:** Create `src/godot/pixel_light.h/.cpp`; modify `src/godot/pixel_world.h/.cpp`,
`src/godot/register_types.cpp`, `build.sh` (class list echo), `tests/godot/run_tests.gd`,
`demo/world_demo.tscn`, `demo/paint_demo.tscn`, `demo/bodies_demo.tscn`,
`demo/creatures_demo.tscn`, `bench/bench.tscn`

**Interfaces:**
- Consumes: Task 2's API.
- Produces (GDScript-visible):
  - `PixelLight` (`Node2D`): `color: Color = (1, 0.75, 0.4)`, `energy: float = 1.5`,
    `radius: float = 48`, `flicker: float = 0` (0..1), `enabled: bool = true`. Joins group
    `pixel_light` in `_enter_tree`. `float current_energy(double time) const` — `energy ·
    (1 − flicker · n(t))`, `n` = 1D value noise at 8 Hz, linearly interpolated, seeded from the
    instance id (smooth, not strobing). Not bound.
  - `PixelWorld` properties per Global Constraints defaults; stats `light_cpu_ms`,
    `light_gpu_ms`, `light_spots` (0 when lighting didn't run).
  - For Task 4 (C++ only): `PixelWorld` holds `LightArea light_area_now`,
    `PackedByteArray light_texels` (area.w·area.h·4 bytes, filled this frame) and
    `bool light_ran_this_frame`.

**Decided here:**
- `_process`, after `upload_dirty_chunks`: if `lighting_enabled` and the rendering device
  exists, compute the lit area from the same view rect `count_view_missing` uses, resize
  `light_texels` if needed, `build_light_inputs` (with `pool`), collect `PixelLight`s from the
  group (`enabled`, position via `to_local` of their global position, `current_energy(Time
  ticks / 1000.0)`) plus `collect_marker_lights`, `add_light_spots`, time all of it as
  `light_cpu_ms`. Otherwise zero the stats and hide backdrop/overlay.
- `LightPalette` built in `_ready` next to `palette`, and rebuilt when a fire-glow setting changes.
- Backdrop: one canvas item child of the world, `z_index -4096`, a `backdrop_color` rect
  covering the lit area, redrawn when the area or colour changes; hidden when lighting is off.
- Existing scenes set `lighting_enabled = false` (they place no lights).

- [ ] **Step 1: Write the failing Godot tests:** `test_lighting_settings_defaults` (every
  property reads its default and accepts a write); `test_lighting_headless_noop` (Review Focus
  1: lighting on, a `PixelLight` child, 5 frames → stats keys present and all 0, no error
  output); `test_lighting_toggle_stats` (Review Focus 5: toggle off/on → stats stay 0, no
  errors); `test_pixel_light_free_order` (free the light, then the world, and the reverse — no
  errors); `test_existing_scenes_opt_out` (each scene listed above instantiates with its
  `PixelWorld.lighting_enabled == false`). Add the three stats to `test_stats_keys`.
- [ ] **Step 2:** `./build.sh --verify` → the new tests fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `./build.sh --test --verify` → pass.
- [ ] **Step 5:** Commit: `feat(godot): PixelLight, lighting settings, backdrop, per-frame light inputs`.

### Task 4: GPU light pass, overlay, bloom, lighting demo

**Files:** Create `src/godot/light_shaders.h`, `src/godot/light_renderer.h/.cpp`,
`demo/lighting_demo.tscn/.gd`, `build/capture_lighting.gd` (not committed); modify
`src/godot/pixel_world.h/.cpp`

**Interfaces:**
- Consumes: Task 3's `light_area_now`, `light_texels`, `light_ran_this_frame`, settings.
- Produces:
  ```cpp
  class LightRenderer {  // owned by PixelWorld through std::shared_ptr
  public:
      // Render thread. Compiles the compute shaders (shader_compile_spirv_from_source);
      // false + error leaves lighting off with one push_error.
      bool init(RenderingDevice* rd, String& error);
      // Render thread. (Re)creates textures when area size changes, uploads texels, runs
      // the cascades top-down and the bloom pass, captures GPU timestamps.
      void render(const PackedByteArray& texels, LightArea area, float bloom_threshold);
      RID light_rd() const; RID bloom_rd() const;  // RD textures, for Texture2DRD
      int texture_generation() const;              // bumps on each recreate
      double gpu_ms() const;                       // from the newest frame with results
      void free_all();                             // render thread
  };
  ```

**Decided here:**
- Every RD call happens inside a callable given to `RenderingServer::call_on_render_thread`.
  The callable captures the `shared_ptr<LightRenderer>` and the `PackedByteArray` by value,
  never `this`, so freeing the world can't leave a dangling call (Review Focus 4). World
  `PREDELETE` queues a final `free_all` the same way.
- Cascades: 6; cascade 0 probes every cell, 4 directions pre-averaged, interval [0, 1) cells;
  each level up doubles probe spacing, ×4 directions, ×4 interval length (coverage ≈ 1365
  cells ≥ the 992×768 default area's diagonal). March 1 cell per step:
  `radiance += T · emission·EMISSION_RANGE; T *= 1 − opacity`. Merge from the cascade above
  with bilinear probe interpolation; two ping-pong cascade textures (RGBA16F) plus the light
  output (RGBA16F, area size). Ringing/leak fixes and texture layout: builder's call, from the
  TypeGPU radiance-cascades reference. Zoomed out past coverage, far light is cut off —
  `ponytail:` comment, more cascades if a game zooms out.
- Bloom: emission above `bloom_threshold` (in emission units) downsampled to 1/4 resolution,
  separable Gaussian blur (radius ~8 quarter-res texels), RGBA16F.
- Overlay, both rects = the lit area in world-local coordinates (so UV = light texel, no
  transform math), `Texture2DRD` wrappers made on the main thread and re-pointed when
  `texture_generation` changes (overlay hidden that frame — Review Focus 3):
  - light: `render_mode blend_mul, unshaded; COLOR = vec4(min(vec3(ambient) + texture(TEXTURE, UV).rgb * light_exposure, 1.0), 1.0);`
  - bloom: `render_mode blend_add, unshaded; COLOR = vec4(texture(TEXTURE, UV).rgb * bloom_strength, 1.0);`
  Both linear-filtered; shader code lives in `light_shaders.h`.
- `light_gpu_ms` = light + bloom from `RenderingDevice` timestamps (`capture_timestamp`,
  read the frame after).

- [ ] **Step 1: Demo.** `demo/lighting_demo.tscn/.gd`: arena world (1024×512) filled with rock,
  caves carved with `fill_circle(air)`, a 40-wide lava pool, a wood pillar, a 6-thick rock wall
  between the player start and the lava. Player `PixelCharacter` with a sprite and a
  `PixelLight` child (flicker 0.3); `A`/`D`/`W` as the creatures demo; `E` explode at the
  mouse (radius 12); `F` `fill_circle(fire)` at the mouse; `L` toggles `lighting_enabled`;
  camera follows the player.
- [ ] **Step 2: Capture script** `build/capture_lighting.gd` (a SceneTree script run with a real
  window) loads the demo and, waiting on `RenderingServer.frame_post_draw` each frame, saves
  screenshots and asserts: (a) the torch-lit area around the player is brighter than a
  far unlit area by > 4×; (b) a region behind the wall that sees lava only through the wall
  brightens in the first frame drawn after `explode` opens it (same-frame); (c) setting fire
  to the wood brightens its surroundings; (d) toggling `lighting_enabled` off/on changes the
  frame accordingly (Review Focus 5); (e) resizing the window and setting camera zoom 0.5
  renders without errors and without a stretched frame (Review Focus 3); (f) freeing the
  world then quitting prints no errors and no "RID allocations leaked" (Review Focus 4).
  It fails now (no GPU pass).
- [ ] **Step 3:** Implement `LightRenderer`, the shaders and the overlay; wire them into
  `_process` after Task 3's input build.
- [ ] **Step 4:** `godot --path . -s build/capture_lighting.gd` → all assertions pass; look at
  the screenshots: warm light fills the cave around lava and torch, air glows faintly against
  the backdrop, rock casts shadows, bloom halos lava. `./build.sh --test --verify` still passes.
- [ ] **Step 5:** Commit: `feat(godot): radiance-cascade light pass, overlay and bloom, lighting demo`.

### Task 5: Benchmark, tuning and the M1 checkpoint

**Files:** Modify `bench/bench_world.gd`, `bench/bench_world.tscn`, `docs/bench-agent.md`,
maybe `materials/*.tres`; create `bench/results/macos-m1-plan6.json`

- [ ] **Step 1: Workload.** `bench_world.tscn` sets `lighting_enabled = true`. Every 60 frames
  paint a 40×6 lava pool at camera offset (−200, 60); a `PixelLight` (radius 48, flicker 0.3)
  is a child of the camera. Samples and keys: `light_cpu_ms_mean/p99`,
  `light_gpu_ms_mean/p99`, `light_spots_mean`. Gate adds: `light_cpu_ms_mean ≤ 1.0`,
  `light_gpu_ms_mean ≤ 3.0`, `light_gpu_ms_mean > 0` (lighting really ran),
  `light_spots_mean ≥ 1`, on top of Plan 5's gate.
- [ ] **Step 2: Tune by eye** in `lighting_demo`: a torch at default radius lights a cave room
  about 40 cells across; the lava pool lights walls out to about a quarter screen (≈120
  cells). Knobs, in order: `spot_source_radius` mapping, `light_exposure` default, emission
  values in `materials/*.tres`. Record what changed in the commit message.
- [ ] **Step 3:** `./build.sh --release --test --verify`; run the bench to
  `bench/results/macos-m1-plan6.json` → `budget_pass: true`. If it fails, stop and report the
  numbers to the user: the spec's fallbacks (half resolution, per-dirty-chunk inputs, then a
  GPU-side input pass) each need the user's go-ahead.
- [ ] **Step 4:** `docs/bench-agent.md`: Plan 6 replaces Plan 5 as the owed Linux run; new
  keys, gate and numbers; lighting needs a real window.
- [ ] **Step 5:** Commit: `bench: lighting in bench_world, macOS M1 Plan 6 results`.
- [ ] **Step 6:** Linux checkpoint: push and hand off — needs the user's go-ahead.
