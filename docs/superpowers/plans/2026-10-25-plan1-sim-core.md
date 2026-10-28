# Plan 1 — Falling-Sand Simulation Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A GDExtension that simulates a chunked, multithreaded falling-sand world with
data-defined chemistry. Godot draws it, game code can paint into it, and a benchmark
scene measures it on Linux and on the M1.

**Architecture:** A Godot-free C++ core (`src/core/`: cells, chunks, material tables,
simulator, thread pool), unit-tested with doctest, wrapped by thin Godot classes
(`src/godot/`: Resources for materials and reactions, and a `PixelWorld` node that owns
the core and draws chunks through RenderingServer). The simulation follows Noita's
model: 64×64 chunks, per-chunk dirty rectangles, and a four-phase checkerboard across
worker threads.

**Tech Stack:** Godot 4.7.2, godot-cpp 10.0.0-stable (`api_version=4.7`), C++17,
SCons, doctest 2.5.3.

**Spec:** `docs/superpowers/specs/2026-10-25-noita-engine-design.md`

## Roadmap (this is Plan 1 of 5)

The spec is split into plans that each ship working, testable software. Each later plan
is written after the previous one lands, so it can name real interfaces.

1. **Simulation core (this plan):** build, cells, chunks, movement, chemistry,
   threading, Godot resources, the `PixelWorld` node, rendering, benchmark.
2. **World generation and streaming:** zones (active, sleeping margin, discard),
   deterministic noise caves and biomes, pixel-scene rooms on a placement grid,
   generation on worker threads.
3. **Free particles and `PixelCharacter`:** the particle pool, a grid-collided
   character, powder pushing, liquid detection, and the remaining `PixelWorld` API
   (`explode`, `raycast`, `spawn_particles`).
4. **Rigid bodies:** Box2D v3.2.0 vendored, `PixelRigidBody`, per-chunk terrain chain
   shapes near awake bodies, stamp and unstamp, buoyancy.
5. **Lighting:** radiance cascades through `RenderingDevice` compute, `PixelLight`,
   bloom, darkness, GPU timings in the benchmark.

## Global Constraints

- Godot 4.7.2. godot-cpp **10.0.0-stable** as a git submodule at
  `thirdparty/godot-cpp`, built with `api_version=4.7`.
- C++17. `src/core/` must never include Godot headers. Only `src/godot/` may.
- Targets: `linux` x86_64 (this machine, Iris Xe, Vulkan) and `macos` arm64 (M1, Metal).
  **Windows is not a target.** Leave the D3D12 line in `project.godot` alone.
- Build commands that must work natively on each machine:
  `scons platform=linux target=template_debug` and
  `scons platform=macos arch=arm64 target=template_debug` (plus `target=template_release`).
- Library outputs: `bin/libnoita_engine.<platform>.<target>.<arch>.<ext>`, for example
  `bin/libnoita_engine.linux.template_release.x86_64.so`. Built binaries are not
  committed.
- Chunk size: **64×64 cells**. The fixed tick runs at **60 Hz**.
- Worker threads: default `min(4, hardware_concurrency - 1)`, at least 1. The M1 has 4
  performance cores.
- Benchmark budget gate for this plan: **mean simulation ≤ 6 ms per tick** on both the
  M1 and the Iris Xe in the benchmark scene.
- No terrain persistence. No Windows. No GDScript in hot paths.
- Every random decision in the simulation comes from a stateless hash of
  `(world_seed, tick, x, y, salt)`, never from a thread-local RNG, so results don't
  depend on the thread count.

## Review Focus

1. **Negative world coordinates.** `x = -1` must map to chunk `-1`, local `63`, not
   chunk `0`. Painting or simulating left of or above the origin must work. (Test in
   Task 3.)
2. **Writes to unloaded chunks.** `set_cell` or `fill_circle` overlapping unloaded
   chunks must skip those cells and return without crashing. During simulation, missing
   chunks act as solid. (Tests in Task 3 and Task 8.)
3. **Bad material data.** An unknown material name in `set_cell`, a `burns_into` that
   names a missing material, or a duplicate reaction pair must produce a clear error,
   not a crash or a silent wrong ID. (Tests in Task 2 and Task 7.)
4. **Thread count of 1 or an over-large value.** `worker_threads = 1` must run fully
   single-threaded, and results must be bit-identical to 4 threads. (Test in Task 6.)
5. **Long frames or hitches.** After a stall, the simulation must not run unbounded
   catch-up ticks. Cap it at 3 per frame through
   `physics/common/max_physics_steps_per_frame`. (Verification in Task 8.)

---

## File Structure

```
SConstruct                         # builds extension + `tests` target
.gitignore                         # + bin/*.so, bin/*.dylib, build/, .sconsign.dblite
thirdparty/.gdignore               # keep Godot from importing submodule assets
thirdparty/godot-cpp/              # submodule @ 10.0.0-stable
thirdparty/doctest/doctest.h       # vendored single header v2.5.3
src/.gdignore
src/core/coords.h                  # floor-div helpers, ChunkCoord, chunk key
src/core/cell.h                    # Cell (8 bytes), flag bits
src/core/rng.h                     # stateless hash RNG
src/core/material.h / .cpp         # MatClass, MaterialDef, ReactionDef, MaterialTable::compile
src/core/chunk.h / .cpp            # Chunk: cells, DirtyRect cur/next with spinlock
src/core/world_grid.h / .cpp       # chunk map, global get/set, Neighborhood (3x3 chunk cache)
src/core/generator.h / .cpp        # ChunkGenerator interface, BoxGenerator
src/core/sim_rules.h / .cpp        # per-cell movement + chemistry (operates on Neighborhood)
src/core/simulator.h / .cpp        # Simulator::tick — dirty swap, 4-phase checkerboard
src/core/job_pool.h / .cpp         # fixed worker pool, parallel_for
src/godot/register_types.cpp
src/godot/pixel_material.h / .cpp        # Resource
src/godot/pixel_reaction.h / .cpp        # Resource
src/godot/pixel_material_set.h / .cpp    # Resource, compile() -> MaterialTable
src/godot/pixel_world.h / .cpp           # Node2D: owns grid/sim/pool, API, rendering
tests/core/test_main.cpp           # doctest main
tests/core/test_*.cpp              # one file per core unit
tests/godot/run_tests.gd           # headless SceneTree test runner
bin/noita_engine.gdextension
materials/*.tres                   # starter materials + reactions + default set
demo/paint_demo.tscn / .gd         # mouse-painting visual check
bench/bench.tscn / bench.gd        # benchmark, writes JSON
bench/results/                     # committed benchmark results
docs/mac-agent.md                  # instructions for the Mac agent
```

---

### Task 1: Build scaffold, extension loads in Godot

**Files:**
- Create: `SConstruct`, `thirdparty/.gdignore`, `src/.gdignore`, `thirdparty/doctest/doctest.h`, `src/godot/register_types.cpp`, `src/godot/pixel_world.h/.cpp` (empty `Node2D` subclass), `bin/noita_engine.gdextension`, `tests/core/test_main.cpp`, `tests/core/test_smoke.cpp`, `tests/godot/run_tests.gd`
- Modify: `.gitignore`

**Interfaces:**
- Produces: entry symbol `noita_engine_library_init`. Class `PixelWorld : Node2D`.
  `scons tests` builds `build/tests/run_tests` from `src/core/*.cpp` + `tests/core/*.cpp`
  (no godot-cpp, `-O2 -g -pthread -std=c++17`). Headless runner:
  `godot --headless --path . -s tests/godot/run_tests.gd`. It exits 0 on success and 1 on
  any failure, and prints `ALL GODOT TESTS PASSED`.

- [ ] **Step 1:** `git submodule add https://github.com/godotengine/godot-cpp thirdparty/godot-cpp`, then check out tag `10.0.0-stable` inside it. Download doctest v2.5.3's `doctest/doctest.h` into `thirdparty/doctest/`. Add empty `.gdignore` files to `thirdparty/` and `src/`.
- [ ] **Step 2:** Write `SConstruct`, modelled on godot-cpp-template's but with the project at the repo root. Call `SConscript("thirdparty/godot-cpp/SConstruct", {"api_version": "4.7", "env": env, "customs": customs})`, use sources `Glob("src/core/*.cpp") + Glob("src/godot/*.cpp")`, and write the library to `bin/` with the name from Global Constraints. Strip `.dev`/`.universal` from the suffix but keep the arch. Add a `tests` alias that builds `build/tests/run_tests` with a fresh `Environment()` (no godot-cpp flags).
- [ ] **Step 3:** Write `bin/noita_engine.gdextension`: `entry_symbol = "noita_engine_library_init"`, `compatibility_minimum = "4.7"`, with libraries for `linux.debug.x86_64`, `linux.release.x86_64`, `macos.debug.arm64` and `macos.release.arm64` pointing to the output names.
- [ ] **Step 4:** Write `tests/core/test_smoke.cpp` with `TEST_CASE("smoke") { CHECK(1 + 1 == 2); }`, and `tests/godot/run_tests.gd`: an `extends SceneTree` runner that calls every `test_*` method on itself, counts failures, prints `ALL GODOT TESTS PASSED` or the failures, and `quit(0|1)`. Its first test is `test_extension_loaded()`, which asserts `ClassDB.class_exists("PixelWorld")`.
- [ ] **Step 5:** Run `scons platform=linux target=template_debug -j$(nproc)`. Expected: `bin/libnoita_engine.linux.template_debug.x86_64.so` exists.
- [ ] **Step 6:** Run `scons tests && build/tests/run_tests`. Expected: `[doctest] Status: SUCCESS!`
- [ ] **Step 7:** Run `godot --headless --path . --import` once, then `godot --headless --path . -s tests/godot/run_tests.gd`. Expected: `ALL GODOT TESTS PASSED`, exit 0.
- [ ] **Step 8:** Add `bin/*.so`, `bin/*.dylib`, `bin/*.framework`, `build/`, `.sconsign.dblite`, `src/**/*.os`, `src/**/*.o` to `.gitignore`. Commit: `build: scaffold godot-cpp 10 extension and test runners`.

---

### Task 2: Cells, coordinates, RNG, material tables

**Files:**
- Create: `src/core/coords.h`, `src/core/cell.h`, `src/core/rng.h`, `src/core/material.h`, `src/core/material.cpp`, `tests/core/test_material.cpp`, `tests/core/test_coords.cpp`

**Interfaces:**
- Produces:
```cpp
// coords.h
constexpr int CHUNK = 64;
inline int floor_div(int a, int b);   // floor_div(-1, 64) == -1
inline int floor_mod(int a, int b);   // floor_mod(-1, 64) == 63
struct ChunkCoord { int x, y; bool operator==(const ChunkCoord&) const; };
inline ChunkCoord chunk_of(int wx, int wy);
inline int64_t chunk_key(ChunkCoord c);  // ((int64)x << 32) | (uint32)y

// cell.h
using MatId = uint16_t;  constexpr MatId MAT_AIR = 0;
struct Cell {               // static_assert(sizeof(Cell) == 8)
  MatId   mat;
  int16_t temp;            // degrees C, clamped to [-273, 5000]
  uint8_t life;            // burn / lifetime counter, 0 = none
  uint8_t flags;           // FLAG_BURNING=1<<0, FLAG_BODY=1<<1 (reserved, plan 4); bits 4..7 = fall velocity 0..15
  uint8_t shade;           // per-pixel color variation 0..255
  uint8_t stamp;           // low 8 bits of tick in which this cell last moved/updated
};
constexpr int16_t AMBIENT_TEMP = 20;

// rng.h — stateless, e.g. a murmur3 fmix32 chain
inline uint32_t hash5(uint32_t seed, uint32_t tick, int32_t x, int32_t y, uint32_t salt);
inline float hash_unit(uint32_t h);   // [0,1)

// material.h
enum class MatClass : uint8_t { Empty, Solid, Powder, Liquid, Gas };
constexpr int16_t NO_TEMP = INT16_MAX; constexpr MatId NO_MAT = 0xFFFF;
struct MaterialDef {
  std::string name; MatClass cls; float density;
  uint32_t color_rgba; float shade_amount;          // brightness jitter 0..1
  uint8_t dispersion;                                // sideways cells/tick, liquid/gas, <= 16
  float emission_r, emission_g, emission_b, emission_energy, light_opacity;  // for plan 5
  bool collision_solid;                              // for plan 4
  float conductivity;  bool fixed_temp;  int16_t base_temp;
  int16_t ignite_temp; uint8_t burn_life; int16_t burn_heat; std::string burns_into;
  int16_t high_temp; std::string high_into; int16_t low_temp; std::string low_into;
  uint8_t lifetime; std::string decays_into;         // lifetime 0 = permanent
};
// Defaults for unset fields: density 1, shade_amount 0.1, dispersion 0, emission 0, light_opacity 1,
// collision_solid false, conductivity 0.1, fixed_temp false, base_temp 20, ignite/high/low temps NO_TEMP,
// burn_life 0, burn_heat 0, lifetime 0, all *_into names empty (= NO_MAT).
struct ReactionDef { std::string a, b, a_into, b_into; float probability; int16_t min_temp = -273, max_temp = 5000; };
struct CompiledMaterial { /* MaterialDef numeric fields with names resolved to MatId (NO_MAT if unset) + bool has_reactions */ };
struct Reaction { MatId a_into, b_into; float probability; int16_t min_temp, max_temp; };
class MaterialTable {
 public:
  // Air (id 0, Empty, density 0) is implicit; defs get ids 1..n in order.
  static bool compile(const std::vector<MaterialDef>&, const std::vector<ReactionDef>&,
                      MaterialTable& out, std::string& error);
  MatId id_of(std::string_view name) const;          // NO_MAT if unknown
  const CompiledMaterial& get(MatId) const;
  const std::string& name_of(MatId) const;
  const Reaction* reaction(MatId self, MatId other) const;  // outputs oriented: a_into applies to self
  size_t size() const;
};
```

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("floor div/mod for negatives") {
  CHECK(floor_div(-1, 64) == -1); CHECK(floor_mod(-1, 64) == 63);
  CHECK(floor_div(-64, 64) == -1); CHECK(floor_div(-65, 64) == -2);
  CHECK(chunk_of(-1, 64).x == -1); CHECK(chunk_of(-1, 64).y == 1);
}
TEST_CASE("cell is 8 bytes") { CHECK(sizeof(Cell) == 8); }
TEST_CASE("hash rng is stateless") { CHECK(hash5(1,2,3,4,5) == hash5(1,2,3,4,5)); CHECK(hash5(1,2,3,4,5) != hash5(1,2,3,4,6)); }
TEST_CASE("compile assigns ids and resolves references") {
  // defs: sand(powder), water(liquid, high_temp 100 -> steam), steam(gas, lifetime 200 -> water)
  // expect: id_of("air")==0, sand==1, water==2, steam==3; get(water).high_into == 3; get(steam).decays_into == 2
}
TEST_CASE("unknown reference fails with the name in the error") {
  // wood.burns_into = "ash" with no ash def -> compile returns false, error contains "ash" and "wood"
}
TEST_CASE("reaction lookup is symmetric with oriented outputs") {
  // reaction {a=water, b=lava, a_into=steam, b_into=rock, p=1}
  // reaction(water, lava)->a_into == steam ; reaction(lava, water)->a_into == rock ; has_reactions true for both
}
TEST_CASE("duplicate reaction pair is an error") { /* water+lava twice (either order) -> false, error mentions both names */ }
```

- [ ] **Step 2:** Run `scons tests && build/tests/run_tests`. Expected: these tests fail to compile or fail.
- [ ] **Step 3:** Implement the headers and `material.cpp`. Store reactions in a dense `vector<int32_t>` of size `n*n` holding indices into a reaction vector (−1 = none), with both orientations filled in at compile time.
- [ ] **Step 4:** Run the tests. Expected: all pass.
- [ ] **Step 5:** Commit: `core: cells, coords, rng, material tables`.

---

### Task 3: Chunks, world grid, dirty rects, generator

**Files:**
- Create: `src/core/chunk.h/.cpp`, `src/core/world_grid.h/.cpp`, `src/core/generator.h/.cpp`, `tests/core/test_world_grid.cpp`

**Interfaces:**
- Consumes: Task 2 types.
- Produces:
```cpp
struct DirtyRect { int x0 = CHUNK, y0 = CHUNK, x1 = -1, y1 = -1;  // inclusive local coords; empty when x1 < x0
  bool empty() const; void include(int lx, int ly); void reset(); };
struct Chunk {
  ChunkCoord coord; std::array<Cell, CHUNK*CHUNK> cells;
  DirtyRect cur, next;            // cur = processed this tick; next = accumulated for next tick
  std::atomic_flag next_lock;     // guards `next` (written by neighbour chunks' workers)
  std::atomic<bool> render_dirty{true};
  Cell& at(int lx, int ly);       // index ly*CHUNK + lx
  void mark_next(int lx, int ly); // spinlocked include
};
class ChunkGenerator { public: virtual ~ChunkGenerator() = default;
  virtual void generate(ChunkCoord, Chunk& out, uint32_t seed) const = 0; };
class BoxGenerator : public ChunkGenerator {  // air inside `arena` (world px, half-open), `solid_mat` outside
 public: BoxGenerator(int x0, int y0, int x1, int y1, MatId solid_mat, const MaterialTable&); };
// Generators fill temp=base_temp, life=lifetime, shade=hash5(seed,0,wx,wy,0)&0xFF.
class WorldGrid {
 public:
  explicit WorldGrid(const MaterialTable&, uint32_t seed);
  Chunk* load(ChunkCoord, const ChunkGenerator&);   // generate + insert; marks whole chunk next-dirty
  void unload(ChunkCoord);
  Chunk* find(ChunkCoord) const;                    // nullptr if not loaded
  bool get(int wx, int wy, Cell& out) const;        // false if unloaded
  bool set(int wx, int wy, MatId);                  // false if unloaded; spawns cell with material defaults,
                                                    // marks 3x3 neighbourhood next-dirty (across chunk borders), render_dirty
  template<class F> void for_each_chunk(F&&);
  const MaterialTable& materials() const; uint32_t seed() const;
};
// Hot-path accessor: 3x3 chunk pointers around a centre chunk; local coords in [-64, 128).
struct Neighborhood {
  Chunk* c[3][3];                                   // nullptr = unloaded = acts as solid
  void build(const WorldGrid&, ChunkCoord centre);
  Cell* cell(int lx, int ly);                       // nullptr when the chunk is missing
  void mark(int lx, int ly);                        // mark_next on owning chunk + render_dirty, and neighbours of (lx,ly) when on a chunk edge
};
```

- [ ] **Step 1: Write failing tests** in `test_world_grid.cpp`:
  - `set/get round trip at negative coords`: load chunks (−1,−1) and (0,0), `set(-1,-1,sand)`, then `get(-1,-1)` → sand, and `find({-1,-1})->at(63,63).mat == sand`.
  - `set on unloaded chunk returns false`: `set(1000, 1000, sand) == false` and nothing is created.
  - `set marks neighbours across a chunk border dirty`: with (0,0) and (1,0) loaded and both `next` reset, `set(63, 10, sand)` makes chunk (1,0)'s `next` include local (0, 9..11).
  - `BoxGenerator fills solid outside the arena`: arena (0,0)-(64,64), chunk (−1,0) is all `solid_mat` and chunk (0,0) is all air.
  - `Neighborhood returns nullptr for missing chunks`: centre (0,0) with only (0,0) loaded, `cell(-1, 5) == nullptr` and `cell(5, 5) != nullptr`.
- [ ] **Step 2:** Run the tests. Expected: FAIL.
- [ ] **Step 3:** Implement. `WorldGrid` stores `std::unordered_map<int64_t, std::unique_ptr<Chunk>>`. Chunks are created only on the main thread (between ticks). `Neighborhood::cell` picks the chunk with `floor_div(l, 64) + 1` and the local index with `floor_mod`.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit: `core: chunks, world grid, dirty rects, box generator`.

---

### Task 4: Movement rules and single-threaded simulator

**Files:**
- Create: `src/core/sim_rules.h/.cpp`, `src/core/simulator.h/.cpp`, `tests/core/test_movement.cpp`, `tests/core/sim_test_util.h` (helpers: make a grid with a BoxGenerator arena, paint from ASCII art, count materials)

**Interfaces:**
- Consumes: Tasks 2 and 3.
- Produces:
```cpp
struct SimStats { int chunks_simulated = 0; int cells_updated = 0; double sim_ms = 0; };
class Simulator {
 public:
  Simulator(WorldGrid&, JobPool* pool /* nullptr = single-threaded */);
  void tick();                         // advances tick_ by 1
  uint32_t tick_count() const;
  const SimStats& last_stats() const;
};
// sim_rules.h — called per cell by Simulator; lx,ly are local to nb's centre chunk
void update_cell(Neighborhood& nb, int lx, int ly, uint32_t tick, uint32_t seed, const MaterialTable& mt);
```
- Movement rules. Screen coordinates: +y is down. `stamp == (tick & 0xFF)` means the
  cell was already updated this tick, so skip it.
  - **Displacement:** a mover may swap into a target if the target exists (its chunk is
    loaded) and either the target is `Empty`, or the target is a `Liquid` or `Gas` with
    lower density than the mover. Gases are the reverse: they swap upward into `Empty`,
    or into a `Gas` or `Liquid` with *higher* density.
  - **Powder:** fall `v` cells straight down, step by step, stopping at the first
    blocked cell, where `v = clamp(velocity + 1, 1, 15)`. If it moved, store `v` as its
    velocity. If straight down is blocked, try one diagonal-down in a hash-chosen
    order, then the other. If nothing moved, reset the velocity to 0 and the cell
    settles.
  - **Liquid:** like a powder for straight down and diagonals. If those fail, move
    sideways up to `dispersion` cells in a hash-chosen direction (falling back to the
    other direction), stopping before the first blocked cell.
  - **Gas:** the mirror of a liquid, moving up. Each tick, with probability 0.5, try
    a sideways step first (jitter).
  - **Solid and Empty:** no movement.
  - Every move sets `stamp` on the moved cell and `mark`s the source and destination.
    Each tick a cell moves at most 16 cells in any axis. That's what keeps the
    checkerboard in Task 6 safe.
- `Simulator::tick` (single-threaded version): for each loaded chunk, `cur = next`, then
  reset `next`. Then for each chunk with a non-empty `cur`, build a `Neighborhood` and
  scan `cur` rows from bottom (`y1`) to top. The x direction alternates on
  `tick & 1`. Call `update_cell` on each cell. Time the whole tick into `sim_ms`.

- [ ] **Step 1: Write failing tests** (each builds an arena spanning at least 2×2 chunks, so cross-chunk moves are covered):
  - `powder falls and rests on solid`: one sand cell at y=10 above a rock floor at y=100. After 120 ticks, the sand is at y=99.
  - `powder accelerates`: sand at y=0 in open air reaches y ≥ 20 after 8 ticks. (Velocity 1+2+…+8 = 36 with no obstacle, so expect y between 20 and 36.)
  - `powder piles diagonally`: drop 20 sand cells one per tick at the same x onto a floor. After 200 ticks, the pile has more than 1 column, and no column is taller than the pile is wide.
  - `powder sinks through water`: a sand cell on top of a 10-deep water column ends at the bottom. Total sand and water counts are unchanged.
  - `oil floats on water`: alternating rows of oil (density 0.8) and water (1.0) in a closed box. After 600 ticks, every oil cell is above every water cell in each column.
  - `liquid spreads sideways`: a 10×10 water block on a wide floor. After 300 ticks, its height is ≤ 2.
  - `gas rises`: a steam cell at the bottom of a closed box ends in the top row within 300 ticks.
  - `missing chunk acts solid`: sand in the bottom row of the only loaded chunk stays put for 60 ticks, and the count is unchanged.
  - `crossing chunk border`: sand at local y=63 of chunk (0,0) is in chunk (0,1) after 1 tick.
  - `settled world goes quiet`: a sand pile after 600 ticks gives `last_stats().chunks_simulated == 0` within 5 more ticks.
  - `conservation`: 2000 random sand, water and oil cells in a 256×256 closed arena. After 500 ticks, the count of each material is unchanged.
- [ ] **Step 2:** Run the tests. Expected: FAIL.
- [ ] **Step 3:** Implement `update_cell`, movement only (chemistry is Task 5), and the single-threaded `tick`. Material lookups use `const CompiledMaterial&` from the table, with no string ops in the hot loop.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit: `core: movement rules and single-threaded simulator`.

---

### Task 5: Chemistry — reactions, temperature, burning, phase change, lifetime

**Files:**
- Modify: `src/core/sim_rules.cpp`
- Create: `tests/core/test_chemistry.cpp`

**Interfaces:**
- Consumes: Task 4's `update_cell`. Chemistry runs in the same call, after movement, on
  the cell's final position.
- Rules (exact):
  - **Reactions:** if the material `has_reactions`, check the 4 neighbours. For each
    neighbour with `reaction(self, n)` whose temperature condition holds (on self's
    temperature), apply it with probability `p`, using
    `hash_unit(hash5(seed, tick, x, y, salt=neighbour index))`. Apply at most one
    reaction per cell per tick. The new cells take their materials' defaults
    (`base_temp`, `lifetime`), and both are `mark`ed. If any candidate existed but none
    fired, `mark` the cell so it stays awake.
  - **Heat exchange:** skip `Empty` and `fixed_temp` cells for their own update. Pick one
    4-neighbour using the hash.
    - Neighbour non-empty: `d = (t_n - t_self) * min(cond_self, cond_n) * 0.5`. Apply
      `+d` to self and `-d` to the neighbour, unless the neighbour is `fixed_temp`.
    - Neighbour `Empty` (air holds no heat): `t_self += (AMBIENT_TEMP - t_self) * cond_self * 0.05`.

    Round away from zero, so any `|diff| ≥ 2` changes at least 1 degree. If a
    temperature changed, `mark` both cells.
  - **Phase change:** if `high_into != NO_MAT` and `temp ≥ high_temp`, become
    `high_into` and keep the temperature. If `low_into != NO_MAT` and `temp ≤ low_temp`,
    become `low_into`.
  - **Ignition and burning:** if `ignite_temp != NO_TEMP`, the cell isn't burning, and
    `temp ≥ ignite_temp`, set `FLAG_BURNING` and `life = burn_life`. A burning cell,
    each tick:
    - set `temp = max(temp, burn_heat)`,
    - with probability 0.3, turn a hash-chosen `Empty` 4-neighbour into `fire`,
    - decrement `life`; at 0, become `burns_into` (`NO_MAT` → air) and clear the flag.

    A burning cell whose temperature falls below `ignite_temp - 50` stops burning.
    Burning cells always `mark` themselves. The `fire` material ID is looked up once,
    by the name `"fire"`. If it's absent, skip spawning.
  - **Lifetime:** if `lifetime > 0`, decrement `life` each tick; at 0, become
    `decays_into` (`NO_MAT` → air). Cells with a lifetime always `mark` themselves.

- [ ] **Step 1: Write failing tests** with a test material set (`sim_test_util.h` gains
  `make_chem_table()`). It contains:
  - water (liquid, cond 0.6, high 100 → steam)
  - steam (gas, base 110, lifetime 120 → water, low 90 → water)
  - lava (liquid, fixed_temp, base 1200, cond 0.5)
  - rock (solid)
  - wood (solid, ignite 300, burn_life 60, burn_heat 600, burns_into none)
  - fire (gas, lifetime 20, fixed_temp, base 600, cond 0.8)
  - acid (liquid)
  - reaction water + lava → steam + rock, p = 1
  - reaction acid + rock → air + air, p = 0.05

  Tests:
  - `water touching lava becomes steam and rock`: in a 1-wide rock shaft (so the water can't spread sideways), water directly above lava gives, after 1 tick, steam where the water was and rock where the lava was.
  - `acid eats rock over time`: a 1-cell acid on a 10-deep rock column has removed at least 3 rock cells after 600 ticks.
  - `wood next to lava ignites and burns away`: a wood cell beside a lava cell is burning within 200 ticks and becomes air within a further `60 + 10` ticks.
  - `fire spreads along wood`: a 20-cell wood beam with fire at one end has ≥ 10 cells burned or burning after 600 ticks.
  - `hot water boils`: water with temp set to 150 is steam after 1 tick.
  - `steam condenses after lifetime`: a steam cell in a closed box is water after 121 ticks.
  - `lava pool stays hot`: lava `temp` is still 1200 after 300 ticks next to water-free rock.
- [ ] **Step 2:** Run the tests. Expected: FAIL.
- [ ] **Step 3:** Implement the chemistry in `update_cell`.
- [ ] **Step 4:** Run all core tests. Expected: PASS (including Task 4's conservation test, which has no reactions).
- [ ] **Step 5:** Commit: `core: data-driven chemistry`.

---

### Task 6: Thread pool and four-phase checkerboard

**Files:**
- Create: `src/core/job_pool.h/.cpp`, `tests/core/test_parallel.cpp`
- Modify: `src/core/simulator.cpp`

**Interfaces:**
- Produces:
```cpp
class JobPool {
 public:
  explicit JobPool(int workers);       // workers >= 1; workers == 1 runs inline on caller
  ~JobPool();
  void parallel_for(int count, const std::function<void(int)>& fn);  // blocks until all done; caller thread participates
  int workers() const;
  static int default_workers();        // max(1, min(4, hardware_concurrency - 1))
};
```
- `Simulator::tick` with a pool: after the dirty swap (single-threaded), bucket the
  active chunks into 4 phases by `((cx & 1) | ((cy & 1) << 1))`. For each phase in
  order 0..3, `parallel_for` over that phase's chunks. Same-phase chunks are ≥ 64
  cells apart and each reaches at most ±32, so their reads and writes never overlap.
  The only shared writes are neighbours' `next` rects, which are guarded by
  `next_lock`. `render_dirty` is atomic.

- [ ] **Step 1: Write failing tests**
  - `1 thread and 4 threads give bit-identical worlds`: two grids with the same seed and the same 512×512 arena of random sand, water, oil, lava and wood. Tick one with `JobPool(1)` and the other with `JobPool(4)` for 400 ticks. Every cell's `mat`, `temp`, `life` and `flags` are equal.
  - `conservation under 4 threads`: Task 4's conservation scenario at 512×512 with `JobPool(4)`. Counts are unchanged after 500 ticks.
  - `parallel_for runs every index once`: 10000 indices, an atomic counter per index, every counter is 1.
- [ ] **Step 2:** Run the tests. Expected: FAIL.
- [ ] **Step 3:** Implement `JobPool`: N−1 std::threads plus the caller, a generation counter, a condition variable for wake-up, and an atomic index for work stealing. Then the phased tick.
- [ ] **Step 4:** Run all core tests, then run them again under ThreadSanitizer (`scons tests tsan=yes` adds `-fsanitize=thread`). Expected: PASS, no TSan reports.
- [ ] **Step 5:** Commit: `core: job pool and checkerboard parallel tick`.

---

### Task 7: Godot material resources and the starter set

**Files:**
- Create: `src/godot/pixel_material.h/.cpp`, `src/godot/pixel_reaction.h/.cpp`, `src/godot/pixel_material_set.h/.cpp`, `materials/*.tres` (10 materials + reactions + `materials/default_set.tres`)
- Modify: `src/godot/register_types.cpp`, `tests/godot/run_tests.gd`

**Interfaces:**
- Produces (GDScript-visible):
  - `PixelMaterial : Resource`. Exported properties mirror `MaterialDef`:
    - `name: StringName`, `material_class: int` (enum `CLASS_SOLID=1, CLASS_POWDER, CLASS_LIQUID, CLASS_GAS`)
    - `density: float`, `color: Color`, `shade_amount: float`, `dispersion: int`
    - `emission: Color`, `emission_energy: float`, `light_opacity: float`, `collision_solid: bool`
    - `conductivity: float`, `fixed_temperature: bool`, `base_temperature: int`
    - `ignite_temperature: int` (−1 = never), `burn_life: int`, `burn_heat: int`, `burns_into: StringName`
    - `high_temperature: int` (−1 = none), `high_into: StringName`, `low_temperature: int` (−1 = none), `low_into: StringName`
    - `lifetime: int`, `decays_into: StringName`
  - `PixelReaction : Resource`: `a, b, a_into, b_into: StringName`, `probability: float`, `min_temperature: int = -273`, `max_temperature: int = 5000`.
  - `PixelMaterialSet : Resource`: `materials: Array[PixelMaterial]`, `reactions: Array[PixelReaction]`. Method `validate() -> String` returns `""` when OK, otherwise the compile error. C++-only: `bool build_table(MaterialTable& out, String& error) const`.
- Starter set values (density / class / notable fields):

| name | class | density | notes |
|---|---|---|---|
| rock | solid | 3.0 | collision_solid, opacity 1 |
| wood | solid | 0.7 | collision_solid, ignite 300, burn_life 90, burn_heat 600 |
| sand | powder | 1.6 | |
| water | liquid | 1.0 | dispersion 5, cond 0.6, high 100 → steam |
| oil | liquid | 0.8 | dispersion 4, ignite 200, burn_life 40, burn_heat 700 |
| lava | liquid | 2.5 | dispersion 2, fixed_temp, base 1200, emission orange energy 2.0 |
| acid | liquid | 1.1 | dispersion 4, emission green energy 0.3 |
| fire | gas | 0.1 | lifetime 24, fixed_temp, base 600, emission yellow-orange energy 1.5 |
| steam | gas | 0.3 | base 110, lifetime 240 → water, low 90 → water |
| smoke | gas | 0.4 | lifetime 180 |

  Reactions:
  - water + lava → steam + rock, p 0.5
  - water + fire → steam + air, p 0.8
  - acid + rock → air + air, p 0.05
  - acid + wood → air + air, p 0.08
  - fire + oil → fire + fire, p 0.3

  Burning wood and oil spawn `fire` cells, and `fire.decays_into = smoke`, so fires
  smoke. Steam has `conductivity 0` (it condenses by lifetime, not by cooling). Light
  opacity: solids 1, powders 0.9, liquids 0.3, gases 0.05.

- [ ] **Step 1: Write failing Godot tests** in `run_tests.gd`:
  - `test_default_set_validates()`: `load("res://materials/default_set.tres").validate() == ""`.
  - `test_bad_reference_reports_name()`: a set built in code whose `wood.burns_into = &"ash"` has `validate()` containing `"ash"`.
- [ ] **Step 2:** Build, then run the headless tests. Expected: FAIL (class missing).
- [ ] **Step 3:** Implement the three Resources (`_bind_methods`, `ADD_PROPERTY`; the class enum through `BIND_ENUM_CONSTANT`). The −1 sentinels map to `NO_TEMP`. Create the `.tres` files (write them through a one-off headless GDScript tool using `ResourceSaver`, or by hand).
- [ ] **Step 4:** Build, then run the headless tests. Expected: `ALL GODOT TESTS PASSED`.
- [ ] **Step 5:** Commit: `godot: material, reaction and material-set resources with starter set`.

---

### Task 8: PixelWorld node — streaming box world, API, rendering

**Files:**
- Modify: `src/godot/pixel_world.h/.cpp`, `tests/godot/run_tests.gd`, `project.godot`
- Create: `demo/paint_demo.tscn`, `demo/paint_demo.gd`

**Interfaces:**
- Consumes: everything in `src/core/`, plus `PixelMaterialSet::build_table`.
- Produces (GDScript-visible, on `PixelWorld : Node2D`, 1 world cell = 1 unit in local space):
  - Properties:
    - `material_set: PixelMaterialSet`, `world_seed: int = 1337`, `worker_threads: int = -1` (−1 → `JobPool::default_workers()`)
    - `follow_target: NodePath`, `load_radius: Vector2i = Vector2i(6, 4)` (chunks around the target's chunk; Plan 2 replaces this with zones)
    - `arena: Rect2i = Rect2i(-512, -256, 1024, 512)`, used for `BoxGenerator` with `rock` outside
    - `simulating: bool = true`
  - Methods:
    - `set_cell(pos: Vector2i, material: StringName) -> bool`
    - `get_material_at(pos: Vector2i) -> StringName` (`&""` if unloaded)
    - `fill_rect(rect: Rect2i, material: StringName) -> int` (cells written)
    - `fill_circle(center: Vector2i, radius: int, material: StringName) -> int`
    - `step(ticks: int) -> void` (manual ticks, for tests and tools)
    - `is_chunk_loaded(chunk: Vector2i) -> bool`
    - `get_stats() -> Dictionary` with keys `sim_ms`, `upload_ms`, `chunks_loaded`, `chunks_simulated`, `cells_updated`, `tick`
  - Unknown material names push a Godot error naming the material and return
    `false`/`0`. Use `"air"` to erase.
- Behaviour:
  - On `_ready`, build the `MaterialTable` (if `validate` fails, `push_error` and
    disable), create `WorldGrid`, `Simulator` and `JobPool`, and load chunks around the
    target (or the origin if there's no target).
  - In `_physics_process`, if `simulating`, run one `Simulator::tick()`.
  - In `_process`: load and unload chunks for the target position (unload discards).
    For each loaded chunk with `render_dirty`, rebuild its 64×64 RGBA8 image, upload it
    with `RenderingServer::texture_2d_update` (create it on first use), and clear the
    flag. Each chunk owns one canvas item (a child of this node's canvas item) that
    draws its texture at `coord * 64`. Time the uploads into `upload_ms`.
  - Cell colour: `color` scaled by `1 + shade_amount * (shade/255 - 0.5)`. Burning
    cells lerp 50% toward `Color(1, 0.5, 0.1)`. Air is transparent.
- `project.godot` changes:
  - `display/window/size/viewport_width=480`, `viewport_height=270`
  - `window_width_override=1440`, `window_height_override=810`
  - `display/window/stretch/mode="viewport"`
  - `rendering/textures/canvas_textures/default_texture_filter=0` (nearest)
  - `physics/common/physics_ticks_per_second=60`, `physics/common/max_physics_steps_per_frame=3`

- [ ] **Step 1: Write failing Godot tests** (each builds a `PixelWorld` with `default_set.tres`, adds it to the tree, and awaits one frame):
  - `test_sand_falls()`: `set_cell((0,-200), &"sand")` then `step(30)`. `get_material_at((0,-200)) == &"air"`, and some cell in column 0 below −200 is sand.
  - `test_negative_coords()`: `set_cell((-300,-100), &"water")` returns true, and `get_material_at` returns `&"water"`.
  - `test_unloaded_write_is_noop()`: `set_cell((100000, 0), &"sand") == false` and `fill_circle((100000,0), 5, &"sand") == 0`, with no crash.
  - `test_unknown_material()`: `set_cell((0,0), &"unobtainium") == false`.
  - `test_chunks_follow_target()`: move `follow_target` to x=+5000 (outside the arena, so the chunks are solid rock) and await 2 frames. Then `is_chunk_loaded(Vector2i(78, 0))` is true and `is_chunk_loaded(Vector2i(0, 0))` is false.
  - `test_stats_keys()`: `get_stats()` has all 6 keys.
- [ ] **Step 2:** Build, then run the headless tests. Expected: FAIL.
- [ ] **Step 3:** Implement `PixelWorld` and the project settings.
- [ ] **Step 4:** Build, then run the headless tests. Expected: `ALL GODOT TESTS PASSED`.
- [ ] **Step 5: Visual check.** Build `demo/paint_demo.tscn`: a `PixelWorld`, a `Camera2D` as the follow target (WASD pans), left mouse paints the selected material in a radius-6 circle, number keys 1–0 select the 10 starter materials, right mouse paints air, and a `Label` shows `get_stats()`. Run `godot --path . res://demo/paint_demo.tscn` and confirm by eye:
  - sand piles,
  - oil floats on water,
  - lava plus water makes rock and steam,
  - wood near lava ignites and smokes,
  - acid eats rock,
  - panning loads and unloads chunks with no holes.
- [ ] **Step 6: Hitch check.** In the demo, a debug key `H` sleeps the main thread for 500 ms with `OS.delay_msec(500)`. Confirm that `get_stats().tick` advances by at most 3 in the following frame.
- [ ] **Step 7:** Commit: `godot: PixelWorld node with streaming box world, painting API and chunk rendering`.

---

### Task 9: Benchmark scene and the Mac checkpoint

**Files:**
- Create: `bench/bench.tscn`, `bench/bench.gd`, `bench/results/.gitkeep`, `docs/mac-agent.md`

**Interfaces:**
- Consumes: the `PixelWorld` API from Task 8.
- Produces: `godot --path . res://bench/bench.tscn -- --frames=1800 --out=<path>` writes
  JSON and quits. The JSON keys are:
  - `machine` (`OS.get_processor_name()`), `os`, `renderer`
    (`RenderingServer.get_video_adapter_name()`), `worker_threads`, `frames`
  - `frame_ms_mean`, `frame_ms_p99`, `sim_ms_mean`, `sim_ms_p99`, `upload_ms_mean`,
    `upload_ms_p99`, `chunks_simulated_mean`, `cells_updated_mean`, `budget_pass`
    (`sim_ms_mean <= 6.0`)

  It needs a real window, not `--headless`.
- Scene content: the default arena (1024×512). On start, `bench.gd` paints a fixed,
  seeded layout using `fill_rect` and `fill_circle`:
  - a 300-wide water lake with a 60-wide oil layer on top,
  - 3 sand piles fed by a sand "faucet" (re-painted each frame at the top for the whole
    run),
  - a lava pool bordering water (continuous steam and rock),
  - a burning wood structure (40×60 lattice ignited by fire),
  - an acid pocket on rock.

  The camera (the follow target) pans across the arena and back over the run.
  `DisplayServer.window_set_vsync_mode(VSYNC_DISABLED)`. Discard the first 120 frames
  as warm-up.

- [ ] **Step 1:** Implement `bench.gd` and the scene. Parse `OS.get_cmdline_user_args()` for `--frames=` (default 1800) and `--out=` (default `user://bench.json`, with relative paths taken from the current directory).
- [ ] **Step 2:** Build the release library with `scons platform=linux target=template_release -j$(nproc)`, then run `godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/linux-irisxe-plan1.json`. Expected: the file exists and `budget_pass` is `true`. If it's false, profile (for example `perf record` on the Godot process) and optimize the hot loop before continuing. Likely fixes are fewer table lookups, a tighter dirty rect, and avoiding `Neighborhood` rebuilds per cell. Note the numbers in the commit message.
- [ ] **Step 3:** Write `docs/mac-agent.md`, the exact steps for the Mac agent:
  - pull the branch, then `git submodule update --init --recursive`,
  - `scons platform=macos arch=arm64 target=template_debug` and `target=template_release`,
  - `scons tests && build/tests/run_tests`,
  - `godot --headless --path . -s tests/godot/run_tests.gd`,
  - run the bench with `--out=bench/results/macos-m1-plan1.json`,
  - commit the result file and push.

  Also list what to report back if anything fails: the build log tail, the test output,
  and the JSON.
- [ ] **Step 4:** Commit: `bench: benchmark scene, Linux results, Mac agent instructions`.
- [ ] **Step 5: Mac checkpoint.** Push the branch and hand off to the Mac agent with `docs/mac-agent.md`. Plan 1 is done only when `bench/results/macos-m1-plan1.json` is committed with `budget_pass: true`. If the Mac fails the budget and Linux passes, look first at the thread count (4 performance cores) and at efficiency-core scheduling.
