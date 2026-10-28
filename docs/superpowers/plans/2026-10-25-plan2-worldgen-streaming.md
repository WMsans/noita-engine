# Plan 2 — World Generation and Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Plan 1's fixed box arena with an endless, Terraria-style generated world:
- a hilly surface with sky above,
- biomes by depth and by x, with smooth transitions between them,
- sideways caves,
- PNG rooms on a seeded grid.

It streams around the follow target in three zones (active, sleeping, discarded), and
generation runs on low-priority background threads.

**Architecture:**
- **Core (Godot-free):**
  - `TerrainGenerator`, a `ChunkGenerator` built from a `TerrainConfig` that names
    materials, the same compile pattern as `MaterialTable`.
  - `ChunkStreamer`, which owns 1–N generator threads. They always take the nearest
    still-requested chunk.
  - `Simulator` gains an optional active rect, so chunks outside it sleep.
  - `WorldGrid` gains `insert`, which wakes the touching borders of loaded neighbours.
- **Godot side:**
  - Four new Resources that compile into `TerrainConfig`.
  - `PixelWorld` gains a generator path. The null-generator path stays Plan 1's arena,
    unchanged.

**Tech Stack:** Godot 4.7.2, godot-cpp 10.0.0-stable, C++17, SCons, doctest 2.5.3, FastNoise
Lite v1.1.1 (MIT, single header).

**Spec:** `docs/superpowers/specs/2026-10-25-plan2-worldgen-streaming-design.md` (parent:
`docs/superpowers/specs/2026-10-25-noita-engine-design.md`). Read the design doc first; this
plan doesn't repeat its reasoning.

## Global Constraints

- Godot 4.7.2, godot-cpp 10.0.0-stable (`api_version=4.7`), C++17.
- `src/core/` never includes Godot headers.
- Targets: `macos` arm64 (M1, **this machine**) and `linux` x86_64 (Iris Xe, hand-off).
  Windows is not a target.
- Chunk size 64×64, fixed tick 60 Hz.
- The sim's `JobPool` keeps `min(4, hardware_concurrency - 1)` workers. Generator threads
  are separate: `generator_threads` (default 2, minimum 1) at low OS priority.
  - macOS: `pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0)`.
  - Linux: `setpriority(PRIO_PROCESS, gettid(), 10)`.
- **Determinism:** a chunk is a pure function of (world seed, coordinates). It's identical
  regardless of load order and generator thread count, within one build. Cross-platform
  bit-equality is **not** required.
- Every noise call passes `double` coordinates (FastNoise Lite's `GetNoise` is templated
  on the coordinate type), so terrain stays precise far from the origin.
- No persistence. A discarded chunk regenerates from the seed. Discarding goes through one
  function, `PixelWorld::evict(ChunkCoord)`, the future save hook.
- The null-generator (`arena`) path behaves exactly as in Plan 1:
  - `load_radius`, synchronous load,
  - everything active,
  - no streamer.
- Zone defaults: `active_radius = (8, 5)`, `loaded_margin = 3`, `discard_hysteresis = 2`
  chunks; `max_inserts_per_frame = 16`.
- Bench gate, `bench_world` (`budget_pass` requires all of):
  - `sim_ms_mean ≤ 6.0`
  - `stream_ms_mean ≤ 1.0` and `stream_ms_p99 ≤ 3.0`
  - `frame_ms_p99 ≤ 16.6`
  - `view_missing_frames == 0`
  - `cells_updated_mean ≥ 54192`, which is 50% of Plan 1's M1 figure of 108383.9
- The build works natively with `scons platform=macos arch=arm64 target=template_debug|template_release`
  and `scons platform=linux target=...`; the tests with `scons tests` and `scons tests tsan=yes`.
  `./build.sh --test --verify` runs everything on this machine.

## Review Focus

1. **Teleporting or freeing the follow target.** Moving the target 100 000 cells away must
   not crash or block a frame. The new area reads as solid until it streams in,
   `is_area_ready` goes false then true, and the old area is discarded. Freeing the target
   node falls back to the origin. (Test in Task 6.)
2. **Bad live zone values.** Negative `active_radius`, `loaded_margin` or
   `discard_hysteresis` are clamped to 0 when read each frame, with no crash and no
   unbounded allocation. (Test in Task 6.)
3. **Negative coordinates in generation.** Placement cells, regions and bands left of or
   above the origin use `floor_div`. A room in placement cell (−1, 2) that crosses a chunk
   border comes out identical in both load orders. (Test in Task 4.)
4. **Freeing the world mid-stream.** Freeing a `PixelWorld` (a scene change) while
   generator threads have work in flight joins cleanly, with no use-after-free of the
   generator or the table. (Tests in Task 2 and Task 6.)
5. **Room resources with bad images.** A `PixelRoom` with no image gives a `validate()`
   error naming the room's index. So does an image whose colours aren't in the palette,
   with the pixel and colour named. An RGB8 image (no alpha) is converted, not rejected.
   (Tests in Task 5.)

---

## File Structure

```
thirdparty/FastNoiseLite/FastNoiseLite.h   # vendored v1.1.1 (Cpp/FastNoiseLite.h), MIT
SConstruct                                 # + thirdparty/ on the extension's CPPPATH
src/core/coords.h                          # + ChunkRect
src/core/generator.h / .cpp                # + make_chunk(): generate a fresh heap Chunk
src/core/world_grid.h / .cpp               # + insert() with neighbour border wake, size()
src/core/simulator.h / .cpp                # + optional active rect (sleeping), SimStats::chunks_active
src/core/chunk_streamer.h / .cpp           # NEW: generator threads, request / take_ready / wait_idle
src/core/terrain.h / .cpp                  # NEW: TerrainConfig, biome weights, fields, rooms, TerrainGenerator
src/godot/pixel_biome.h / .cpp             # NEW Resource -> BiomeDef
src/godot/pixel_room.h / .cpp              # NEW Resource -> RoomDef (image -> RGBA8)
src/godot/pixel_room_palette.h / .cpp      # NEW Resource -> palette entries
src/godot/pixel_world_generator.h / .cpp   # NEW Resource -> TerrainConfig -> TerrainGenerator
src/godot/pixel_world.h / .cpp             # generator path: zones, streamer, evict, stats
src/godot/register_types.cpp
tests/core/test_streaming.cpp              # sleeping, insert, ChunkStreamer
tests/core/test_terrain.cpp                # surface, biomes, transitions, fields, determinism, config errors
tests/core/test_rooms.cpp                  # palette, placement, anchors, border crossing
tests/godot/run_tests.gd                   # + resource and generated-world tests
materials/dirt.tres, materials/grass.tres  # appended to default_set.tres (ids 11, 12)
worldgen/*.tres                            # 5 biomes + default_generator.tres
rooms/*.png, rooms/*.tres, rooms/palette.tres
demo/world_demo.tscn                       # generated-world fly-through (reuses paint_demo.gd)
bench/bench_world.tscn / bench_world.gd    # streaming benchmark
docs/bench-agent.md                        # replaces docs/mac-agent.md; two platforms
```

---

### Task 1: Sleeping chunks and chunk insertion

**Files:**
- Modify: `src/core/coords.h`, `src/core/generator.h/.cpp`, `src/core/world_grid.h/.cpp`, `src/core/simulator.h/.cpp`
- Create: `tests/core/test_streaming.cpp`

**Interfaces:**
- Consumes: Plan 1 core (`Chunk`, `WorldGrid`, `Simulator`, `BoxGenerator`, `simtest::` helpers).
- Produces:
```cpp
// coords.h
struct ChunkRect {                    // inclusive; empty when x1 < x0 or y1 < y0
  int x0, y0, x1, y1;
  bool contains(ChunkCoord c) const;
  static ChunkRect around(ChunkCoord centre, int rx, int ry);  // [centre-r, centre+r]
};
// generator.h — fresh heap chunk, coord set, cells generated, dirty rects untouched
std::unique_ptr<Chunk> make_chunk(ChunkCoord coord, const ChunkGenerator& gen, uint32_t seed);
// world_grid.h
Chunk* insert(std::unique_ptr<Chunk> chunk);  // main thread, between ticks
size_t size() const;
// simulator.h
struct SimStats { int chunks_simulated, cells_updated, chunks_active; double sim_ms; };
void Simulator::set_active_rect(std::optional<ChunkRect> rect);  // nullopt (default) = all active
```
- `insert` behaviour:
  - If the coord is already loaded, the existing chunk wins, the argument is dropped, and
    the existing one is returned.
  - Otherwise it marks the new chunk's whole `next` dirty (as `load` does today) and sets
    `render_dirty`.
  - For each **loaded 8-neighbour**, it `mark_next`s the cells of that neighbour that touch
    the new chunk: the edge column for a left / right neighbour, the edge row for an
    above / below neighbour, and the one corner cell for a diagonal neighbour.
  - `load` becomes `insert(make_chunk(...))`.
- Sleeping, in `Simulator::tick`'s swap pass:
  - A chunk outside the active rect gets `cur.reset()` and **keeps** `next`, so marks keep
    accumulating.
  - Active chunks swap as before.
  - `chunks_active` counts the chunks inside the rect.
  - Because sleeping chunks have an empty `cur`, the existing `cur.empty()` checks skip
    them in both the single-threaded and phased paths.

- [ ] **Step 1: Write failing tests** in `tests/core/test_streaming.cpp` (use `simtest::movement_table()` and a `BoxGenerator`):
  - `chunk outside the active rect sleeps and keeps its marks`:
    - Load chunks (0,0) and (1,0) as all air, with an arena covering both.
    - Settle with 2 ticks.
    - Place one sand cell in (1,0) at local (10,10) with `grid.set`.
    - `set_active_rect(ChunkRect{0,0,0,0})`, then tick 20 times. The sand is still at
      local (10,10).
    - `set_active_rect(std::nullopt)`, then tick 20 times. The sand has moved down.
  - `active rect stats`: with 4 chunks loaded and the rect covering 1, `last_stats().chunks_active == 1`.
  - `insert returns the existing chunk on a duplicate`: insert (0,0) twice. The second call returns the first pointer, and `size() == 1`.
  - `insert wakes the touching border of loaded neighbours` (`DirtyRect` is one bounding
    box, so each case gets a fresh grid):
    - Load (0,0) and tick until quiet (`chunks_simulated == 0`).
    - Insert (1,0). Chunk (0,0)'s `next` is exactly `{x0=63, x1=63, y0=0, y1=63}`.
    - Fresh grid, same setup, insert (0,1). `next` is exactly `{x0=0, x1=63, y0=63, y1=63}`.
    - Fresh grid, same setup, insert (1,1). `next` is exactly the single cell (63, 63).
  - `water held by an unloaded neighbour flows in once it loads`:
    - An arena covering x [0,128) and y [0,64), so chunks (0,0) and (1,0) are air inside.
    - Load only (0,0), and fill local column x = 63, y 40..63 with water.
    - Tick until quiet. The water stays in (0,0), because (1,0) is unloaded and acts solid.
    - Insert (1,0) with `make_chunk`, then tick 60 times. At least one water cell is now in
      chunk (1,0), and the total water count is unchanged.
- [ ] **Step 2:** `scons tests && build/tests/run_tests`. Expected: the new cases fail to compile or fail.
- [ ] **Step 3:** Implement `ChunkRect`, `make_chunk`, `insert` (and rewrite `load` on top of it), `size`, `set_active_rect` and the sleeping swap.
- [ ] **Step 4:** `scons tests && build/tests/run_tests`. Expected: all pass, including Plan 1's 41 cases.
- [ ] **Step 5:** Commit: `feat(core): sleeping chunks outside an active rect, chunk insert waking neighbour borders`.

---

### Task 2: ChunkStreamer — background generator threads

**Files:**
- Create: `src/core/chunk_streamer.h/.cpp`
- Modify: `tests/core/test_streaming.cpp`

**Interfaces:**
- Consumes: `make_chunk`, `ChunkGenerator`, `ChunkCoord`.
- Produces:
```cpp
struct StreamStats { int generated = 0; double gen_ms_total = 0; };
class ChunkStreamer {
 public:
  // threads < 1 is treated as 1. Each worker lowers its own OS priority on start (see
  // Global Constraints). `gen` must outlive the streamer and be safe for concurrent generate().
  ChunkStreamer(const ChunkGenerator& gen, uint32_t seed, int threads);
  ~ChunkStreamer();  // stops and joins; in-flight work finishes and is discarded
  // Main thread. Replaces the whole request: `missing` are coords the caller wants and hasn't
  // loaded. Workers take the request entry nearest to `centre` (squared distance in chunks)
  // that is neither in flight nor ready.
  void request(const std::vector<ChunkCoord>& missing, ChunkCoord centre);
  // Main thread. Up to `max` finished chunks that are in the latest request; finished chunks
  // no longer requested are dropped here.
  std::vector<std::unique_ptr<Chunk>> take_ready(int max);
  void wait_idle();   // blocks until every requested coord is ready (none queued or in flight)
  int pending() const;          // requested coords not yet ready (queued + in flight)
  StreamStats take_stats();     // since the previous call, then reset
};
```
- Implementation: one mutex plus one condition variable. The workers' pick is a linear
  scan of the request. Mark it with
  `// ponytail: linear nearest-pick over <= ~400 coords; a priority structure if it shows up in a profile`.
  A coord delivered by `take_ready` is removed from the request, so it isn't generated
  again even if the caller requests it before inserting it.

- [ ] **Step 1: Write failing tests** in `test_streaming.cpp`. The test generator `RecordingGen` fills a chunk with a material derived from its coord and appends each coord to a mutex-guarded vector.
  - `streamer delivers every requested chunk`:
    - 4 threads, request a 9×9 block.
    - Call `wait_idle()`, then drain `take_ready(1000)`.
    - Exactly 81 distinct coords come back, each chunk's `coord` matches its contents, and
      `pending() == 0`.
  - `streamer generates nearest first`:
    - 1 thread, request a 15×15 block around (100, -40) with centre (100, -40), then
      `wait_idle`.
    - In `RecordingGen`'s order, the squared distance to the centre never decreases.
  - `streamer drops chunks no longer requested`:
    - Request block A, `wait_idle`, then request block B, which shares no coords with A.
    - `wait_idle`, then drain: no coord from A comes back.
  - `streamer does not regenerate a delivered chunk`: request {(0,0)}, `wait_idle`, take it, request {(0,0)} again, `wait_idle`. `RecordingGen` recorded (0,0) exactly once.
  - `streamer shuts down with work in flight`:
    - A generator that sleeps 5 ms per chunk, 2 threads.
    - Request 200 coords and destroy the streamer immediately.
    - It returns within 1 s, and TSan reports nothing (Step 4).
  - `take_stats counts generated chunks`: after the 81-chunk test, `generated == 81` and `gen_ms_total > 0`. A second call returns zeros.
- [ ] **Step 2:** `scons tests && build/tests/run_tests`. Expected: FAIL.
- [ ] **Step 3:** Implement `ChunkStreamer`, including the platform priority call (`#ifdef __APPLE__` `<pthread/qos.h>`; `#ifdef __linux__` `<sys/resource.h>`, `<sys/syscall.h>`). Ignore its return value.
- [ ] **Step 4:** `scons tests && build/tests/run_tests`, then `scons tests tsan=yes && build/tests/run_tests`. Expected: PASS, no TSan reports.
- [ ] **Step 5:** Commit: `feat(core): ChunkStreamer generator threads, nearest-first at low OS priority`.

---

### Task 3: TerrainGenerator — surface, biome weights, shared fields

**Files:**
- Create: `thirdparty/FastNoiseLite/FastNoiseLite.h` (from tag `v1.1.1`, `Cpp/FastNoiseLite.h`), `src/core/terrain.h/.cpp`, `tests/core/test_terrain.cpp`, `tests/core/terrain_test_util.h`
- Modify: `SConstruct` (add `"thirdparty/"` to the extension env's `CPPPATH`; `tests_env` already has it)

**Interfaces:**
- Consumes: `ChunkGenerator`, `spawn_cell`, `MaterialTable`, `hash5`/`hash_unit`, `floor_div`, `ChunkStreamer` (in the determinism test).
- Produces (`terrain.h`). Task 4 adds the room parts of `TerrainConfig`'s behaviour; the
  fields are declared here:
```cpp
struct BiomeDef {
  std::string name;
  int min_depth = 0, max_depth = -1;          // [min, max) cells below the local surface; -1 = open
  float weight = 1.0f;                        // pick weight among its band's biomes
  std::string surface_material, base_material, pocket_material, liquid_material;  // "" = none; base required
  int surface_depth = 0;
  // "Amounts" in [0, 1]: 0 = never, larger = more. Blended by biome weight per cell.
  float pocket_threshold = 0, tunnel_threshold = 0, cavern_threshold = 0, liquid_threshold = 0;
};
enum class PaletteKind : uint8_t { Material, Keep, MarkerLight, MarkerBody };
struct PaletteEntry { uint32_t rgba; PaletteKind kind; std::string material; };  // rgba = 0xRRGGBBAA
enum class RoomAnchor : uint8_t { Surface, Embedded, Cave };
struct RoomDef {
  std::string name; int width = 0, height = 0; std::vector<uint32_t> rgba;  // row-major 0xRRGGBBAA
  float weight = 1.0f; RoomAnchor anchor = RoomAnchor::Embedded; std::vector<std::string> biomes;  // empty = any
};
struct TerrainConfig {
  int surface_y = 0; float surface_amplitude = 48, surface_frequency = 0.004f;
  int band_warp = 24, region_width = 768, region_warp = 96, region_transition = 96, band_transition = 32;
  float tunnel_frequency = 0.012f, cave_stretch = 2.5f, cavern_frequency = 0.006f;
  float pocket_frequency = 0.03f, pool_frequency = 0.02f;
  int cave_taper_depth = 40; float entrance_frequency = 0.002f, entrance_threshold = 0.75f;  // > 1 disables entrances
  int room_cell_size = 256; float room_chance = 0.25f;
  std::vector<BiomeDef> biomes; std::vector<PaletteEntry> palette; std::vector<RoomDef> rooms;
};
struct BiomeWeights { int count = 0; int biome[4]; float weight[4]; };  // indices into config.biomes; sum 1
class TerrainGenerator : public ChunkGenerator {
 public:
  // nullptr + `error` naming the culprit on any config problem (see Step 1). `seed` seeds every
  // noise field (via hash5 with a fixed salt per field); generate() must get the same seed.
  static std::unique_ptr<TerrainGenerator> compile(const TerrainConfig&, const MaterialTable&,
                                                   uint32_t seed, std::string& error);
  void generate(ChunkCoord coord, Chunk& out, uint32_t seed) const override;
  int surface_height(int wx) const;              // h(wx): first ground row
  BiomeWeights weights_at(int wx, int wy) const; // count 0 above the surface
  int biome_at(int wx, int wy) const;            // dithered pick; -1 above the surface
  bool cave_at(int wx, int wy) const;            // tunnel || cavern, thresholds blended, taper applied
};
```
- Field semantics. Every noise value `n` is in [−1, 1]; `amount` is the weight-blended
  threshold, and `taper` is the depth taper below.
  - **Tunnel:** air when `|n_tunnel(x, y * cave_stretch)| < tunnel_amount * taper`.
  - **Cavern:** air when `n_cavern > 1 − 2 * cavern_amount * taper`.
  - **Pocket:** pocket material when `n_pocket > 1 − 2 * pocket_amount`.
  - **Pool:** in cave air, liquid when `n_pool > 1 − 2 * liquid_amount`.
  - **Taper:** `clamp(depth / cave_taper_depth, 0, 1)`, forced to 1 where
    `n_entrance(x) > entrance_threshold`.
  - One FastNoiseLite object per field, built once in `compile`, read-only afterwards.
- Layout:
  - **Bands** group biomes with an identical `[min_depth, max_depth)`, sorted by
    `min_depth`. The band's warped depth is `depth + band_warp * n_band(x, y)`.
  - **Regions** split each band along `x + region_warp * n_region(x)`, at multiples of
    `region_width`, with a per-band offset taken from `hash5(seed, 0, band, 0, salt) % region_width`.
  - A region's biome is a weighted hash pick among its band's biomes:
    `hash5(seed, 0, band, region, salt)`.
  - **Weights.** Within `T/2` of a border, the two sides blend with `smoothstep` over the
    signed distance, where T is `region_transition` or `band_transition`. `T = 0` is a
    hard edge. The x weights within each band are multiplied by the band's depth weight.
    The same biome appearing twice merges into one entry.
- Cell order inside `generate`:
  1. air above `h`;
  2. the cell's picked biome;
  3. the surface material for the top `round(Σw·surface_depth)` ground cells, otherwise the
     base material or the pocket material;
  4. cave air;
  5. the pool liquid;
  6. rooms (Task 4);
  7. the cell spawned with `spawn_cell`.

  Fast path: when a whole column segment has a single biome, skip the per-cell weight work.

- [ ] **Step 1: Write failing tests** in `tests/core/test_terrain.cpp`. Helpers live in `tests/core/terrain_test_util.h` (Task 4 reuses them). A helper `terrain_table()` compiles materials `rock, dirt, sand, grass, water, oil, lava` (classes as in the starter set). A helper `config(...)` builds a `TerrainConfig` with defaults and given biomes.
  - `surface splits air from ground`:
    - One biome on `[0, -1)` (base dirt, surface grass, depth 3), all amounts 0, no rooms.
    - For every x in [−2000, 2000) step 7: the cell at `h−1` is air, `h .. h+2` is grass
      and `h+3` is dirt.
    - Check through `generate` on the owning chunks, compared with `surface_height`.
  - `bands and regions pick the expected biome`:
    - Bands A `[0,50)`, B and C `[50,200)`, D `[200,-1)`.
    - `band_warp = 0`, `region_warp = 0`, transitions 0.
    - Depth 10 is A, depth 1000 is D, and depth 100 is B or C, with both seen across 40
      regions.
  - `weights sum to 1, at most 4 biomes`: with transitions 96 / 32 and warps on, at 20 000 hash-chosen points below the surface, `count` is in 1..4 and `|Σw − 1| < 1e-5`.
  - `transitions are gradual`:
    - One band; sand biome S and dirt biome D, weight 1 each.
    - `region_warp = 0`, `region_transition = 128`, all amounts 0.
    - Scan x until `weights_at` reports S and D together. That's a border b. Then take
      8-wide slices across `[b−80, b+80)` over depths 10..200.
    - The fraction of S cells in each slice changes by at most 0.25 from the previous slice.
    - The first slice is exactly 1 (or 0), and the last is exactly the other.
  - `biomes do not own the shape`:
    - Config 1: one biome with tunnel 0.12, cavern 0.3.
    - Config 2: the same, split into two biomes that differ only in materials, with
      transitions on.
    - `cave_at` is identical for every cell in x [−1024, 1024), depth [0, 512).
  - `no caves at the surface row`: with `entrance_threshold = 2` (entrances off), `cave_at(x, surface_height(x))` is false for every x in [−4000, 4000). The taper is 0 at depth 0.
  - `chunk is independent of load order and threads`:
    - The starter-like 5-biome config.
    - Generate the 6×6 chunks around (3, 2) alone in raster order, and again in reverse
      order after first generating 30 unrelated chunks.
    - Generate them once more through a `ChunkStreamer` with 1 thread and with 4 threads.
    - The cell arrays are `memcmp`-equal across all four.
  - `far from the origin terrain still varies`: in chunk (156250, 3), which is x = 10 000 000, the cells aren't all one material and `cave_at` takes both values.
  - Config errors. `compile` returns nullptr, and the error contains the quoted text:
    - unknown material in a biome: the biome's name and the material's name;
    - empty `base_material`: the biome name;
    - a band gap `[0,100)` then `[150,-1)`: "100" and "150";
    - a first band not starting at 0: its `min_depth`;
    - the last band not open: "open";
    - `region_transition > region_width / 2`: "region_transition";
    - `band_transition` over half the thinnest band: "band_transition";
    - no biomes: "biome".
- [ ] **Step 2:** `scons tests && build/tests/run_tests`. Expected: FAIL.
- [ ] **Step 3:** Vendor FastNoiseLite, update `SConstruct`, and implement `terrain.h/.cpp` (rooms ignored for now). Noise types are the builder's choice within the field semantics above. A suggestion: OpenSimplex2 fractal for the surface and fields, and ridged fractal for tunnels.
- [ ] **Step 4:** `scons tests && build/tests/run_tests` and `scons tests tsan=yes && build/tests/run_tests`. Expected: PASS.
- [ ] **Step 5:** Commit: `feat(core): TerrainGenerator with surface, blended biome bands and regions, shared cave fields`.

---

### Task 4: Rooms — palette, placement, anchors

**Files:**
- Modify: `src/core/terrain.h/.cpp`
- Create: `tests/core/test_rooms.cpp`

**Interfaces:**
- Consumes: Task 3.
- Produces:
```cpp
struct RoomPlacement { int room; int x, y; };  // room index; world top-left
// The room the placement cell (px, py) holds, if any. Cells are room_cell_size square.
std::optional<RoomPlacement> TerrainGenerator::room_in_cell(int px, int py) const;
```
- Compile, on top of Task 3's checks:
  - Each palette entry's material resolves, with errors naming the colour as `#rrggbbaa`.
  - Each room pixel maps through the palette. Alpha 0 is always *keep*. A colour that
    isn't in the palette is an error naming the room, `(x, y)` and `#rrggbbaa`.
  - A room larger than `room_cell_size` is an error naming the room.
  - `room_cell_size` must be a positive multiple of 64 (**my call**), so every chunk lies in
    exactly one placement cell and consults only that one.
  - An unknown biome name in a room is an error naming the room and the biome.
  - Markers stamp air. *Keep* leaves the terrain as generated.
- Placement for cell (px, py):
  - `hash_unit(hash5(seed, 0, px, py, SALT_ROOM)) < room_chance`, or no room.
  - The candidates are the rooms whose biome list is empty or contains the
    **highest-weight** biome at the cell centre, picked by weight with a second hash.
  - The offset comes from further hashes, within `[0, S − w] × [0, S − h]`.
  - Then the anchor check below. A failed check means no room; there's no retry.
- Anchors:
  - **Surface:** `x` from the offset. `y = surface_height(x + w/2) − h + 1`, so the bottom row
    sits on the first ground row. Valid only if the room lies inside the cell vertically.
  - **Embedded:** valid only if `y > max(surface_height(x .. x+w−1))`.
  - **Cave:** valid only if `cave_at(x + w/2, y + h/2)`.

- [ ] **Step 1: Write failing tests** in `tests/core/test_rooms.cpp` (reuse `terrain_table()` / `config()` from `tests/core/terrain_test_util.h`):
  - `embedded room across chunk borders is order-independent`:
    - A 100×80 room: a rock border 3 cells thick, an air interior, a 10×10 transparent
      (*keep*) patch and one marker-light pixel.
    - `room_chance = 1`, one dirt biome, caves off.
    - Take `room_in_cell(0, 1)` (non-empty), and generate the chunks it covers in raster
      order and in reverse order, in two fresh grids.
    - Every cell in the room rect matches in both grids.
    - Material pixels have the room's material, and the marker pixel is air.
    - *Keep* pixels equal the same cell from a generator compiled with `room_chance = 0`.
  - `negative placement cell works the same`: the same check with cell (−1, 2) (Review Focus 3).
  - `surface room stands on the ground`: an anchor-Surface room (40×24), `room_chance = 1`. For every cell in py = −1 and py = 0 (px in −20..20) that holds a placement `p`, `p.y + 23 == surface_height(p.x + 20)`. At least 5 placements are found.
  - `cave room only where the cave field is open`: an anchor-Cave room with cavern amount 0.45. For 200 cells with a placement, `cave_at(x + w/2, y + h/2)` is true.
  - `room biome eligibility`: a room limited to `{"desert"}` in a two-biome world (desert, forest). Every placement found over 300 cells has `desert` as the highest-weight biome at the cell centre.
  - `placement is deterministic`: two generators compiled from the same config and seed give equal `room_in_cell` results for 500 cells.
  - Config errors. The error contains:
    - a room colour missing from the palette: the room name, `(3, 4)` and `#12345678`;
    - an oversized room (257×10 with S = 256): the room name;
    - `room_cell_size = 100`: "room_cell_size";
    - an unknown room biome: the room name and the biome name;
    - an unknown palette material: `#rrggbbaa` of that entry.
- [ ] **Step 2:** `scons tests && build/tests/run_tests`. Expected: FAIL.
- [ ] **Step 3:** Implement room compile, `room_in_cell`, and stamping in `generate`.
- [ ] **Step 4:** `scons tests && build/tests/run_tests`. Expected: PASS (all core cases).
- [ ] **Step 5:** Commit: `feat(core): pixel-scene rooms on a seeded placement grid with surface, embedded and cave anchors`.

---

### Task 5: Godot worldgen Resources and starter content

**Files:**
- Create: `src/godot/pixel_biome.h/.cpp`, `src/godot/pixel_room.h/.cpp`, `src/godot/pixel_room_palette.h/.cpp`, `src/godot/pixel_world_generator.h/.cpp`, `materials/dirt.tres`, `materials/grass.tres`, `worldgen/{forest,desert,underground,caverns,deep}.tres`, `worldgen/default_generator.tres`, `rooms/palette.tres`, `rooms/{hut,shrine,platform}.png` + `.tres`
- Modify: `src/godot/register_types.cpp`, `materials/default_set.tres`, `tests/godot/run_tests.gd`, `build.sh` (registered-classes message)

**Interfaces:**
- Consumes: `BiomeDef`, `PaletteEntry`, `RoomDef`, `TerrainConfig`, `TerrainGenerator::compile`, `PixelMaterialSet::build_table`.
- Produces (GDScript-visible). The bindings follow `PixelMaterial` / `PixelMaterialSet`
  style: `ADD_PROPERTY`, `_bind_methods`, and `BIND_ENUM_CONSTANT`.
  - `PixelBiome : Resource`, with exactly the `BiomeDef` fields:
    - `biome_name: StringName`; `name` clashes, as with `PixelMaterial`'s `material_name`
    - `min_depth`, `max_depth`
    - `weight`
    - `surface_material`, `base_material`, `pocket_material`, `liquid_material`
      (`StringName`)
    - `surface_depth`
    - `pocket_threshold`, `tunnel_threshold`, `cavern_threshold`, `liquid_threshold`

    C++: `BiomeDef to_def() const`.
  - `PixelRoomPalette : Resource`: `entries: Dictionary`, mapping `Color` to `StringName`.
    The values `&"@keep"`, `&"@light"` and `&"@body"` select the special kinds; anything
    else is a material name. C++: `std::vector<PaletteEntry> to_entries() const`, keyed by
    `Color::to_rgba32()`.
  - `PixelRoom : Resource`:
    - `image: Texture2D`
    - `weight: float = 1.0`
    - `anchor: int`, with enum `ANCHOR_SURFACE = 0, ANCHOR_EMBEDDED, ANCHOR_CAVE`
    - `biomes: PackedStringArray`

    C++: `bool to_def(int index, RoomDef& out, String& error) const`.
    - `name` is the image's file basename, or `"room <index>"`.
    - The image is read with `get_image()` and `convert(FORMAT_RGBA8)`. A null image is an
      error, `"room <index> has no image"`.
  - `PixelWorldGenerator : Resource`:
    - every numeric `TerrainConfig` field, same names and defaults
    - `biomes: Array[PixelBiome]`, `rooms: Array[PixelRoom]`, `room_palette: PixelRoomPalette`
    - Method: `validate(material_set: PixelMaterialSet) -> String`. Returns `""` when OK,
      otherwise the material-set error or the terrain compile error verbatim.
    - C++: `std::unique_ptr<TerrainGenerator> build(const MaterialTable&, uint32_t seed, String& error) const`.
- Starter content:
  - `dirt`: solid, `collision_solid`, density 2.0, colour about `(0.42, 0.29, 0.18)`,
    opacity 1.
  - `grass`: solid, `collision_solid`, density 1.5, colour about `(0.30, 0.55, 0.20)`,
    opacity 1.
  - Both are appended to `default_set.tres` after `smoke`, so they get ids 11 and 12.
  - Biomes, as in the design doc:
    - forest `[0,120)`
    - desert `[0,120)`
    - underground `[120,500)`
    - caverns `[500,1500)`
    - deep `[1500,-1)`
  - Thresholds are tuned by eye in Task 6's demo. Caves should read as sideways
    exploration.
  - Rooms:
    - `hut`: Surface, about 48×32, wood walls, air interior, forest only.
    - `shrine`: Embedded, about 96×72, crossing chunk borders, a rock shell with an air
      room, underground and caverns.
    - `platform`: Cave, about 70×24, a wood ledge with a water basin, caverns.
  - The PNGs are made with a one-off headless GDScript (`Image.create` + `save_png`), which
    isn't committed. They're imported lossless, with mipmaps and filter off.
  - `palette.tres` lists every colour the PNGs use.

- [ ] **Step 1: Write failing Godot tests** in `run_tests.gd`:
  - Update `test_default_set_validates` to expect 12 materials, and `test_starter_set_ids_and_fields`'s `expected` to end with `&"dirt", &"grass"`.
  - `test_default_generator_validates()`: `load("res://worldgen/default_generator.tres").validate(load("res://materials/default_set.tres")) == ""`.
  - `test_room_colour_missing_reported()`:
    - A 4×4 `Image` (RGBA8) with pixel (2,1) = `Color8(18, 52, 86, 255)`, wrapped by
      `ImageTexture.create_from_image`.
    - A palette without that colour, a generator holding the room plus the forest biome.
    - `validate()` contains `"(2, 1)"` and `"#123456ff"`.
  - `test_room_without_image_reported()`: a `PixelRoom` with `image = null` at index 0. `validate()` contains `"room 0 has no image"`.
  - `test_rgb8_room_image_accepted()`: the same 4×4 room as an `FORMAT_RGB8` image whose colours are all in the palette. `validate() == ""`.
- [ ] **Step 2:** `./build.sh --verify`. Expected: FAIL (classes missing).
- [ ] **Step 3:** Implement the four Resources, register them (each after its dependencies), and create the starter content.
- [ ] **Step 4:** `./build.sh --test --verify`. Expected: core `SUCCESS`, `ALL GODOT TESTS PASSED`.
- [ ] **Step 5:** Commit: `feat: worldgen resources (biome, room, palette, generator) with starter biomes and rooms`.

---

### Task 6: PixelWorld generator path — zones, streaming, evict, stats, demo

**Files:**
- Modify: `src/godot/pixel_world.h/.cpp`, `tests/godot/run_tests.gd`, `demo/paint_demo.gd`
- Create: `demo/world_demo.tscn`

**Interfaces:**
- Consumes: `ChunkStreamer`, `TerrainGenerator`, `PixelWorldGenerator::build`, `WorldGrid::insert`, `Simulator::set_active_rect`, `ChunkRect`.
- Produces (GDScript-visible, added to `PixelWorld`):
  - Properties:
    - `generator: PixelWorldGenerator`, read at `_ready`; null means the Plan 1 arena path.
    - `active_radius: Vector2i = (8, 5)`, `loaded_margin: int = 3`,
      `discard_hysteresis: int = 2`. These are live, and every component is clamped to ≥ 0
      on each read.
    - `generator_threads: int = 2`, `max_inserts_per_frame: int = 16`, read at `_ready`.
  - Method: `is_area_ready(rect: Rect2i) -> bool`. True when every chunk overlapping the
    world-cell rect is loaded. An empty rect is true.
  - `get_stats()` adds:
    - `chunks_active`
    - `gen_pending`
    - `gen_ms`: mean worker ms per chunk finished since the previous frame, 0 when none
    - `chunks_generated`: cumulative
    - `stream_ms`: this frame's insert + evict + first-upload time
    - `view_missing`

    On the arena path they read 0, except `chunks_active`, which equals `chunks_loaded`.
- Behaviour on the generator path:
  - **`_ready`:**
    1. Build the table and the generator, pushing an error and disabling the world on any
       failure.
    2. Create the `ChunkStreamer`. Declare it **after** the generator and table members, so
       it's destroyed first.
    3. Request only the active zone, call `wait_idle`, and insert everything.
  - **`_process`, in this order:**
    1. Compute the centre chunk from `target_cell()`.
    2. Build three rects with `ChunkRect::around`: active = `active_radius`; loaded =
       active + `loaded_margin`; discard = loaded + `discard_hysteresis`.
    3. `sim->set_active_rect(active)`.
    4. `streamer->request(missing coords in the loaded rect, centre)`.
    5. Insert `take_ready(max_inserts_per_frame)`, keeping only chunks inside the loaded
       rect.
    6. `evict` every loaded chunk outside the discard rect.
    7. `take_stats` into `gen_ms` / `chunks_generated`.
    8. Upload dirty chunks.
    9. Count `view_missing`: the chunks overlapping the viewport's visible rect mapped into
       this node's space (inverse canvas transform, then inverse global transform) that
       aren't loaded.
  - **`evict(ChunkCoord)`** is the single discard function: it unloads the grid chunk and
    drops its view. The arena path's unload calls it too. Comment it as the future
    persistence hook.
  - Teleports need nothing special: the streamer's request just moves.

- [ ] **Step 1: Write failing Godot tests.** A helper `_make_generated_world(target: Node2D)` sets `generator = load("res://worldgen/default_generator.tres")`, `worker_threads = 1`, `generator_threads = 2` and the follow target, then adds the world and awaits one frame. A helper `_await_ready(world, rect, max_frames := 600) -> bool` awaits frames until `is_area_ready(rect)`.
  - `test_generated_world_active_zone_ready()`: target at (0, 0). Right after the first frame, `is_area_ready(Rect2i(-8*64, -5*64, 17*64, 11*64))` is true, and `get_material_at(Vector2i(0, -500)) == &"air"` (sky).
  - `test_far_chunks_discarded()`:
    - Move the target to (20000, 0).
    - `_await_ready` on the view-sized rect around it.
    - Then await 2 more frames. `is_chunk_loaded(Vector2i(0, 0))` is false.
  - `test_regenerated_chunk_identical()`:
    - `simulating = false`.
    - Record `get_material_at` for all 4096 cells of chunk (0, 4).
    - Move the target to (20000, 0) and wait until chunk (0, 4) is unloaded.
    - Move it back to (0, 0) and `_await_ready` on chunk (0, 4)'s rect.
    - All 4096 cells are equal.
  - `test_teleport_and_freed_target()` (Review Focus 1):
    - Teleport the target to (100000, 3000).
    - Immediately, before any frame, `is_area_ready` for the target's chunk rect is false,
      and `get_material_at` there returns `&""`. That's deterministic, because nothing has
      been requested yet.
    - `_await_ready` returns true, and the origin chunk (0, 0) is no longer loaded.
    - Free the target, await 2 frames. There's no crash, and `get_stats()["tick"]` still
      advances.
  - `test_bad_zone_values_clamped()` (Review Focus 2): set `active_radius = Vector2i(-3, -3)`, `loaded_margin = -5`, `discard_hysteresis = -1` and await 3 frames. `get_stats()["chunks_loaded"] >= 1`, with no error or crash.
  - `test_free_world_while_streaming()` (Review Focus 4): create a generated world, move the target to (50000, 0), await 1 frame (work in flight), then `world.free()` and await 2 frames. The test reaches its end.
  - Extend `test_stats_keys` with the 6 new keys.
  - All Plan 1 tests stay unchanged and pass on the arena path.
- [ ] **Step 2:** `./build.sh --verify`. Expected: FAIL.
- [ ] **Step 3:** Implement the generator path.
- [ ] **Step 4:** `./build.sh --test --verify`. Expected: all pass. Then `./build.sh --tsan` to recheck the core under TSan.
- [ ] **Step 5: World demo.**
  - `demo/world_demo.tscn` instances the same `paint_demo.gd`, with a `PixelWorld` using
    `default_generator.tres` and the camera starting at (0, −60).
  - `paint_demo.gd` changes:
    - Hold Shift for 4× pan speed.
    - The label shows `chunks_active`, `gen_pending`, `gen_ms`, `stream_ms` and
      `view_missing`.
    - Keys 1–0 keep the 10 Plan 1 materials.
  - Run `godot --path . res://demo/world_demo.tscn`. Tune the starter biome thresholds and
    noise frequencies until, by eye:
    - hills meet sky,
    - caves run mostly sideways,
    - each depth band looks different,
    - forest↔desert borders show a ragged dithered fringe, not a line,
    - caves continue across biome borders,
    - all three rooms appear,
    - flying at 4× speed shows no holes (`view_missing` stays 0) and no regeneration on
      screen.

    Commit the tuned `.tres` values.
- [ ] **Step 6:** Commit: `feat(godot): PixelWorld generated-world streaming with active, sleeping and discard zones`.

---

### Task 7: Streaming benchmark, checkpoints, bench-agent doc

**Files:**
- Create: `bench/bench_world.tscn`, `bench/bench_world.gd`, `docs/bench-agent.md`, `bench/results/macos-m1-plan2.json`
- Delete: `docs/mac-agent.md` (its content moves into `docs/bench-agent.md`)

**Interfaces:**
- Consumes: the `PixelWorld` API from Task 6, and Plan 1's `bench.gd` (copy its argument
  parsing, `_mean`, `_p99`, `_write_json`, `_refuse` and warm-up handling; don't share code
  between the two scripts).
- Produces:
  - Command:
    `godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=<path>`.
    Needs a real window, with vsync off.
  - JSON keys:
    - Plan 1's keys
    - `stream_ms_mean`, `stream_ms_p99`
    - `gen_ms_mean`: over frames with `gen_ms > 0`
    - `chunks_generated`: final cumulative
    - `view_missing_frames`
    - `budget_pass`, as defined in Global Constraints
- Scene:
  - The `PixelWorld` uses `default_generator.tres`, `world_seed = 1337` and
    `simulating = false`, with one `step(1)` per frame as in Plan 1.
  - The camera follows a looping polyline at **10 cells per frame** (600 px/s at 60 fps,
    frame-locked, so it's deterministic and stricter at higher frame rates). The
    waypoints, as world cells:
    - A (0, −40)
    - B (2400, −40), along the surface
    - C (2400, 2000), the dive through every band
    - D (−600, 1400), the cavern run
    - E (−600, −40)
    - back to A, which revisits discarded ground
  - Every frame, paint the workload at offsets from the camera:
    - sand faucets 8×3 at (−150, −110) and (150, −110),
    - a water faucet 6×3 at (0, −120).
  - Every 30 frames, also `fill_circle` oil with radius 10 at (220, 40), and fire with
    radius 3 at its centre.
  - The builder raises the faucet sizes if `cells_updated_mean` lands under the gate.
- One summary line, as in Plan 1, plus the stream, gen and view-missing figures.

- [ ] **Step 1:** Implement `bench_world.gd` and the scene.
- [ ] **Step 2: Mac run (this machine).**
  - `./build.sh --release --test --verify`.
  - Then `godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/macos-m1-plan2.json`.
  - Expected: exit 0, the file exists, and `budget_pass: true`.
  - If `view_missing_frames > 0` or a stream gate fails, profile before relaxing anything.
    Likely fixes: `generator_threads` 3, a lower per-chunk generation cost (the
    single-biome fast path), or a lower `max_inserts_per_frame` with a wider
    `loaded_margin`.
  - Also re-run Plan 1's `bench.tscn` once. `sim_ms_mean` should stay within noise of
    3.53 ms; record it in the commit message.
- [ ] **Step 3:** Write `docs/bench-agent.md`. It's `docs/mac-agent.md` generalised to both platforms:
  - Build commands for macOS arm64 and Linux x86_64 (`./build.sh --release --test --verify` on either).
  - Expected test tails.
  - Both bench commands, with `--out=bench/results/<macos-m1|linux-irisxe>-<plan1|plan2>.json`.
  - What to commit.
  - What to report on failure: carry over Plan 1's list, plus the `bench_world` summary line.
  - A status section with this machine's Plan 2 numbers.
  - Update any references to `mac-agent.md` (`git grep mac-agent`).
- [ ] **Step 4:** Commit: `bench: world-streaming benchmark, macOS M1 Plan 2 results, two-platform bench-agent doc`.
- [ ] **Step 5: Linux checkpoint.**
  - Push the branch and hand off `docs/bench-agent.md` to the Linux machine.
  - Plan 2 is done only when `bench/results/linux-irisxe-plan1.json` and
    `linux-irisxe-plan2.json` are committed, both with `budget_pass: true`.
  - If Linux fails a stream gate but the Mac passes, look first at the nice-value call,
    and at `generator_threads` relative to the 20 hardware threads.
