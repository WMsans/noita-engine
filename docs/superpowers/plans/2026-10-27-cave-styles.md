# Cave Styles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the cave algorithm a per-biome choice. `classic` is today's algorithm, unchanged. `slabs` is a new one, tuned until its rock shape looks like the user's reference: mostly open space, with wide flat ragged slabs that have holes.

**Architecture:** Every style turns a cell into a signed *cave margin* (> 0 = air). `cave_for` sums each style's margin weighted by the share of the cell's biome weight on that style. `slabs` is a domain-warped, vertically squashed fBm field plus a fine roughness term, with a sparse hole field OR'd in. It's cut at a level read from a quantile table built at compile time, so a biome's `slab_air` *is* its air share. Everything stays per-cell, so chunks stay independent and `cave_at` still agrees with `generate()`.

**Tech Stack:** C++17 core (`src/core`), FastNoiseLite, doctest (`./build.sh --test`), godot-cpp bindings (`src/godot`), GDScript tests (`./build.sh --verify`).

**Spec:** `docs/superpowers/specs/2026-10-27-cave-styles-design.md`

## Global Constraints

- A biome on `classic` generates exactly the same cells as today. Pinned by the fingerprint `0xc99e0ec9f0e4e29b` (Task 1).
- `BiomeDef::cave_style` and `PixelBiome.cave_style` default to `classic`. A `.tres` without the field reads as classic.
- Shape of rock against air only: pockets, pools, surface skins and rooms keep using today's fields.
- Each chunk generates on its own. `cave_at(wx, wy)` must agree with `generate()`.
- The surface taper and entrance noise apply to `slabs` exactly as to `classic`. No cave on the surface row when entrances are off.
- New noise salts are `0x60`–`0x63`. Never reuse or renumber existing salts.
- Targets for the shipped `slabs` look: air share 50–60% below the taper, largest connected air region ≥ 90% of air, rock slabs typically 3–5× wider than tall.
- Every biome `.tres` ships with `cave_style = slabs`. The user's uncommitted edits in `worldgen/*.tres` are built on, not reverted.

## Review Focus

1. **Out-of-range amounts** (`slab_air` < 0 or > 1, `slab_holes` > 1): clamp to [0, 1], never index past the quantile table. Test in Task 2.
2. **`slab_stretch` ≤ 0 or `slab_frequency` ≤ 0**: compile fails with an error naming the field. A zero stretch would make every column identical. Test in Task 2.
3. **Biome border between styles**: no hard vertical seam where classic meets slabs. Test in Task 3.
4. **Far from the origin** (x = 10 000 000): slabs still vary and aren't all air or all rock. FastNoiseLite precision is the risk. Test in Task 2.
5. **Generation cost**: slabs adds ~5 noise evaluations per cell. The world bench's stream budget (`STREAM_BUDGET_MS` 1.0, p99 3.0) must still hold. Manual check in Task 5.

---

### Task 1: Cave style field and margin-based `cave_for` (classic unchanged)

**Files:**
- Modify: `src/core/terrain.h` (BiomeDef, Blended, private helpers)
- Modify: `src/core/terrain.cpp:394-461` (`blend`, `cave_for`)
- Test: `tests/core/test_terrain.cpp`

**Interfaces:**
- Produces: `enum class CaveStyle : uint8_t { Classic, Slabs };` in `terrain.h`. `BiomeDef::cave_style = CaveStyle::Classic`. `Blended` gains `double classic_weight = 0, slabs_weight = 0;` (the share of the cell's biome weight on each style; they sum to 1). `tunnel` and `cavern` are now blended over classic biomes only, renormalised by `classic_weight`. Private `double classic_margin(int wx, int wy, double taper, const Blended&) const;`.

- [ ] **Step 1: Write the pinning test** `TEST_CASE("classic caves are unchanged")`. Compile `terraintest::starter_config()` with `test_seed()`. For `x` in [-512, 512), `s = surface_height(x)`, `d` in [-8, 600): FNV-1a 64 over `cave_at(x, s + d)` (`h = (h ^ (uint64_t)c) * 1099511628211ull`, starting at `1469598103934665603ull`). `CHECK(h == 0xc99e0ec9f0e4e29bull)` and `CHECK(caves == 205590)`.
- [ ] **Step 2: Run it on the unchanged code.** Run `./build.sh --test`. Expected: PASS. The value was taken from today's code, so it pins the baseline.
- [ ] **Step 3: Add `CaveStyle` and the `Blended` weights, and rewrite `cave_for` as a margin sum.** The classic margin is `max(tunnel·taper − |n_tunnel|, n_cavern − (1 − 2·cavern·taper))`, where a term whose amount is 0 contributes −∞. Return `false` early when `taper <= 0` (as today). Cave iff `classic_weight·classic_margin + slabs_weight·slabs_margin > 0`. Only evaluate a style's margin when its weight is > 0. Until Task 2, the slabs margin is a stub that returns −1.
- [ ] **Step 4: Run the tests.** Run `./build.sh --test`. Expected: every test passes, including the fingerprint.
- [ ] **Step 5: Commit** `refactor(worldgen): cave styles as weighted margins, classic unchanged`.

### Task 2: The `slabs` style

**Files:**
- Modify: `src/core/terrain.h` (TerrainConfig, BiomeDef, new fields and members)
- Modify: `src/core/terrain.cpp` (salts, `compile`, `blend`, new `slabs_margin`)
- Modify: `tests/core/terrain_test_util.h` (add `slabs_config()`)
- Test: `tests/core/test_terrain.cpp`

**Interfaces:**
- Consumes: `CaveStyle`, `Blended::slabs_weight`, and the margin sum from Task 1.
- Produces:
  - `TerrainConfig` fields with core defaults: `float slab_frequency = 0.004f, slab_stretch = 3.0f, slab_warp = 24.0f, slab_rough_frequency = 0.06f, slab_rough = 0.12f, slab_hole_frequency = 0.02f;`
  - `BiomeDef` fields: `float slab_air = 0, slab_holes = 0;` These are amounts in [0, 1], clamped. `slab_air` is the target air share.
  - `Blended` gains `slab_air` and `slab_holes`, blended over slabs biomes only and renormalised by `slabs_weight`.
  - Private `double slabs_margin(int wx, int wy, double taper, const Blended&) const;`
  - Members: `n_slab_, n_slab_warp_, n_slab_rough_, n_slab_hole_` (salts `0x60`, `0x61`, `0x62`, `0x63`), plus `std::vector<float> slab_quantiles_`.
  - `terraintest::slabs_config()`: a single open band biome `"slabby"`, base `rock`, `cave_style = Slabs`, `slab_air = 0.55f`, `slab_holes = 0.1f`, everything else 0. Core-default generator fields.

**The field** (the part the signature doesn't determine):

```
f(x, y) = fbm4(n_slab_, x + warp·wx(x,y), (y + warp·wy(x,y))·stretch) + rough·n_slab_rough_(x, y)
  wx, wy = n_slab_warp_ sampled at (x, y) and at (x + 5413.7, y − 2971.3), frequency slab_frequency·2
cut(a)  = slab_quantiles_ at fraction (1 − a), linearly interpolated; a ≤ 0 → +∞ (no air)
slab margin = max( f − cut(slab_air·taper),
                   n_slab_hole_(x, y) − (1 − 2·slab_holes·taper) )
```

`slab_quantiles_` is built in `compile()`. Sample `f` (without the hole term) on a 256×256 grid with a spacing of 37 cells, starting at (−4736, −4736). Sort, and keep 1025 evenly spaced order statistics. The grid is seeded and fixed, so the table is deterministic.

- [ ] **Step 1: Write the failing tests.** Every window starts at least `cave_taper_depth` below the surface.
  - `TEST_CASE("slabs air share follows slab_air")`: `slabs_config()`, window 1024×1024. Air fraction is in [0.50, 0.62]. With `slab_air = 0.3f` it's in [0.25, 0.40].
  - `TEST_CASE("slabs are wide and flat")`: over the same window, the mean horizontal run of rock cells is ≥ 2.5× the mean vertical run.
  - `TEST_CASE("slabs open space is connected")`: 4-neighbour flood fill over a 1024×512 window. The largest air component is ≥ 0.9 of all air.
  - `TEST_CASE("slabs cave_at agrees with generate")`: make chunks (0..2, 2..4) and check that every cell's `mat == MAT_AIR` equals `cave_at`. The biome has no liquid, so air is exactly cave.
  - `TEST_CASE("slabs are deterministic per seed")`: two compiles with `test_seed()` match over a 512×512 window. A compile with `test_seed() + 1` differs in > 10% of cells.
  - `TEST_CASE("slabs amounts are clamped")`: `slab_air = 1.7f` gives the same cells as `1.0f`. `slab_air = -0.5f` gives no cave cells at all.
  - Extend `TEST_CASE("far from the origin terrain still varies")` with a `slabs_config()` generator: it has both cave and solid cells in the chunk at (156250, 3).
  - Extend `TEST_CASE("config errors name the culprit")`: `slab_stretch = 0` fails and the error contains `"slab_stretch"`; `slab_frequency = -1` fails and the error contains `"slab_frequency"`.
- [ ] **Step 2: Run them.** Run `./build.sh --test`. Expected: the new tests FAIL. The stub margin means no air, and the new fields don't exist yet.
- [ ] **Step 3: Implement the fields, validation, quantile table and `slabs_margin`** as specified above. The classic code paths stay untouched.
- [ ] **Step 4: Run the tests.** Run `./build.sh --test`. Expected: everything passes, including the Task 1 fingerprint. If shape tests fail, tune the *core defaults* (stretch, warp, rough), not the test bounds.
- [ ] **Step 5: Commit** `feat(worldgen): slabs cave style`.

### Task 3: Blending across a style border

**Files:**
- Test: `tests/core/test_terrain.cpp`
- Modify (only if the test fails): `src/core/terrain.cpp`

**Interfaces:**
- Consumes: Tasks 1–2.

- [ ] **Step 1: Write the test** `TEST_CASE("no seam where cave styles meet")`. Use one open band with two biomes of equal weight: `"old"` (classic, tunnel 0.12, cavern 0.3) and `"new"` (slabs, `slab_air` 0.55). Set `region_width = 512` and `region_transition = 128`, and use the seed whose region picks put the two biomes side by side. Find that seed by scanning `test_seed() + k` for the first one where the regions holding x = 0 and x = 512 differ, then locate the border with `weights_at`, as the existing blend test does. Count rows where `cave_at(x, y) != cave_at(x + 1, y)` over 512 rows below the taper:
  - `CHECK` that the count at every column pair inside the transition is ≤ 3× the mean count of the 64 column pairs on either side.
  - `CHECK` that the transition holds at least some cave cells.
- [ ] **Step 2: Run it.** Run `./build.sh --test`. Expected: PASS if Task 1's weighted sum is right. If it fails, fix the blend, not the bound.
- [ ] **Step 3: Commit** `test(worldgen): no seam where cave styles meet`.

### Task 4: Godot bindings

**Files:**
- Modify: `src/godot/pixel_biome.h`, `src/godot/pixel_biome.cpp`
- Modify: `src/godot/pixel_world_generator.h`, `src/godot/pixel_world_generator.cpp`
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Consumes: the `CaveStyle`, `BiomeDef` and `TerrainConfig` fields from Tasks 1–2.
- Produces:
  - `PixelBiome`: `enum CaveStyle { CAVE_STYLE_CLASSIC = 0, CAVE_STYLE_SLABS = 1 }`, bound with `BIND_ENUM_CONSTANT` and `VARIANT_ENUM_CAST`. Property `cave_style` (INT, `PROPERTY_HINT_ENUM "Classic:0,Slabs:1"`, default 0). Properties `slab_air` and `slab_holes` (FLOAT). `to_def()` maps them; unknown ints fall back to Classic (as `PixelRoom` does for anchors).
  - `PixelWorldGenerator` properties `slab_frequency`, `slab_stretch`, `slab_warp`, `slab_rough_frequency`, `slab_rough`, `slab_hole_frequency`, with the core defaults. They go into `to_config()` next to `cavern_frequency`.

- [ ] **Step 1: Write the failing GDScript tests:**
  - `test_biome_cave_style_defaults_classic()`: `PixelBiome.new().cave_style == PixelBiome.CAVE_STYLE_CLASSIC`.
  - `test_generator_slab_defaults()`: `PixelWorldGenerator.new().slab_stretch == 3.0` and `slab_frequency` ≈ 0.004.
- [ ] **Step 2: Run them.** Run `./build.sh --verify`. Expected: FAIL, because the property doesn't exist yet.
- [ ] **Step 3: Implement the bindings**, following the existing members' pattern line for line.
- [ ] **Step 4: Run the tests.** Run `./build.sh --verify`. Expected: PASS, including `test_default_generator_validates`.
- [ ] **Step 5: Commit** `feat(worldgen): expose cave styles to Godot`.

### Task 5: Ship slabs everywhere and tune against the reference

**Files:**
- Modify: `worldgen/forest.tres`, `desert.tres`, `underground.tres`, `caverns.tres`, `deep.tres`, `default_generator.tres`
- Test: `tests/godot/run_tests.gd`
- Throwaway: a renderer in the session scratchpad (`preview/`)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Write the failing test** `test_shipped_biomes_use_slabs()`. Load each of the five biome `.tres` files and check `cave_style == PixelBiome.CAVE_STYLE_SLABS` and `slab_air > 0`.
- [ ] **Step 2: Run it.** Run `./build.sh --verify`. Expected: FAIL.
- [ ] **Step 3: Set every biome to `cave_style = 1`** with starting amounts `slab_air = 0.55` and `slab_holes = 0.1`. Lower the surface band to `slab_air = 0.4` so the starting ground isn't swiss cheese; that's the builder's call to retune. In `default_generator.tres`, scale the core defaults by the world's 4× factor: `slab_frequency = 0.001`, `slab_warp = 96`, `slab_rough_frequency = 0.015`, `slab_hole_frequency = 0.005`. `slab_stretch` and `slab_rough` don't scale.
- [ ] **Step 4: Tuning loop.** Extend the scratchpad renderer to mirror these `.tres` values. Render 2000×900 windows at depths 1000, 3000 and 7000 for three seeds, side by side with the reference image. For each render, print the air share, the largest-connected-air fraction and the rock run-length ratio. Adjust the generator fields and biome amounts until all of these hold:
  - air is 50–60% below the taper;
  - connectivity is ≥ 0.9;
  - the run ratio is ≥ 3;
  - by eye, the slabs are lumpy, ragged-edged shelves with holes, as in the reference.

  Record the final render paths for the user.
- [ ] **Step 5: In-game check.** Run `./build.sh --verify` and expect PASS. Then run `world_demo.tscn` and `creatures_demo.tscn`. The player spawns on ground (widen the demo spawn raycasts if needed, as in the bigger-world change), rooms still appear, and caves look like the renders. Optionally run the world bench (`godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/world.json`, which needs a window) and confirm the stream budgets still pass.
- [ ] **Step 6: Commit** the `.tres` files and the test as `feat(worldgen): ship slabs caves, tuned to the reference`. This includes the user's earlier uncommitted tweaks to these files, which this step builds on.
