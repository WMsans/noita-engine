# Material Textures and Generated Crust Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Opted-in materials draw a 16×16 db32 texture tiled in world space, and generated solid
edges get Noita-style blob crust drawn from a second texture, seamless across chunks.

**Architecture:** A textured cell's existing `shade` byte becomes its texel index
(`(y&15)*16 + (x&15)`), and a new `FLAG_CRUST` bit marks crust; both travel with the cell, so
rendering stays one table lookup. `TerrainGenerator::generate()` is split into resolve (padded
region) → passes → emit; the crust stamp is the first `GenPass`, and later cross-chunk features
are more passes.

**Tech Stack:** C++17 core (`src/core`), doctest (`./build.sh --test`), godot-cpp bindings
(`src/godot`), GDScript tests (`./build.sh --verify`).

**Spec:** `docs/superpowers/specs/2026-11-01-material-textures-design.md`

## Global Constraints

- `Cell` stays 8 bytes. `FLAG_CRUST = 1 << 2`. No other flag bits change meaning.
- Texel index: `(uint8_t)(((ty & 15) << 4) | (tx & 15))`; 1 texel = 1 cell; textures exactly 16×16.
- Texture words use the same packing as `color_rgba` (`0xRRGGBBAA`, i.e. Godot `to_rgba32()`).
- A material with no texture behaves exactly as today (hash shade, flat colour + `shade_amount`).
- A world whose materials have no `crust_texture` generates byte-identical cells to today, at
  today's cost (no passes → margin 0). Classic fingerprint `0xc99e0ec9f0e4e29b` / `205590` holds.
- Crust: only Solid-class cells whose material has a crust texture; edge = a 4-neighbour of class
  Empty, Liquid or Gas (Powder is not open). Uniform: sky surface and rooms included.
- Defaults: `crust_chance = 0.3`, `crust_radius = 6`. Blob radius `crust_radius * (0.7 + 0.6 * h)`.
- Drawing a chunk stays one `cell_rgba` lookup per cell.
- Material ids don't change: no materials are added or reordered in `default_set.tres`.

## Review Focus

1. **Negative world coords tile continuously** — `x & 15` on negative ints must give 15 at x = −1,
   so no mirrored seam at x = 0 / y = 0 (Task 1 test).
2. **A blob centre in the neighbour chunk** reaching across the border — must match a wide
   reference region (Task 3 seamless test uses a border column where crust exists on both sides).
3. **Crust under a liquid pool and at the sky surface** gets crust; crust never on sand next to
   air, never on air (Task 3).
4. **A crust cell that melts/reacts** loses `FLAG_CRUST` (spawn_cell resets flags) — and a
   crust grain falling as a particle keeps both texel and crust colour (Task 1 / Task 4).
5. **Bad texture input** — wrong size in core (`compile` error naming the material) and in Godot
   (`push_error`, falls back to flat colour, world still loads) (Tasks 1, 4).

---

### Task 1: Texture data in materials, texel shade, crust flag

**Files:**
- Modify: `src/core/cell.h` (add `FLAG_CRUST`)
- Modify: `src/core/material.h`, `src/core/material.cpp` (fields, copy, validation)
- Modify: `src/core/generator.h`, `src/core/generator.cpp` (`texel_index`, `spawn_cell` overload)
- Test: `tests/core/test_material.cpp`

**Interfaces:**
- Produces: `constexpr uint8_t FLAG_CRUST = 1 << 2;`
- Produces: `MaterialDef::texture_rgba`, `MaterialDef::crust_rgba` (`std::vector<uint32_t>`, empty
  or 256 words, row-major y*16+x); same two fields on `CompiledMaterial`.
- Produces: `inline uint8_t texel_index(int tx, int ty)` in `generator.h`.
- Produces: `Cell spawn_cell(MatId, const MaterialTable&, uint32_t seed, int wx, int wy, int tx, int ty)`;
  the existing 5-arg `spawn_cell` forwards with `tx = wx, ty = wy`.

- [ ] **Step 1: Write the failing tests** in `test_material.cpp`:

```cpp
TEST_CASE("textured material spawns its texel as shade") {
	// defs: "rock" Solid with texture_rgba = 256 words; "water" Liquid untextured.
	// spawn_cell(rock, t, 1, 5, 3).shade == 3*16 + 5
	// spawn_cell(rock, t, 1, -1, -1).shade == 255        // negative coords wrap, no mirror
	// spawn_cell(rock, t, 1, 21, 19).shade == 3*16 + 5   // 16-cell period
	// spawn_cell(rock, t, 1, 100, 100, 2, 7).shade == 7*16 + 2  // explicit texel coords win
	// spawn_cell(water, t, 1, 5, 3).shade == (hash5(1, 0, 5, 3, 0) & 0xFF)  // unchanged
	// spawn_cell(...).flags == 0  (no FLAG_CRUST from spawning)
}

TEST_CASE("texture of the wrong size fails compile naming the material") {
	// "rock" texture_rgba of 255 words -> compile false, error contains "rock" and "texture".
	// "rock" crust_rgba of 300 words  -> compile false, error contains "rock" and "crust".
}
```

- [ ] **Step 2: Run** `./build.sh --test` — Expected: compile failure (fields / overload missing).

- [ ] **Step 3: Implement.** Add the fields and `FLAG_CRUST` (update the `flags` comment in
  `cell.h`). `compiled_from` copies both vectors. `MaterialTable::compile` rejects a size other
  than 0 or 256 with `error = name + ".texture: must be 16x16 (256 texels)"` (and
  `.crust_texture: …`), matching the `owner.field: …` style already used there. `spawn_cell` (7-arg) sets
  `shade = def.texture_rgba.empty() ? hash5(seed, 0, wx, wy, 0) & 0xFF : texel_index(tx, ty)`.

- [ ] **Step 4: Run** `./build.sh --test` — Expected: all pass, including
  "classic caves are unchanged".

- [ ] **Step 5: Commit**

```bash
git add src/core/cell.h src/core/material.h src/core/material.cpp src/core/generator.h src/core/generator.cpp tests/core/test_material.cpp
git commit -m "feat(core): material textures as texel shade, FLAG_CRUST"
```

### Task 2: Bodies sample texture in body-local coords

**Files:**
- Modify: `src/core/physics.cpp:524` (`PhysicsWorld::stamp`)
- Test: `tests/core/test_physics.cpp`, `tests/core/physics_test_util.h` (texture on wood if needed)

**Interfaces:**
- Consumes: 7-arg `spawn_cell` from Task 1.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("a falling body keeps its texture") {
	// Test world whose "wood" has a 256-word texture. Add a 12x12 box body, stamp, record
	// shade of the grid cell under body pixel (px, py) = (3, 4): expect texel_index(3, 4).
	// Step physics until the body has moved >= 5 cells, stamp again, find the cell under the
	// same body pixel: shade still texel_index(3, 4).
}
```

- [ ] **Step 2: Run** `./build.sh --test` — Expected: FAIL (shade follows world coords).
- [ ] **Step 3: Implement.** In `stamp`, `spawn_cell(mat, mt, grid.seed(), wx, wy, px, py)`.
- [ ] **Step 4: Run** `./build.sh --test` — Expected: PASS.
- [ ] **Step 5: Commit** — `feat(physics): body pixels keep their texel as the body moves`

### Task 3: Padded generation, pass list, crust pass

**Files:**
- Create: `src/core/gen_pass.h` (`GenRegion`, `GenPass`)
- Create: `src/core/crust_pass.h`, `src/core/crust_pass.cpp`
- Modify: `src/core/terrain.h`, `src/core/terrain.cpp` (`TerrainConfig` crust fields, split
  `generate`, own passes, compile-time pass setup)
- Test: `tests/core/test_terrain.cpp`, `tests/core/terrain_test_util.h`

**Interfaces:**
- Consumes: `FLAG_CRUST`, `CompiledMaterial::crust_rgba` (Task 1).
- Produces:

```cpp
// gen_pass.h
// A padded square of resolved terrain: world cells [x0, x0 + size) x [y0, y0 + size).
struct GenRegion {
	int x0, y0, size;
	std::vector<MatId> mat;      // size*size, row-major
	std::vector<uint8_t> flags;  // OR-ed into the emitted cells
	MatId mat_at(int wx, int wy) const;
	uint8_t& flags_at(int wx, int wy);
};

// One generation step that may look at neighbouring chunks. A pass must leave correct
// results in every cell at least margin() in from the region edge; nearer cells may be wrong.
// The region margin is the sum of all passes' margins.
class GenPass {
public:
	virtual ~GenPass() = default;
	virtual int margin() const = 0;
	virtual void apply(GenRegion&) const = 0;
};
```

- Produces: `TerrainConfig::crust_chance` (`float`, 0.3f), `TerrainConfig::crust_radius`
  (`int`, 6); `class CrustPass : public GenPass` with
  `CrustPass(const MaterialTable&, uint32_t seed, float chance, int radius)`;
  `int TerrainGenerator::region_margin() const`.

- [ ] **Step 1: Pin today's output.** Add a test that hashes (FNV-1a 64, as in "classic caves are
  unchanged") `mat`, `shade` and `flags` of every cell of `make_chunk` over chunks
  `cx in [-3, 3), cy in [-1, 5)` for `starter_config()` *and* a config with one room (reuse
  `test_rooms.cpp`'s room helper). Run it on the current code, paste the two numbers into the
  test as `CHECK(hash == 0x…)`. Name: `"generate output is unchanged without crust"`.

- [ ] **Step 2: Run** `./build.sh --test` — Expected: PASS (it pins the current code).

- [ ] **Step 3: Write the failing crust tests.** In `terrain_test_util.h` add
  `crust_table()` = `terrain_table()` but `rock` gets a 256-word `texture_rgba` and `crust_rgba`
  and `sand` gets only `texture_rgba`. Tests:

```cpp
TEST_CASE("crust is seamless across chunk borders") {
	// starter_config(), crust_table(). For chunks (cx, cy), cx in [-2, 2), cy in [0, 4):
	// FLAG_CRUST of each cell must equal crust_reference(gen, wx, wy) — a test-only helper that
	// builds one GenRegion covering the whole 4x4-chunk area plus region_margin() and runs the
	// passes once. CHECK some crust cell lies within 3 cells of a chunk border on each side.
}
TEST_CASE("crust is deterministic per seed") { /* two generators, same seed: memcmp equal; other seed: differs */ }
TEST_CASE("crust only on crust materials next to open space") {
	// Over the same area: every FLAG_CRUST cell is rock; no FLAG_CRUST on air, water, sand.
	// Every FLAG_CRUST cell is within ceil(crust_radius*1.3 + 2) of an edge rock cell.
	// Some rock edge cell facing water has crust, some facing sky (wy < surface_height) has crust.
	// No rock cell whose only open neighbours are sand counts as an edge (build it with a
	// hand-made GenRegion and run CrustPass directly).
}
TEST_CASE("no passes without a crust material") {
	// terrain_table() (no crust) -> region_margin() == 0; crust_chance = 0 -> region_margin() == 0.
}
TEST_CASE("crust settings are validated") {
	// crust_chance < 0 or > 1, crust_radius < 1 or > 32 -> compile fails naming the field.
}
```

- [ ] **Step 4: Run** `./build.sh --test` — Expected: compile failure.

- [ ] **Step 5: Implement the split.** `generate()` = resolve into a `GenRegion` of size
  `CHUNK + 2 * region_margin()`, run `passes_` in order, emit the centre via `spawn_cell` with
  `flags |= region.flags`. The per-cell body moves unchanged into the resolve step. Light markers
  and `biome_of` are recorded only for centre cells; back wall fill is unchanged. Rooms: look up
  each distinct room placement cell the region touches once (`room_in_cell`), since the padded
  region may cross placement-cell borders. `compile()` appends a `CrustPass` iff
  `crust_chance > 0` and some material has a non-empty `crust_rgba`; `region_margin()` = sum.

- [ ] **Step 6: Implement `CrustPass`.** `SALT_CRUST = 0x70u` (unused in `src/core`; 0x65 is taken by back_wall), plus a `FastNoiseLite` for the blob edge (value noise, frequency `0.4f`, seeded via
  `hash5(seed, 0, 0, 0, SALT_CRUST)`). `margin() = reach + 1` where
  `reach = (int)std::ceil(radius * 1.3f + 2.0f)` (11 for radius 6 → 86×86 region; the spec's "10" forgot the ±2 noise term).

```
for each cell c=(wx,wy) with 1 cell of region border around it:
  if c is not Solid-with-crust_rgba, or no 4-neighbour of class Empty/Liquid/Gas: continue
  h = hash5(seed, 0, wx, wy, SALT_CRUST)
  if hash_unit(h) >= chance: continue
  r = radius * (0.7 + 0.6 * hash_unit(hash5(seed, 1, wx, wy, SALT_CRUST)))
  for (nx,ny) within reach of c and inside the region:
    if (nx,ny) is Solid with crust_rgba and hypot(nx-wx, ny-wy) + noise(nx,ny) * 2 < r:
      flags_at(nx,ny) |= FLAG_CRUST
```

- [ ] **Step 7: Run** `./build.sh --test` — Expected: all pass, including Step 1's pinned hashes
  and "chunk is independent of load order and threads".

- [ ] **Step 8: Commit** — `feat(worldgen): padded generation with passes; crust pass`

### Task 4: Godot side — textures on PixelMaterial, colour table, generator settings

**Files:**
- Modify: `src/godot/pixel_material.h`, `src/godot/pixel_material.cpp` (`texture`,
  `crust_texture` properties; `to_def`)
- Modify: `src/godot/pixel_world.h`, `src/godot/pixel_world.cpp` (palette, `cell_rgba`,
  `cell_color`, `write_chunk_image`, new `get_color_at`)
- Modify: `src/godot/pixel_world_generator.h`, `src/godot/pixel_world_generator.cpp`
  (`crust_chance`, `crust_radius`)
- Modify: `build.sh:220` (allow the new expected error)
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Consumes: Task 1 fields and `FLAG_CRUST`; Task 3 `TerrainConfig::crust_chance/crust_radius`.
- Produces: `PixelMaterial.texture`, `PixelMaterial.crust_texture` (`Texture2D`);
  `PixelWorld.get_color_at(pos: Vector2i) -> Color` — the colour the chunk image draws for that
  cell (transparent if unloaded or air); `PixelWorldGenerator.crust_chance` (float, range 0..1),
  `crust_radius` (int, range 1..32).

- [ ] **Step 1: Write the failing tests** in `run_tests.gd`:

```gdscript
func test_material_texture_draws() -> bool:
	# Duplicate default_set; give a duplicated rock an ImageTexture 16x16 where texel (x, y) =
	# Color(x / 15.0, y / 15.0, 0.5). set_cell((5, 3), rock) and (-1, -1): get_color_at equals
	# the texel (5, 3) / (15, 15) within 1/255. Water at (6, 3) still equals water.color within
	# shade_amount. Mark the rock cell burning is not reachable from GDScript — skip.

func test_material_texture_wrong_size_falls_back() -> bool:
	# 8x8 texture on rock: world still loads; get_color_at of a rock cell is within
	# shade_amount of rock.color. Expects error "PixelMaterial 'rock': texture must be 16x16".

func test_generator_crust_properties() -> bool:
	# PixelWorldGenerator crust_chance / crust_radius round-trip; defaults 0.3 / 6.
```

- [ ] **Step 2: Run** `./build.sh --verify` — Expected: FAIL (`texture` property missing).

- [ ] **Step 3: Implement.** `to_def()`: if a texture is set, `get_image()`, decompress if
  needed, `convert(Image::FORMAT_RGBA8)`, require 16×16 else
  `push_error("PixelMaterial '<name>': texture must be 16x16, got WxH")` (same for
  `crust texture`) and leave the vector empty; pixels go in as `Color::to_rgba32()`.
  `cell_rgba` becomes `palette.size() * 2 * 2 * 256`, indexed
  `((mat * 2 + burning) * 2 + crust) * 256 + shade`. `cell_color(cell)` picks the base colour as:
  untextured → today's formula; textured → `(crust && crust non-empty ? crust : texture)[shade]`;
  then the existing burning tint. `PaletteEntry` gains the two vectors (or a pointer to the
  compiled material). `write_chunk_image` uses the new index; `FLAG_CRUST` is bit 2 so
  `crust = (flags >> 2) & 1`. `get_color_at` reads the cell and returns `cell_rgba` for it.
  Generator properties pass through `to_config`, with the range hints above.
  Add `^ERROR: PixelMaterial '.*': (crust )?texture must be 16x16` to `allowed_errors`.

- [ ] **Step 4: Run** `./build.sh --verify` — Expected: `ALL GODOT TESTS PASSED` (exit 139 after
  that line is the known exit segfault, not a failure).

- [ ] **Step 5: Commit** — `feat(godot): material textures and crust in the chunk colour table`

### Task 5: Ship the textures and assignments, check it in the game

**Files:**
- Create: `textures/blocks/{stone_generic,cobblestone,dirt,gravel,sandstone,sandstone_bricks,sand_ugly,grass_top,oak_planks}.png`
  (copied from `/Users/jeremyzhao/Development/godot/top-down-rogue/textures/Assets/db32/blocks/`)
- Modify: `materials/{rock,dirt,sandstone,sand,grass,wood}.tres`
- Modify: `bench/results/` (new result file)

- [ ] **Step 1: Copy the nine PNGs**, open the project once headless
  (`godot --headless --path . --import`) so `.import` files are generated, and set each import to
  lossless, no mipmaps, filter off (`compress/mode=0`, `mipmaps/generate=false`).

- [ ] **Step 2: Assign textures** exactly per the spec table:

  | Material  | texture         | crust_texture      |
  |-----------|-----------------|--------------------|
  | rock      | stone_generic   | cobblestone        |
  | dirt      | dirt            | gravel             |
  | sandstone | sandstone       | sandstone_bricks   |
  | sand      | sand_ugly       | —                  |
  | grass     | grass_top       | —                  |
  | wood      | oak_planks      | —                  |

- [ ] **Step 3: Run** `./build.sh --test --verify` — Expected: core all pass;
  `ALL GODOT TESTS PASSED` with no unexpected errors (the starter-set field test must still pass).

- [ ] **Step 4: Look at it.** Launch `demo/world_demo.tscn` (use the `run` skill), screenshot a
  cave wall, the sky surface and a dug tunnel. Expected: tiled textures, blob crust on generated
  edges with no straight cut-offs on 64-cell lines, no crust on the dug tunnel. Show the user.

- [ ] **Step 5: Bench.** Run the bench per `docs/bench-agent.md`, save as
  `bench/results/macos-m1-textures.json`. Expected: frame time within noise of
  `macos-m1-plan7.json`; generation time per chunk up to ~1.8×.

- [ ] **Step 6: Commit** — `feat: db32 textures and crust on the starter materials`
