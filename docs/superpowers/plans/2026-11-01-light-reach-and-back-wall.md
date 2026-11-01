# Light Reach and Back Wall Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lights fade to zero at a per-source reach, light goes ~40 cells into terrain, unlit areas
are a dim cool blue-grey, bloom is a small halo, and a generated back wall with toothed hollows
sits behind the underground.

**Architecture:** A second input plane carries a per-texel reach code next to the RGBA8
emission/opacity texels; the cascade march weights each emission sample by a falloff that hits
zero at that reach, and only as many cascades run as the largest reach needs. The surface spread
becomes a two-pass separable max filter. The back wall is a per-chunk RGBA8 image computed by a
new core `BackWall` inside `TerrainGenerator::generate`, stored on the `Chunk`, and drawn under the
chunk's cell texture in the same canvas item.

**Tech Stack:** C++17 core (doctest, `scons tests`), godot-cpp GDExtension, GLSL 450 compute on
Godot's `RenderingDevice`, GDScript tests (`./build.sh --verify`), FastNoiseLite.

**Spec:** `docs/superpowers/specs/2026-11-01-light-reach-and-back-wall-design.md` (visual targets
in `docs/superpowers/specs/assets/2026-11-01-light-reach/`).

## Global Constraints

- Godot 4.7.2, Forward+; compute on `RenderingServer.get_rendering_device()`, render thread only.
- M1 budgets (`bench_world`): light + bloom ≤ 3 ms GPU mean; light inputs ≤ 1 ms CPU mean; frame p99 ≤ 16.6 ms. The Mac run is owed; record Linux numbers here.
- Only terrain cells block light; the back wall never does and never touches simulation, physics or light inputs.
- Back wall: none above the generator's surface line; fixed to world cells (no parallax); deterministic from the seed; no seams at chunk edges.
- Deep light spreads only into solids (input opacity ≥ 0.5) from lit open cells, never back into air; far faces of thin solids may glow (user accepted).
- `ambient` stays a float property; existing scenes must load unchanged.
- Light falloff reaches exactly zero at the reach.
- Lighting does nothing under `--headless`; GPU work is checked with a windowed `--write-movie` capture.

## Review Focus

1. `light_resolution` 2: reach and surface depth must cover the same distance in cells (convert to texels by dividing by `area.scale`). Test in Task 1 (`spot reach at scale 2`) and checked by eye in Task 4.
2. `PixelLight.radius` 0, negative or huge (> 1020): clamp to reach codes 1..255, never a zero-reach spot that writes emission which then never shows, never overflow. Test in Task 1 (`spot reach clamps`).
3. Chunks at negative coordinates and across chunk edges: back wall identical to the per-cell reference (no seams, no `floor_div` mistakes). Test in Task 5 (`back wall chunk matches per-cell reference`).
4. A chunk unloaded and regenerated: same back wall bytes, and the recreated view draws the wall under the cells again. Test in Task 5 (`back wall deterministic`) and Task 6 (Godot test).
5. Lighting off (`lighting_enabled = false`): back wall still draws, unlit; `back_wall_enabled = false` removes it. Godot test in Task 6.

---

### Task 1: Reach in the light inputs (core)

**Files:**
- Modify: `src/core/light_input.h`, `src/core/light_input.cpp`
- Test: `tests/core/test_light_input.cpp`

**Interfaces:**
- Produces:
  - `constexpr float LIGHT_REACH_UNIT = 4.0f;` cells per reach code; codes 1..255, so reach ≤ 1020 cells; 0 = not an emitter.
  - `uint8_t pack_reach(float reach_cells);` → `clamp(ceil(reach / UNIT), 1, 255)`; non-finite or ≤ 0 → 1.
  - `LightPalette` gains `std::vector<uint8_t> reach;` (index = MatId; `pack_reach(emission_reach)` for materials with any non-zero packed emission, else 0) and `uint8_t fire_reach;`. `LightPalette::build(table, fire_r, fire_g, fire_b, fire_energy, float emission_reach)`.
  - Every builder takes a reach plane alongside the texels, same layout (one byte per texel, row-major): `build_light_inputs(grid, palette, area, LightTexel* out, uint8_t* reach_out, JobPool*)`, same extra `uint8_t* reach_out` after `out` on `build_light_region`, `build_light_rects`; `add_light_spots(area, spots, LightTexel* out, uint8_t* reach_out)`; `restore_light_rects(area, rects, const LightTexel* base, const uint8_t* reach_base, LightTexel* out, uint8_t* reach_out)`.
  - Reach rules: a cell's reach is its material's (burning → max with `fire_reach`); a scale>1 block takes the max over its cells; unloaded cells 0; a spot writes `max(existing, pack_reach(radius))` on every texel its disc touches.
  - `int light_cascade_count(float max_reach_texels);` number of levels L in [0, LIGHT_CASCADES) whose interval start `(4^L − 1) / 3` is < max_reach, at least 1.

- [ ] **Step 1: Write the failing tests** in `test_light_input.cpp` (update existing calls to pass a reach plane and `emission_reach = 40`):
  - `TEST_CASE("pack reach")`: `pack_reach(40) == 10`, `pack_reach(41) == 11`, `pack_reach(0) == 1`, `pack_reach(-5) == 1`, `pack_reach(NAN) == 1`, `pack_reach(5000) == 255`.
  - `TEST_CASE("reach plane from materials")`: lava cell → `reach == 10`; rock and air → 0; burning wood with palette `fire_reach` from `emission_reach` 40 → 10; unloaded chunk → 0.
  - `TEST_CASE("reach block max at scale 2")`: a 2×2 block with one lava cell → 10.
  - `TEST_CASE("spot reach")`: spot radius 48 over lava texels (reach 10) → 12; over air → 12; texels outside the disc keep 0.
  - `TEST_CASE("spot reach clamps")`: radius 0 → 1; radius 1e9 → 255; both still add their emission.
  - `TEST_CASE("spot reach at scale 2")`: radius 48 at scale 2 → code 12 (reach stays in cells; the GPU divides by scale).
  - `TEST_CASE("restore spot reach")`: after `add_light_spots` then `restore_light_rects`, both planes equal the base.
  - `TEST_CASE("light cascade count")`: `0→1, 1→1, 2→2, 21→3, 40→4, 85→4, 86→5, 341→5, 342→6, 5000→6`.
  - Existing incremental-equals-full tests also compare the reach plane.

- [ ] **Step 2: Run to verify they fail**
  Run: `scons tests -j$(nproc) && build/tests/run_tests -tc='*light*,*reach*,*cascade*'`
  Expected: compile errors / failures on the new names.

- [ ] **Step 3: Implement** the interfaces above in `light_input.{h,cpp}`. `cell_texel` returns the reach next to the texel so the scale-1 and block paths share it; the block path keeps a per-texel max.

- [ ] **Step 4: Run the tests**
  Run: `scons tests -j$(nproc) && build/tests/run_tests`
  Expected: all pass. (The extension won't build until Task 2 updates its callers; that is expected.)

- [ ] **Step 5: Commit** — `git commit -m "feat(light): per-texel reach in the light inputs"`

### Task 2: Reach on the GPU and in PixelWorld

**Files:**
- Modify: `src/godot/light_shaders.h` (`LIGHT_MIP_COMPUTE`, `LIGHT_CASCADE_COMPUTE`), `src/godot/light_renderer.{h,cpp}`, `src/godot/pixel_world.{h,cpp}`
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Consumes: Task 1 names.
- Produces:
  - `PixelWorld.emission_reach` (float, cells, default 40.0, range 1..1020) — rebuilds the light palette on set, like `fire_glow_energy`.
  - `PixelLight.radius` and `marker_light_radius` are now reaches (no new properties).
  - `LightRenderJob` / `LightRenderer::render(texels, reach, area, bloom_threshold, surface_passes, cascade_count)` (Task 3 renames `surface_passes` to `surface_radius`)`; `reach` is a `PackedByteArray` of texel_w × texel_h bytes.

- [ ] **Step 1: Write the failing Godot test** in `run_tests.gd`: add `"emission_reach": 40.0` to `test_lighting_settings_defaults` defaults and `"emission_reach": 24.0` to its written values.

- [ ] **Step 2: Run to verify it fails**
  Run: `./build.sh --verify`
  Expected: build fails (callers of the Task 1 signatures) or the test reports `emission_reach` missing.

- [ ] **Step 3: PixelWorld plumbing.** Keep `light_reach` and `light_reach_base` `PackedByteArray`s sized texel_w × texel_h next to `light_texels` / `light_base` and pass them through every build / repaint / spot / restore call in `build_light_frame`. Compute `max_reach_cells = max(emission_reach, every spot's radius clamped to 1020)` and `cascade_count = light_cascade_count(max_reach_cells / area.scale)`; pass both planes and the count to the job.

- [ ] **Step 4: Renderer + shaders.**
  - A reach input texture (`R8_UNORM`, texel size) uploaded each frame, and a parallel R8 reach mip chain (levels 2..5, same sizes as the RGBA8 mips). `LIGHT_MIP_COMPUTE` also writes `max` of the 2×2 reach texels.
  - `LIGHT_CASCADE_COMPUTE` gets the reach sampler at binding 3 (level 0: the input; level ≥ 1: its reach mip). Replace `pad` in `CascadeParams` with `float step_texels` (= 2^(level−1), level 0 unused) and append `float reach_unit` (= `LIGHT_REACH_UNIT / area.scale`) padded to a 16-byte multiple. In the march, a sample at `step` is at `d = step * step_texels` texels; weight its emission by `w = pow(max(1 − d / R, 0), 2)` with `R = reach_texel * 255 * reach_unit`; a reach of 0 contributes nothing. Level 0 (own cell) keeps weight 1.
  - Run only levels `cascade_count − 1 … 0`; the top level run has `has_upper = 0`. Mips above the top level aren't built.

- [ ] **Step 5: Build and run all tests**
  Run: `./build.sh --test --verify`
  Expected: core and Godot tests pass.

- [ ] **Step 6: Visual check.** Capture `world_demo` with lighting on (the working-tree scene already enables it):
  Run: `mkdir -p /tmp/lr && godot --path . --write-movie /tmp/lr/reach.png --fixed-fps 60 --quit-after 180 res://demo/world_demo.tscn` and look at the last frame next to `docs/superpowers/specs/assets/2026-11-01-light-reach/today-lava.png`. If no lava is on screen, add `--` args or move the camera in a throwaway script; don't commit that.
  Expected: lava light ends about 40 cells out; nothing lights a far ceiling.

- [ ] **Step 7: Commit** — `git commit -m "feat(light): lights fade to zero at their reach; cascades follow the largest reach"`

### Task 3: Deep light into terrain

**Files:**
- Modify: `src/core/light_input.{h,cpp}`, `src/godot/light_shaders.h` (`LIGHT_SURFACE_COMPUTE`), `src/godot/light_renderer.{h,cpp}`, `src/godot/pixel_world.{h,cpp}` (default + range hint)
- Test: `tests/core/test_light_input.cpp`, `tests/godot/run_tests.gd`

**Interfaces:**
- Produces:
  - `LIGHT_SURFACE_MAX_DEPTH = 96`; `light_surface_depth` default 40, property range `0,96,1`.
  - `int light_surface_radius(int depth_cells, int scale);` = `ceil(clamp(depth, 0, MAX) / scale)` (replaces `light_surface_passes`).
  - `float light_surface_falloff(int radius);` unchanged meaning: per-texel factor k with `k^radius = LIGHT_SURFACE_FADE`; 1 at 0.

- [ ] **Step 1: Failing tests.** Rename the passes test to `light surface radius`: `(40,1)→40, (40,2)→20, (200,1)→96, (0,1)→0, (-3,1)→0`. Godot defaults test: `"light_surface_depth": 40`.

- [ ] **Step 2: Run to verify they fail** — `scons tests && build/tests/run_tests -tc='*light*,*reach*,*cascade*'`; expected: fail on `light_surface_radius`.

- [ ] **Step 3: Implement.** Replace the iterated 3×3 passes with a separable max filter, two dispatches after the light output:
  - H pass (`dir = (1,0)`), `light_texture → surface_texture`: `h(p) = max over |i| ≤ r of k^|i| · src(p + i·dir)` where `src(q) = min(light(q), 1)` if `q` is open (input opacity < 0.5), else 0.
  - V pass (`dir = (0,1)`), reads `h` from `surface_texture` and own light from `light_texture`, writes `light_texture`: open texels unchanged; solid texels `max(own, max over |i| ≤ r of k^|i| · h(p + i·dir))`.
  - One shader with a `dir` push constant and a `final_pass` flag is fine; push params `{ivec2 size; ivec2 dir; int radius; float falloff; int final_pass; int pad;}`. The output pass always writes `light_texture` now (drop the odd/even ping-pong).

- [ ] **Step 4: Run** `./build.sh --test --verify`; expected: all pass.

- [ ] **Step 5: Visual check** as in Task 2, Step 6 (`/tmp/lr/deep.png`); also capture `res://demo/lighting_demo.tscn` (`/tmp/lr/demo.png`). Expected: ground under lava lit ~40 cells deep and fading; the open air behind `lighting_demo`'s 6-cell wall stays dark.

- [ ] **Step 6: Bench.** `godot --path . res://bench/bench_world.tscn` (windowed); record light_gpu_ms / light_cpu_ms / frame p99 in the commit message.

- [ ] **Step 7: Commit** — `git commit -m "feat(light): light ~40 cells into terrain with a separable spread"`

### Task 4: Ambient tint and bloom defaults

**Files:**
- Modify: `src/godot/light_shaders.h` (`LIGHT_OVERLAY_SHADER`), `src/godot/pixel_world.{h,cpp}`
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Produces: `PixelWorld.ambient_tint` (Color, default `Color(0.62, 0.69, 1.0)`); `ambient` default 0.26; `bloom_threshold` default 2.5; `bloom_strength` default 0.35. Overlay: `COLOR.rgb = min(ambient * ambient_tint.rgb + light * light_exposure, 1)`.

- [ ] **Step 1: Failing test.** In `test_lighting_settings_defaults`: `"ambient": 0.26`, `"ambient_tint": Color(0.62, 0.69, 1.0)`, `"bloom_threshold": 2.5`, `"bloom_strength": 0.35`; written values include `"ambient_tint": Color(1, 1, 1)`.
- [ ] **Step 2: Run** `./build.sh --verify`; expected: the defaults test fails.
- [ ] **Step 3: Implement** the property (live: set the overlay material's uniform on change, like `ambient`).
- [ ] **Step 4: Run** `./build.sh --verify`; expected: pass.
- [ ] **Step 5: Visual check** (`/tmp/lr/ambient.png`, and the same at `light_resolution = 2` via a throwaway override): unlit rock reads as dim blue-grey shapes like `ref-noita-lit-cave.png`; bloom is a halo, terrain next to lava visible. Adjust these four defaults (and lava / fire `emission_energy` in `materials/*.tres` if needed) by eye; update the test to the final values.
- [ ] **Step 6: Commit** — `git commit -m "feat(light): cool ambient tint, quieter bloom"`

### Task 5: Back wall generator (core)

**Files:**
- Create: `src/core/back_wall.h`, `src/core/back_wall.cpp`
- Modify: `src/core/chunk.h` (`std::vector<uint32_t> back_wall;` — CHUNK×CHUNK RGBA8 as `0xRRGGBBAA`, empty = none), `src/core/terrain.{h,cpp}`
- Test: `tests/core/test_back_wall.cpp`

**Interfaces:**
- Produces:
  - `struct BackWallConfig` in `back_wall.h`, a member `back_wall` of `TerrainConfig`. Fields and defaults (tuned by eye in Task 7):
    `bool enabled = true; float darken = 0.38f; float desaturate = 0.5f; int edge_rough = 4;`
    `float hollow_frequency = 0.012f; float hollow_stretch = 3.0f; float hollow_amount = 0.35f; float hollow_darken = 0.3f;`
    `int stalactite_length = 14; int stalagmite_length = 6; int tooth_period = 8;`
    `float crack_frequency = 0.04f; float crack_width = 0.04f; float crack_darken = 0.75f;`
    `float pebble_amount = 0.35f; float shade_amount = 0.06f;`
  - `class BackWall { public: BackWall(const BackWallConfig&, uint32_t seed); void fill(const TerrainGenerator&, const MaterialTable&, ChunkCoord, std::vector<uint32_t>& out) const; uint32_t colour_at(const TerrainGenerator&, const MaterialTable&, int wx, int wy) const; bool hollow_at(const TerrainGenerator&, int wx, int wy) const; };` — `colour_at` is the slow per-cell reference; `fill` must produce identical bytes. Alpha 0 = no wall, else 255.
  - `TerrainGenerator`: public `MatId biome_base(int biome) const;` and `const BackWall* back_wall() const;` (null when `!config.back_wall.enabled`); `generate()` fills `out.back_wall` (cleared when disabled). New noise fields get their own `hash5` salts.

- [ ] **Step 1: Failing tests** in `test_back_wall.cpp`, using `terraintest` helpers and the starter-like config:
  - `back wall absent above the surface`: every cell with `wy < surface_height(wx) − edge_rough` has alpha 0; every cell with `wy ≥ surface_height(wx) + edge_rough` has alpha 255.
  - `back wall chunk matches per-cell reference`: for chunks (0,2), (−1,3), (−3,−1) and (5,4), `fill` equals `colour_at` at every cell.
  - `back wall deterministic`: two generators with the same seed give identical `fill` bytes for (2,3); a different seed differs somewhere.
  - `back wall hollow share`: over the 16 chunks (0..3, 3..6), the share of cells with `hollow_at` (teeth applied) among wall cells is within [0.15, 0.55]; and every `hollow_at` cell's colour is darker than every non-hollow wall cell of the same biome in that chunk.
  - `back wall darker than terrain`: every wall cell's max channel < `darken + 0.1` × the max channel of its biome's base material colour (×255).
  - `back wall disabled`: `enabled = false` → `generate` leaves `back_wall` empty.

- [ ] **Step 2: Run** `scons tests && build/tests/run_tests -tc='back wall*'`; expected: compile failure.

- [ ] **Step 3: Implement.** Per cell, from world coordinates only:
  - Coverage: `depth = wy − surface_height(wx) − rough(wx)` where `rough` is 1-D noise in [0, edge_rough]; `depth < 0` → 0.
  - Base: biome base material colour (`biome_at`, the same dithered pick as terrain), lerped toward its luma by `desaturate`, times `darken`, times `1 + shade_amount·(hash − 0.5)`.
  - Hollow field: FBm noise sampled at `(wx, wy · hollow_stretch)` × `hollow_frequency`, hollow when above the level that leaves `hollow_amount` of cells hollow (fixed threshold computed from the amount, like `slab_cut`).
  - Teeth: a hollow cell becomes wall when a non-hollow cell lies within `down(wx)` above it (stalactite) or within `up(wx)` below it (stalagmite); `down(wx) = floor(stalactite_length · tri(wx)^1.6 · amp(wx))`, `up` the same with `stalagmite_length` and a different salt; `tri` a triangle wave whose period varies in `[tooth_period − 2, tooth_period + 2]` per segment by hash; `amp` 1-D noise in [0,1] raised to 1.5.
  - Hollow colour: base × `hollow_darken`. Wall detail: ridged noise `|n| < crack_width` → × `crack_darken`; wall cells within ~4 cells of a hollow (hollow field just under its threshold) get pebble blobs (cellular noise above `1 − pebble_amount`) at × 1.15.
  - `fill` evaluates the hollow field once over the chunk plus `max(stalactite_length, stalagmite_length)` rows above and below, then resolves teeth per column; `colour_at` may evaluate directly.

- [ ] **Step 4: Run** `scons tests && build/tests/run_tests`; expected: all pass. Also confirm `gen_ms` in the existing streaming / terrain timing tests didn't more than double.

- [ ] **Step 5: Commit** — `git commit -m "feat(worldgen): generated back wall with toothed hollows"`

### Task 6: Draw the back wall

**Files:**
- Modify: `src/godot/pixel_world_generator.{h,cpp}` (one property per `BackWallConfig` field, `back_wall_` prefix, into `to_config`), `src/godot/pixel_world.{h,cpp}`
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Consumes: `Chunk::back_wall`, `BackWallConfig`.
- Produces: `PixelWorld.back_wall_enabled` (bool, default true); `ChunkView` gains `RID back_texture; bool has_back = false;`.

- [ ] **Step 1: Failing Godot tests.**
  - `test_back_wall_generator_defaults`: `PixelWorldGenerator.new()` has `back_wall_enabled == true`, `back_wall_darken ≈ 0.38`, `back_wall_hollow_amount ≈ 0.35`, `back_wall_stalactite_length == 14`.
  - `test_back_wall_draws_without_lighting`: a world with the default generator and `lighting_enabled = false` ticks until a chunk below the surface is loaded; that chunk's view has a back texture (expose `get_stats()["back_walls"]`, the number of views with a back texture) > 0; with `back_wall_enabled = false` set before `_ready`, it is 0.
  - After `unload` + regenerate of the same chunk (use the existing streaming test pattern), `back_walls` is > 0 again.
- [ ] **Step 2: Run** `./build.sh --verify`; expected: failures on the missing properties / stat.
- [ ] **Step 3: Implement.** When a view's cell texture is first created and `back_wall_enabled` and `!chunk.back_wall.empty()`, create the back texture from `back_wall` and add its texture rect to `view.canvas_item` **before** the cell rect (draw order = command order). Never re-upload it on `render_dirty`. Toggling `back_wall_enabled` at runtime clears and re-adds each view's rects. Free the back texture in `drop_view`.
- [ ] **Step 4: Run** `./build.sh --test --verify`; expected: all pass.
- [ ] **Step 5: Commit** — `git commit -m "feat(render): draw the back wall under each chunk"`

### Task 7: Tune by eye, demos and bench

**Files:**
- Modify: `demo/world_demo.tscn` (commit the working-tree change that turns lighting on), `demo/lighting_demo.tscn` (retune overrides; first `git diff` it — it has uncommitted changes that aren't ours; keep them), `src/core/back_wall.h` / `src/godot/pixel_world.h` defaults if tuning changes them, the matching tests
- Test: existing suites

- [ ] **Step 1: Capture** `world_demo` (`/tmp/lr/final.png`) at a spot with a cave, lava and a hole near the surface. Compare with `ref-backwall-blue.png`, `ref-backwall-grey.png` and `ref-noita-lit-cave.png`.
  Expected: hollows wide and horizontal with stalactite teeth on top, shorter stalagmites below; wall clearly darker and lower contrast than terrain; unlit areas dim but readable; lava light local; no back wall in the sky; edge at entrances not ruler-straight.
- [ ] **Step 2: Tune** `BackWallConfig`, ambient, bloom and reach defaults until Step 1 holds; mirror any changed default in its test.
- [ ] **Step 3: Retune `lighting_demo`** overrides (`ambient`, `light_surface_depth`, `bloom_threshold`) so it still reads well with the new defaults; it keeps the flat backdrop.
- [ ] **Step 4: Run** `./build.sh --test --verify` and `bench_world`; expected: all pass; record light_gpu_ms, light_cpu_ms, upload_ms, gen_ms and frame p99 vs the last numbers in `bench/results/`.
- [ ] **Step 5: Show the user** the final capture and the bench numbers; their eye is the acceptance gate.
- [ ] **Step 6: Commit** — `git commit -m "tune: back wall, ambient and bloom defaults; lighting on in world_demo"`
