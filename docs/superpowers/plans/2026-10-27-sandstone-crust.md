# Sandstone Crust Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A thin band of a new solid `sandstone` under the desert's surface sand, which caves don't carve except on entrance columns, so the dunes stop draining into the slabs caves.

**Architecture:** Two new `BiomeDef` fields, `crust_material` and `crust_depth`. `crust_depth` is blended like `surface_depth`. In `generate()`, a cell at `top <= depth < top + crust` whose picked biome has a crust becomes crust. Cave carving skips that cell unless `entrance_at(wx)` is true. `cave_at` stays the raw cave field (it knows nothing of materials).

**Tech Stack:** C++17 core (`src/core`), doctest (`./build.sh --test`), godot-cpp bindings (`src/godot`), GDScript tests (`./build.sh --verify`).

**Spec:** `docs/superpowers/specs/2026-10-27-sandstone-crust-design.md`

## Global Constraints

- Biomes without a crust generate exactly as today. The classic fingerprint `0xc99e0ec9f0e4e29b` / `205590` must still pass.
- `crust_material = ""` / `crust_depth = 0` are the defaults. A `.tres` without the fields has no crust.
- Entrance columns cut through the crust. No new noise field and no new salt.
- Shipped desert: `crust_material = &"sandstone"`, `crust_depth = 6`.
- Sandstone: `material_class = 1` (Solid), `density = 2.4`, `color = Color(0.72, 0.6, 0.4, 1)`, `collision_solid = true`, `light_opacity = 1.0`. Appended **last** in `default_set.tres`, so existing material ids don't change.

## Review Focus

1. **The crust actually gets protected**: slabs caves at high air would otherwise cut the band. The test must show `cave_at` is true for some crust cells while `generate()` still writes sandstone there (Task 1).
2. **Entrances**: crust cells that are air occur only on `entrance_at` columns, and with default entrances some exist (Task 1).
3. **Unknown `crust_material`**: compile fails naming `crust_material` (Task 1).
4. **`crust_depth` ≤ 0 with a material set**: no crust and no error. The band `[top, top + crust)` is empty (Task 1).
5. **Material id order**: appending sandstone must not shift rock..grass ids 1..12. The Godot id test pins sandstone as id 13 (Task 2).

---

### Task 1: Core crust layer

**Files:**
- Modify: `src/core/terrain.h` (`BiomeDef`, `BiomeIds`, `Blended`, public `entrance_at`)
- Modify: `src/core/terrain.cpp` (`compile` material resolution, `blend`, `taper_at`, `generate`)
- Modify: `tests/core/terrain_test_util.h` (add `sandstone` to `terrain_table`, add `crust_config()`)
- Test: `tests/core/test_terrain.cpp`

**Interfaces:**
- Produces: `BiomeDef::crust_material` (`std::string`, `""` = none), `BiomeDef::crust_depth` (`int`, 0), `bool TerrainGenerator::entrance_at(int wx) const`, `terraintest::crust_config()`.

- [ ] **Step 1: Test helpers.** In `terrain_table()` append `defs[7]` `"sandstone"`: Solid, density 2.4, collision_solid. Update the comment's material count. Add `crust_config()`: one open-band biome `"dunes"` (0, -1): surface `sand` depth 6, base `sand`, crust `sandstone` depth 6, `CaveStyle::Slabs`, `slab_air = 0.9f`, `slab_holes = 0.1f`.

- [ ] **Step 2: Write the failing tests** in `test_terrain.cpp`:

```cpp
TEST_CASE("crust holds under the caves") {
	// crust_config with entrance_threshold = 2.0f (entrances off), ChunkCache over it.
	// For x in [-2000, 2000), d in [6, 12): REQUIRE cache.at(x, s + d) == table.id_of("sandstone").
	// Also count gen->cave_at(x, s + d) over the same cells: CHECK(count > 0),
	// so the caves really reach the band.
}

TEST_CASE("entrances cut the crust") {
	// crust_config with default entrances. Over x in [-20000, 20000), d in [6, 12):
	// every non-sandstone cell has gen->entrance_at(x) == true (REQUIRE_MESSAGE with x),
	// and CHECK(at least one such cell).
}

TEST_CASE("no crust when crust_depth is not positive") {
	// crust_config with crust_depth = 0, then -3: no cell in x [-500, 500), d [0, 40)
	// is sandstone.
}
```

In `"config errors name the culprit"`, add a case: `crust_config()` with `crust_material = "granite"` fails, and the error contains `"crust_material"`.

- [ ] **Step 3: Run tests, expect failure.** `./build.sh --test` fails to compile (no `crust_material`, `entrance_at`).

- [ ] **Step 4: Implement.**
  - Header: the new `BiomeDef` fields, with a comment like the others (`// a solid band under the surface skin that caves don't carve, except at entrances`). Add `BiomeIds::crust`, `Blended::crust_depth`, and public `bool entrance_at(int wx) const;  // the column is a cave entrance: it carves through the surface and crust`.
  - `compile`: `resolve_material(..., def.crust_material, "crust_material", ids.crust, error)` alongside the others.
  - `blend`: `crust_depth` in both the single-biome and weighted paths, like `surface_depth`.
  - `taper_at`: use `entrance_at(wx)` instead of the inline noise test. Behaviour must be identical.
  - `generate`: after `top`, compute `const int crust = (int)std::lround(amounts.crust_depth);`. Then, before the pocket branch: `else if (ids.crust != MAT_AIR && depth < top + crust) mat = ids.crust;`. Gate the cave block with `(mat != ids.crust || ids.crust == MAT_AIR || entrance_at(wx))`. A plain `bool in_crust` flag is clearer, so use that instead of the `mat` comparison.

- [ ] **Step 5: Run tests, expect pass.** `./build.sh --test`: all pass, including `"classic caves are unchanged"` and `"slabs cave_at agrees with generate"`.

- [ ] **Step 6: Commit.** `git commit -m "feat(worldgen): crust layer that caves don't carve"`

### Task 2: Sandstone material, Godot binding, desert ships it

**Files:**
- Create: `materials/sandstone.tres`
- Modify: `materials/default_set.tres` (ext_resource `1_sandstone`, `load_steps` 18 → 19, append to `materials` last)
- Modify: `src/godot/pixel_biome.h`, `src/godot/pixel_biome.cpp` (`crust_material: StringName`, `crust_depth: int`, bound and copied in `to_def`, placed next to `surface_depth`)
- Modify: `worldgen/desert.tres` (`crust_material = &"sandstone"`, `crust_depth = 6`)
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Consumes: `BiomeDef::crust_material`, `BiomeDef::crust_depth` (Task 1).

- [ ] **Step 1: Write the failing tests.**
  - `test_starter_set_ids_and_fields`: append `&"sandstone"` to `expected`.
  - New `test_biome_crust_defaults_none`: `PixelBiome.new()` has `crust_material == &""` and `crust_depth == 0`.
  - New `test_desert_ships_sandstone_crust`: `load("res://worldgen/desert.tres")` has `crust_material == &"sandstone"` and `crust_depth == 6`. Sandstone's `material_class` in `default_set.tres` is 1.

- [ ] **Step 2: Run, expect failure.** `./build.sh --verify` reports the new tests FAILED (or a property-not-found error).

- [ ] **Step 3: Implement** the files above with the values from Global Constraints.

- [ ] **Step 4: Run, expect pass.** `./build.sh --test --verify`: `ALL GODOT TESTS PASSED`, and the core suite is green. The existing `default_set` validation and default-generator validation tests cover sandstone resolving in the shipped config.

- [ ] **Step 5: Look at it.** Run the demo (`run` skill or the editor). In the desert, the dunes stay put over the caves, there's a visible sandstone band, and sand only pours down entrances.

- [ ] **Step 6: Commit.** `git commit -m "feat(worldgen): sandstone crust under the desert dunes"`
