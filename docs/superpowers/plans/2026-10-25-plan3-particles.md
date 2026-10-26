# Plan 3 — Free Particles, `explode`, `raycast` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pixels can leave the grid and fly. There are two kinds of particle:
- **Material particles** fly in arcs and land back as cells.
- **Visual particles** fade out and never touch the grid.

Game code gets `spawn_particles`, `spawn_visual_particles`, `explode` and `raycast`.

**Architecture:**
- **Core (Godot-free):**
  - `ParticlePool` owns both particle kinds and steps them once per tick after
    `Simulator::tick`.
  - `explode` removes cells and throws loose ones into the pool.
  - `raycast` walks the grid with a DDA.
- **Godot:** `PixelWorld` owns the pool. It runs sim then particles in one private tick
  helper, binds the four methods, and draws every particle in one RenderingServer
  MultiMesh above the chunks.

**Tech Stack:** C++17, godot-cpp 10.0.0-stable, doctest, GDScript, SCons.

**Spec:** `docs/superpowers/specs/2026-10-25-particles-bodies-creatures-design.md`. This
plan is stage 1: "Free particles" and "`PixelWorld` additions". Read it with the parent
spec `docs/superpowers/specs/2026-10-25-noita-engine-design.md`.

## Global Constraints

- Godot 4.7.2, godot-cpp 10.0.0-stable (`api_version=4.7`), C++17.
- `src/core/` never includes Godot headers.
- Targets: `macos` arm64 (M1, **this machine**) and `linux` x86_64 (Iris Xe, hand-off).
  Windows is not a target.
- Chunk size 64×64, fixed tick 60 Hz.
- **Units.**
  - The core works in cells and ticks; the Godot API works in cells and seconds.
  - GDScript velocities and `power` are in cells per second. Divide by 60 at the boundary.
  - 1 world cell = 1 Godot unit (existing).
- **Particle constants**, all in `src/core/particles.h`:
  - `PARTICLE_GRAVITY = 0.25f` cells/tick².
  - `MAX_PARTICLE_SPEED = 16.0f` cells/tick per axis, which is the sim's per-tick move
    limit.
  - `LAND_SEARCH = 4` cells.
  - `EXPLODE_THROW_CHANCE = 0.5f`.
  - Default capacity is `20000`, shared by both kinds.
  - Gravity and throw chance are tuning knobs; the builder may change them after the demo
    check. Tests must not depend on their exact values.
- **Coordinates.** Every float→cell conversion uses `std::floor`, never a cast. Truncation
  puts x = −0.5 in cell 0.
- **Single-threaded particles.** `ParticlePool::step` runs on the main thread, and its
  file carries `// ponytail: single-threaded; move to JobPool if particle_ms > 1 ms`.
- **Explosions** affect pixels only. No forces on anything else (spec).
- The null-generator `arena` path and the generator path both get particles.
- **Bench gate** (`bench_world`, `budget_pass` requires all of):
  - every Plan 2 condition (`sim_ms_mean ≤ 6.0`, stream, frame p99, view missing,
    `cells_updated_mean ≥ 54192`),
  - `particle_ms_mean ≤ 1.0`,
  - `particles_material_mean ≥ 200`, so a bench that makes no particles can't pass.
- `./build.sh --test --verify` runs everything on this machine. `scons tests tsan=yes`
  stays clean.

## Review Focus

1. **Full pool.** If an explosion throws more cells than the pool has room for, nothing
   crashes and every removed cell is counted as thrown or vanished. A material `emit` into
   a full pool writes the cell back at its spawn spot if that spot is air, and otherwise
   counts it in `dropped`. (Test in Task 2.)
2. **Negative coordinates.** A particle at (−0.5, −0.5) lives in cell (−1, −1), and it lands
   correctly in chunks left of and above the origin. Explode and raycast work across the
   origin. (Tests in Tasks 1 and 2.)
3. **Unloaded ground.**
   - A particle that flies into an unloaded chunk, or whose chunk is evicted under it,
     counts as blocked. If the landing search finds no loaded air, it's dropped.
   - Nothing is ever written to an unloaded chunk.
   (Test in Task 1.)
4. **Bad input from GDScript.**
   - A non-finite velocity becomes 0, and a huge one is clamped to `MAX_PARTICLE_SPEED`.
   - `count ≤ 0` spawns nothing, and an unknown material calls `push_error` and returns 0.
   - `explode` with radius ≤ 0 is a no-op, and `raycast` with mask 0 never hits.
   - None of these crash.
   (Tests in Tasks 1 and 3.)
5. **Raycast edge cases.**
   - A ray that starts inside a matching cell hits at its start with normal (0, 0).
   - A zero-length ray checks only its start cell.
   - An axis-aligned ray, and one through an exact cell corner, never skip a cell.
   (Tests in Task 2.)

---

## File Structure

```
src/core/world_grid.h / .cpp     # + put(wx, wy, const Cell&): write an exact cell and wake it
src/core/particles.h / .cpp      # NEW: Particle, VisualParticle, ParticlePool, explode()
src/core/raycast.h / .cpp        # NEW: RayMask, RayHit, raycast()
src/godot/pixel_world.h / .cpp   # pool, tick helper, 4 bound methods, RAY_* constants, MultiMesh, stats
tests/core/test_particles.cpp    # NEW: pool, landing, visual, explode
tests/core/test_raycast.cpp      # NEW
tests/godot/run_tests.gd         # + particle / explode / raycast API tests
demo/paint_demo.gd               # + explode and spark keys, particle stats line
bench/bench_world.gd             # + explosions and sparks, particle stats, new gate
bench/results/macos-m1-plan3.json
docs/bench-agent.md              # + Plan 3 command and numbers
```

---

### Task 1: `ParticlePool` — material and visual particles

**Files:**
- Modify: `src/core/world_grid.h`, `src/core/world_grid.cpp`
- Create: `src/core/particles.h`, `src/core/particles.cpp`, `tests/core/test_particles.cpp`

**Interfaces:**
- Consumes: `WorldGrid::get/find`, `Cell`, `FLAG_BODY`, `MaterialTable::get`, `MatClass`,
  `spawn_cell`, `hash5`/`hash_unit` (`rng.h`), and `simtest::` helpers (`load_arena`,
  `paint`, `mat_at`, `count_mat`, `movement_table`).
- Produces:
  ```cpp
  // world_grid.h
  bool WorldGrid::put(int wx, int wy, const Cell& cell);  // false when unloaded; wakes like set()

  // particles.h
  struct Particle { float x, y, vx, vy; Cell cell; };  // position in cells, velocity in cells/tick
  struct VisualParticle {
      float x, y, vx, vy;
      float gravity_scale = 1.0f;
      uint32_t rgba = 0xFFFFFFFF;  // same packing as CompiledMaterial::color_rgba
      uint16_t life = 0;           // ticks left
      uint16_t lifetime = 0;       // ticks total, for the fade
      bool collides = true;
  };
  struct ParticleStats { int material = 0, visual = 0; int64_t dropped = 0; double particle_ms = 0; };

  class ParticlePool {
  public:
      ParticlePool(WorldGrid& grid, int capacity);  // capacity counts both kinds
      bool emit(float x, float y, float vx, float vy, const Cell& cell);  // true if pooled
      bool emit_visual(VisualParticle p);                                 // true if pooled
      void step();  // one tick; fills stats()
      const std::vector<Particle>& material() const;
      const std::vector<VisualParticle>& visual() const;
      const ParticleStats& stats() const;  // material/visual = sizes after the last step; dropped cumulative
  };
  ```

**Behaviour (decided here):**
- **Passable cell.** A cell a particle may move into or through is:
  - loaded,
  - with no `FLAG_BODY`,
  - and with a material of class `Empty` or `Gas`.
- **Movement.** Each step a material particle:
  1. adds `PARTICLE_GRAVITY` to `vy`,
  2. clamps both axes to ±`MAX_PARTICLE_SPEED`,
  3. moves in `n = ceil(max(|vx|, |vy|))` equal sub-steps (at least 1), so it never jumps a
     cell.
- **Blocked sub-steps.** A sub-step is blocked when the cell at its new position is not
  passable, even if that is the cell it already occupies (the grid can change under it).
- **No rest-speed rule.** A particle in open air keeps falling until something blocks it.
- **Landing.** The first blocked sub-step stops it. It then lands in the
  first **air** cell (`MAT_AIR`, loaded, no `FLAG_BODY`) among:
  1. its current cell,
  2. up to `LAND_SEARCH` cells back along `-velocity` (cell steps),
  3. up to `LAND_SEARCH` cells straight up.

  It lands with `grid.put` and keeps its own `Cell`. With no such cell, `dropped++` and the
  particle is removed.
- **Visual particles** move the same way, scaled by `gravity_scale`.
  - `life` drops by 1 each step, and the particle is removed at 0.
  - With `collides`, a non-passable sub-step removes it. Without `collides` it ignores
    cells, including unloaded ones.
  - It never writes to the grid.
- **Removal** is swap-with-last, so order isn't stable and nothing may rely on it.
- **`emit` sanitising.** Non-finite x, y, vx or vy become 0, and velocities are clamped as
  in Movement.
- **Full pool** (both sizes summed ≥ capacity):
  - `emit` writes the cell with `put` if `floor(x), floor(y)` is air, otherwise `dropped++`.
    Either way it returns false.
  - `emit_visual` returns false.

- [ ] **Step 1: Write the failing tests** in `tests/core/test_particles.cpp`. Use
  `movement_table()` and a 128×128 rock-boxed arena at (0, 0), as in `test_movement.cpp`.
  - `"put writes the exact cell and wakes it"`: `put` a sand `Cell` with `temp = 77` and
    `shade = 9` at (10, 10). `get` returns those values, and the chunk's `next` is no longer
    empty. `put` into an unloaded coord returns false.
  - `"material particle falls and lands as its cell"`: rock floor from y = 100. `emit` sand
    at (60.5, 10.5) with velocity 0 and `temp = 55`. After 120 steps the pool is empty,
    `mat_at(60, 99) == sand` with `temp == 55`, and `count_mat(sand) == 1`.
  - `"thrown particle follows an arc and does not tunnel"`:
    - Rock wall 1 cell thick at x = 80, from y = 0 to 128. `emit` sand at (20.5, 50.5) with
      `vx = 16`, `vy = -4`.
    - After 200 steps there's exactly one sand cell, it has x < 80, and every column with
      x > 80 is free of sand.
  - `"landing searches back along the path"`:
    - Rock floor from y = 100. `emit` sand at (60.5, 98.5) with velocity 0.
    - `put` rock at (60, 98), the particle's own cell, then `step` once.
    - The sand lands at (60, 97), and the sand count is 1.
  - `"unloaded ground blocks; a chunk unloaded under a particle drops it"`:
    - Load only chunk (0, 0), all air, using `BoxGenerator` with a rect covering it.
    - `emit` at (60.5, 30.5) with `vx = 16, vy = -0.25`. The next cell, x = 64, is unloaded.
      After one step it has landed at (63, 30), and `dropped == 0`.
    - `emit` a second particle at (30.5, 10.5), `unload` chunk (0, 0), then `step`.
      `dropped == 1`, the pool is empty, and there's no crash.
  - `"negative coordinates use floor"`:
    - Arena at (−128, −128)–(0, 0) with a rock floor at y = −20.
    - `emit` sand at (−0.5, −100.5). It lands at x = −1, never at x = 0.
  - `"visual particle fades out without touching the grid"`:
    - `emit_visual` with `life = lifetime = 30` and `collides = false`, inside a rock wall.
    - After 29 steps `visual().size() == 1`; after 30 it's 0.
    - The cell counts of every material are unchanged.
  - `"colliding visual particle dies on solid"`: `collides = true`, falling onto a rock floor
    with `lifetime = 600`. It's gone well before 600 steps, and no cell changed.
  - `"emit sanitises input"`:
    - `emit` with `vx = NaN`, `vy = 1e9`.
    - The pooled particle has `vx == 0` and `vy == MAX_PARTICLE_SPEED`.
  - `"full pool writes back or drops"`:
    - Capacity 2. Two `emit`s succeed.
    - A third at an air cell returns false and that cell becomes the material.
    - A fourth at a cell you painted rock returns false, and `dropped == 1`.
- [ ] **Step 2:** Run `scons tests && build/tests/run_tests -tc="*particle*,*put*"`. Expected:
  build fails, because `particles.h` doesn't exist.
- [ ] **Step 3: Implement `WorldGrid::put`.**
  - Same chunk lookup as `set`, but write the given cell verbatim, then `mark_world`.
  - Set the cell's `stamp` to 0, so the simulator doesn't treat it as already updated this
    tick.
- [ ] **Step 4: Implement `ParticlePool`** in `particles.h/.cpp` as specified above.
  - `step()` times itself with `steady_clock` into `stats().particle_ms`.
  - Two `std::vector`s, both `reserve(capacity)`.
- [ ] **Step 5:** Run `scons tests && build/tests/run_tests`. Expected: all tests pass,
  including Plans 1–2.
- [ ] **Step 6:** Commit: `feat(core): particle pool with landing material particles and fading visual particles`.

### Task 2: `explode` and `raycast`

**Files:**
- Modify: `src/core/particles.h`, `src/core/particles.cpp`
- Create: `src/core/raycast.h`, `src/core/raycast.cpp`, `tests/core/test_raycast.cpp`
- Test: `tests/core/test_particles.cpp`

**Interfaces:**
- Consumes: Task 1's `ParticlePool`, `WorldGrid::get/set`, `hash5`, `hash_unit`.
- Produces:
  ```cpp
  // particles.h
  struct ExplodeResult { int removed = 0, thrown = 0, vanished = 0; };
  // power: ejecta speed at the centre, cells/tick. salt: varies the dice per call (PixelWorld passes the tick).
  ExplodeResult explode(WorldGrid& grid, ParticlePool& pool, int cx, int cy, int radius,
                        float power, uint32_t salt);

  // raycast.h
  enum RayMask : uint32_t { RAY_SOLID = 1, RAY_POWDER = 2, RAY_LIQUID = 4, RAY_GAS = 8 };
  struct RayHit {
      bool hit = false;
      int x = 0, y = 0;        // the hit cell
      MatId mat = MAT_AIR;     // MAT_AIR when `unloaded`
      bool unloaded = false;   // hit an unloaded chunk (counts as solid)
      int nx = 0, ny = 0;      // face normal; (0,0) when the ray starts inside the hit cell
  };
  RayHit raycast(const WorldGrid& grid, float x0, float y0, float x1, float y1, uint32_t mask);
  ```

**Behaviour (decided here):**
- **`explode`** visits each loaded cell with `dx² + dy² ≤ radius²` that isn't air and has no
  `FLAG_BODY`.
  - `Solid` and `Gas` cells are set to air: `vanished++`.
  - `Powder` and `Liquid` cells are read, set to air, and with probability
    `EXPLODE_THROW_CHANCE` (`hash5(grid.seed(), salt, x, y, SALT)`) thrown.
    - A thrown cell is emitted from its cell centre.
    - Its direction is `(dx, dy)` normalised, or a hashed angle at the centre cell.
    - Its speed is `power × (1 − d / radius) × (0.5 + 0.5 × hash_unit(...))`, plus
      `−0.25 × power` on `vy`, so debris lifts.
    - An `emit` that returns true is `thrown++`, anything else `vanished++`.
    - Clear all cells first and emit afterwards. Then a write-back from a full pool lands in
      the crater instead of being cleared again.
  - `removed == thrown + vanished` always. `radius ≤ 0` returns an empty result.
- **`raycast`** uses Amanatides–Woo DDA from `(x0, y0)` to `(x1, y1)` over cells
  `floor(x), floor(y)`.
  - The start cell is checked first.
  - A cell matches if it's unloaded (`unloaded = true`), or if its material's class bit is
    in `mask`: Solid → `RAY_SOLID`, and so on. `Empty` never matches.
  - `mask == 0` never matches anything, not even unloaded.
  - At an exact corner crossing, step x then y (both cells are visited).
  - It stops after the end cell.

- [ ] **Step 1: Write the failing tests.**
  - In `test_particles.cpp`:
    - `"explode clears the radius and conserves the count"`:
      - Arena with a sand block over (40..80) × (40..80) and rock elsewhere inside.
      - `explode` at (60, 60) with radius 10 and power 8.
      - Every cell within the radius is air.
      - `removed` equals the number of non-air cells there before.
      - `removed == thrown + vanished`, and `thrown == pool.material().size()`.
    - `"explode with a full pool still conserves"`: capacity 5, same setup.
      `removed == thrown + vanished`, `thrown ≤ 5`, no crash.
    - `"explode leaves body pixels and solids outside alone"`: a cell with `FLAG_BODY` set
      by `put` inside the radius is untouched, and so is rock at distance radius + 1.
    - `"explode radius zero is a no-op"`: radius 0 and radius −3 both leave a zero result
      and an unchanged grid.
    - `"explode across the origin"`: arena (−64, −64)–(64, 64) filled with sand, explode at
      (0, 0) with radius 8. Cells (−8, 0), (7, 0), (0, −8) and (0, 7) are all air.
  - In `test_raycast.cpp`:
    - `"ray hits the first matching cell with its face normal"`: rock column at x = 50, ray
      from (10.5, 20.5) to (90.5, 20.5) with `RAY_SOLID`. Hit at (50, 20), normal (−1, 0).
    - `"mask filters classes"`: water at x = 30, rock at x = 50. With `RAY_SOLID` it hits 50;
      with `RAY_SOLID | RAY_LIQUID` it hits 30.
    - `"ray starting inside a match hits at start"`: start inside rock. Hit at the start
      cell, normal (0, 0).
    - `"zero-length ray checks only its start cell"`: air start gives no hit; rock start
      gives a hit.
    - `"ray never skips a cell"`:
      - Diagonal from (0.5, 0.5) to (40.5, 40.5) against a staircase of single rock cells
        at (k, k + 1) for k = 10..20. The ray hits the first stair it passes, never none.
      - An exact-corner ray from (0, 0) to (30, 30) against rock at (15, 14) hits it.
    - `"mask zero never hits"`, and `"unloaded counts as solid"`: a ray leaving the loaded
      area hits with `unloaded == true` and `mat == MAT_AIR`.
    - `"ray across the origin"`: rock at (−5, 3). A ray from (5.5, 3.5) to (−20.5, 3.5) hits
      (−5, 3), normal (1, 0).
- [ ] **Step 2:** Run `scons tests && build/tests/run_tests -tc="*explode*,*ray*"`. Expected:
  build fails, because the new symbols don't exist.
- [ ] **Step 3:** Implement `explode` in `particles.cpp` and `raycast` in `raycast.cpp`.
- [ ] **Step 4:** Run `scons tests && build/tests/run_tests && scons tests tsan=yes && build/tests/run_tests`.
  Expected: all pass, and TSan is clean.
- [ ] **Step 5:** Commit: `feat(core): explode throws loose cells as particles; DDA raycast over the grid`.

### Task 3: `PixelWorld` integration, rendering and demo

**Files:**
- Modify: `src/godot/pixel_world.h`, `src/godot/pixel_world.cpp`, `tests/godot/run_tests.gd`,
  `demo/paint_demo.gd`

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces (GDScript-visible on `PixelWorld`):
  - `max_particles: int = 20000`, read once in `_ready` like `worker_threads`.
  - `spawn_particles(material: StringName, position: Vector2, velocity: Vector2, count: int, spread: float) -> int`.
    - `spread` is the full cone angle in radians. Each particle's velocity is `velocity`
      rotated by a uniform angle in `[-spread/2, spread/2]` and scaled by a uniform
      `[0.75, 1.25]`.
    - Each particle's cell is `spawn_cell(material, …)` at `floor(position)`.
    - Returns how many were pooled. An unknown material calls `push_error` and returns 0;
      `count ≤ 0` returns 0.
  - `spawn_visual_particles(color: Color, position: Vector2, velocity: Vector2, count: int, spread: float, lifetime: float, gravity_scale: float = 1.0, collides: bool = true) -> int`.
    - `lifetime` is in seconds and becomes `clamp(round(lifetime × 60), 1, 65535)` ticks.
  - `explode(position: Vector2i, radius: int, power: float) -> int`. Returns
    `ExplodeResult::removed`. `power` is in cells/s.
  - `raycast(from: Vector2, to: Vector2, mask: int = RAY_SOLID | RAY_POWDER) -> Dictionary`.
    - On a hit it returns `{ "position": Vector2i, "material": StringName, "normal": Vector2i }`;
      `material` is `&""` when the hit is unloaded. Otherwise `{}`.
    - Bind `RAY_SOLID`, `RAY_POWDER`, `RAY_LIQUID` and `RAY_GAS` with `BIND_CONSTANT`.
  - `get_stats()` gains `particles_material`, `particles_visual`, `particles_dropped` and
    `particle_ms`.
- Random angles and scales use `hash5(world_seed, tick, i, count, SALT)`, so a run with the
  same inputs repeats.

**Decided here:**
- **Tick.**
  - A private `tick_world()` runs `sim->tick()` then `particles->step()`.
  - `_physics_process` and `step()` call it instead of `sim->tick()`.
  - Construct `particles` right after `sim` in `_ready`. Declare it after `grid`, so it's
    destroyed before `grid`.
- **Rendering.**
  - **The MultiMesh.** One RenderingServer MultiMesh (`MULTIMESH_TRANSFORM_2D`, colours on)
    over a 1×1 `QuadMesh`, sized to `max_particles` once in `_ready`.
  - **Where it draws.** It sits on its own canvas item parented to the node's, with
    `canvas_item_set_z_index(…, 1)`, so it stays above chunk views created later.
  - **Each frame, in `_process`:**
    - Fill one `PackedFloat32Array` with 12 floats per particle (2D transform, then colour).
    - The quad is centred on `floor(x) + 0.5, floor(y) + 0.5`, so particles sit on the
      pixel grid.
    - Then call `multimesh_set_buffer` and `multimesh_set_visible_instances(material + visual)`.
  - **Colour.**
    - Factor the colour code in `write_chunk_image` into `Color cell_color(const Cell&) const`.
      Use it for chunk pixels and material particles alike.
    - A visual particle's colour is its `rgba`, with alpha multiplied by `life / lifetime`.
  - **Cleanup.** Free the MultiMesh and canvas item in `NOTIFICATION_PREDELETE`, next to the
    chunk views.
- **`explode` in Godot** passes `power / 60` and `sim->tick_count()` as `salt`.

- [ ] **Step 1: Write the failing Godot tests** in `run_tests.gd`. Use `_make_world()` and
  the arena path.
  - `test_particles_land`: the arena's rock border is the floor.
    - Ten calls, `spawn_particles(&"sand", Vector2(-50 + 10 * i, -100), Vector2.ZERO, 5, 0.0)`
      for i in 0..9, each return 5. Five per spot is what fits: a particle landing on its
      siblings searches at most 4 cells up.
    - After `step(300)`, `particles_material == 0` and `particles_dropped == 0`.
    - Count `&"sand"` cells in the 100×400 strip under the spawn points before and after.
      The count goes up by exactly 50.
  - `test_visual_particles_expire`:
    - `spawn_visual_particles(Color.RED, Vector2.ZERO, Vector2(0, -60), 20, PI, 0.5)`
      returns 20.
    - After `step(31)`, `particles_visual == 0`.
  - `test_explode_and_raycast`:
    - `fill_rect` sand in a 40×40 block.
    - `explode` at its centre with radius 8 and power 300 returns > 0, and the centre reads
      `&"air"`.
    - `raycast` from the block's left outside edge towards its centre with `RAY_POWDER`
      returns `position.x` equal to the block's left column and normal `(-1, 0)`.
  - `test_particle_api_rejects_bad_input`:
    - `spawn_particles(&"nope", …)` returns 0.
    - `count = -5` returns 0.
    - A velocity of `Vector2(INF, NAN)` returns > 0 and `step(1)` doesn't crash.
    - `explode(…, 0, 100)` returns 0.
    - `raycast(…, 0)` returns `{}`.
- [ ] **Step 2:** Run `./build.sh --verify`. Expected: the new tests fail, because the
  methods don't exist.
- [ ] **Step 3:** Implement the `PixelWorld` changes above.
- [ ] **Step 4:** Run `./build.sh --test --verify`. Expected: `ALL GODOT TESTS PASSED` and
  all core tests pass.
- [ ] **Step 5: Demo.** In `paint_demo.gd` (used by both `paint_demo.tscn` and `world_demo.tscn`):
  - The middle mouse button calls `explode` at the mouse cell with radius 12 and power 300.
  - `F` sprays 60 yellow visual sparks upward from the mouse (spread `PI / 2`, lifetime
    0.6 s).
  - `G` sprays 40 water particles from the mouse at `Vector2(0, -200)` with spread 0.6.
  - Add the help text and a stats line:
    `particles %d / visual %d   particle %.2f ms   dropped %d`.
- [ ] **Step 6: Manual check.** Run `godot --path . res://demo/world_demo.tscn`.
  - Explosions carve holes, and sand and water arc out and land as cells.
  - Sparks fade.
  - Particles draw above terrain and sit on the pixel grid.
  - Nothing appears at the world origin when the pool is empty.
  - If the gravity or throw chance looks wrong, tune the constants from Global Constraints
    and note the new values in the commit message.
- [ ] **Step 7:** Commit: `feat(godot): spawn_particles, spawn_visual_particles, explode and raycast, particles drawn in one MultiMesh`.

### Task 4: Benchmark, results and Linux checkpoint

**Files:**
- Modify: `bench/bench_world.gd`, `docs/bench-agent.md`
- Create: `bench/results/macos-m1-plan3.json`

**Interfaces:**
- Consumes: the Task 3 API and stats.
- Produces: `bench_world` JSON gains `particle_ms_mean`, `particle_ms_p99`,
  `particles_material_mean`, `particles_visual_mean` and `particles_dropped`, the final
  cumulative value. `budget_pass` follows Global Constraints.

**Workload added** (frame-locked, offsets from the camera like the existing faucets):
- Every 20 frames, `explode` at (0, 60) with radius 14 and power 400. Below the camera is
  ground on most of the path.
- In the same frame, 150 visual sparks at the same spot, upward, spread `PI`, lifetime 0.8 s.
- If `particles_material_mean` lands under 200, raise the radius. Don't loosen the gate.

- [ ] **Step 1:** Add the workload, the samples, the JSON keys, the gate and the particle
  figures to the summary line in `bench_world.gd`.
- [ ] **Step 2: Mac run (this machine).**
  - `./build.sh --release --test --verify`.
  - Then `godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/macos-m1-plan3.json`.
  - Expected: exit 0 and `budget_pass: true`.
  - If `particle_ms_mean > 1.0`, profile first. The likely fix is the particle loop's
    per-sub-step `grid.find`: cache the last chunk pointer. Leave threading for later.
  - `sim_ms_mean` should stay within noise of the committed Plan 2 figure; record both in
    the commit message.
- [ ] **Step 3:** Update `docs/bench-agent.md`: the Plan 3 result path
  (`<macos-m1|linux-irisxe>-plan3.json`), the new JSON keys, and this machine's numbers in
  the status section.
- [ ] **Step 4:** Commit: `bench: explosions and sparks in bench_world, macOS M1 Plan 3 results`.
- [ ] **Step 5: Linux checkpoint.**
  - Push the branch and hand `docs/bench-agent.md` to the Linux machine.
  - Plan 3 is done only when `bench/results/linux-irisxe-plan3.json` is committed with
    `budget_pass: true`.
