# Plan 4 — Rigid Bodies (Box2D) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Non-breakable Box2D v3.2 bodies (crates, boulders, a car built from joints)
that fall, roll, tip and rest on the pixel terrain and on sand, float or sink in liquid, are
stamped into the grid so sand piles on them and water flows around them, and report hits
to game code.

**Architecture:** Godot-free core. `PhysicsWorld` (new) owns one `b2World` working directly
in cell units (`b2SetLengthUnitsPerMeter(10)`), one static terrain body per chunk near
bodies (pixel-edge outline → Douglas-Peucker → chain loops), the stamp/unstamp of body
pixels with a held-cell buffer, buoyancy, joints and hit events. Godot gets
`PixelRigidBody`, `PixelRevoluteJoint` and `PixelWheelJoint` nodes, and `PixelWorld` runs
the tick: terrain → Box2D step → stamp → cell sim → unstamp → particles.

**Tech Stack:** C++17, Box2D v3.2.0 (C17, MIT, git submodule at `thirdparty/box2d`),
godot-cpp 10.0.0-stable, doctest, GDScript, SCons.

**Spec:** `docs/superpowers/specs/2026-10-25-particles-bodies-creatures-design.md` (section
"Rigid bodies (stage 4)"), parent spec `docs/superpowers/specs/2026-10-25-noita-engine-design.md`.

## Deviations from the spec (decided while planning)

- **Sand holds bodies by collision, not by a spring.** The spec asked for a stiff
  penetration spring under bodies on sand. An explicit spring that stiff, applied once per
  60 Hz tick, oscillates: it's the jitter anti-goal. Instead `sand.tres` gets
  `collision_solid = true`, so sand gets terrain chain shapes like rock. Bodies rest on sand
  firmly (no settling at all, no muddiness), and a car drives a desert like ground. Cost:
  shapes rebuild when sand moves near a body (measured as `shape_ms`), and bodies don't sink
  in a pixel. Liquid buoyancy is unchanged.
- **Body shapes:** `HULL` (default, ≤ 8-vertex convex hull of the opaque pixels), `CIRCLE`
  (wheels, boulders) or `BOXES` (greedy rectangles, for concave bodies), chosen per body,
  instead of a general convex decomposition.
- **Terrain shapes build on the main thread**, synchronously, for the chunks bodies need,
  so a body never sees a missing or half-built chunk. `// ponytail: synchronous shape
  builds; move to a worker if shape_ms > 1 ms`. Box2D runs single-threaded (`workerCount =
  1`), which settles Plan 2's deferred enkiTS question: not adopted.

## Global Constraints

- Godot 4.7.2, godot-cpp 10.0.0-stable, C++17; Box2D compiled as C17. `src/core/` never
  includes Godot headers; it may include `box2d/box2d.h`.
- Targets: `macos` arm64 (this machine) and `linux` x86_64 (hand-off). Windows is not a target.
- **Units:** Box2D works in cells: `b2SetLengthUnitsPerMeter(10.0f)` before
  `b2CreateWorld`. Positions, velocities (cells/s), forces all in cells. Box2D's y axis is
  the world's y axis (down). Densities are the materials' `density` (mass per cell²).
- **Tick:** `1/60` s, `4` sub-steps. Body gravity: `PixelWorld.body_gravity`, default
  `600` cells/s², down.
- **Collision categories:** `CAT_TERRAIN = 1`, `CAT_BODY = 2`, `CAT_CREATURE = 4` (reserved
  for Plan 5). Bodies collide with all.
- **Terrain shapes:** cells whose material is `collision_solid` and that don't carry
  `FLAG_BODY`. Loop winding: walking a loop edge, Box2D's right normal `(d.y, -d.x)` points
  out of the solid. Douglas-Peucker epsilon `1.0` cell. Shapes are kept for chunks within
  `BODY_SHAPE_MARGIN = 16` cells of any enabled body's AABB.
- **Stamped body pixels are inert** to the cell sim: they don't move, aren't passable,
  don't react, don't exchange heat. Unstamping restores exactly what was held.
- **Bench gate** (`bench_world`, all of): Plan 3's conditions, plus
  `physics_ms_mean + shape_ms_mean + stamp_ms_mean ≤ 2.0` and `bodies_mean ≥ 40`.
- `./build.sh --test --verify` passes; `scons tests tsan=yes` stays clean.

## Review Focus

1. **Body spawned overlapping terrain or another body.** No crash, no NaN transform; it
   resolves or falls, and stamping never overwrites terrain or another body's pixels.
   (Test in Task 4.)
2. **Freeing in either order.** Freeing a `PixelRigidBody` or joint node mid-game, or freeing
   the `PixelWorld` before its bodies, never touches a destroyed `b2BodyId`/`b2JointId`.
   (Test in Task 6.)
3. **Bad body definitions.** Null texture, a texture with no opaque pixels, a palette colour
   that isn't mapped, an unknown `pixel_material`: `push_error`, no body is created, no
   crash. (Test in Task 6.)
4. **Fast bodies.** A body falling from 600 cells up onto a 2-cell-thick rock floor stops on
   it, never passes through. (Test in Task 3.)
5. **Leaving the world.** A body whose chunks are evicted (or teleported far away) is
   disabled and emits `left_world` once; terrain bodies of evicted chunks are destroyed, so
   their count stays bounded. (Tests in Tasks 3 and 6.)

---

## File Structure

```
.gitmodules, thirdparty/box2d          # submodule pinned at v3.2.0 (already added)
SConstruct                             # + Box2D C sources and include path, both builds
materials/sand.tres                    # collision_solid = true
src/core/chunk.h                       # + shape_dirty flag
src/core/world_grid.h / .cpp           # set shape_dirty wherever render_dirty is set; + wake()
src/core/sim_rules.cpp                 # FLAG_BODY cells inert
src/core/outline.h / .cpp              # NEW: solid mask, loop tracing, Douglas-Peucker, hull, boxes
src/core/physics.h / .cpp              # NEW: PhysicsWorld
src/godot/pixel_rigid_body.h / .cpp    # NEW node
src/godot/pixel_joints.h / .cpp        # NEW: PixelRevoluteJoint, PixelWheelJoint
src/godot/pixel_world.h / .cpp         # physics ownership, tick order, body registry, raycast, stats
src/godot/register_types.cpp, build.sh # register / list the new classes
tests/core/physics_test_util.h         # NEW: material table + arena for physics tests
tests/core/test_outline.cpp            # NEW
tests/core/test_physics.cpp            # NEW
tests/godot/run_tests.gd               # + body / joint tests
demo/bodies_demo.tscn / .gd            # NEW: crates, boulders, car, desert run
bench/bench_world.gd, bench/results/macos-m1-plan4.json, docs/bench-agent.md
```

---

### Task 1: Build Box2D into both targets

**Files:**
- Modify: `SConstruct`, `materials/sand.tres`
- Create: `tests/core/test_physics.cpp` (smoke case only)

**Interfaces:**
- Produces: `#include "box2d/box2d.h"` works from `src/core/`, `src/godot/` and `tests/core/`.

- [ ] **Step 1:** Write `TEST_CASE("box2d links and a box falls")` in `test_physics.cpp`:
  `b2SetLengthUnitsPerMeter(10)`, a world with gravity `(0, 600)`, a dynamic 4×4 box at
  (0, 0); after 60 `b2World_Step(world, 1/60.f, 4)` its y is > 200. Destroy the world.
- [ ] **Step 2:** `scons tests` → fails: `box2d/box2d.h` not found.
- [ ] **Step 3:** In `SConstruct`, add `thirdparty/box2d/include` to `CPPPATH` of both
  environments and `Glob("thirdparty/box2d/src/*.c")` to both source lists. C files get
  `-std=c17` via `CFLAGS` (and `-O2` in the tests env). No Box2D SIMD defines: it picks
  SSE2/NEON itself. Set `collision_solid = true` in `materials/sand.tres`.
- [ ] **Step 4:** `scons tests && build/tests/run_tests` → all pass; `./build.sh` builds the
  extension.
- [ ] **Step 5:** Commit: `build: vendor Box2D v3.2.0 into the extension and tests; sand is collision-solid`.

### Task 2: Outline geometry

**Files:** Create `src/core/outline.h`, `src/core/outline.cpp`, `tests/core/test_outline.cpp`

**Interfaces:**
- Consumes: `Chunk`, `MaterialTable`, `FLAG_BODY`, `b2Vec2`.
- Produces:
  ```cpp
  using Mask = std::vector<uint8_t>;  // row-major w*h, 1 = solid
  using Loop = std::vector<b2Vec2>;   // closed; first point not repeated at the end
  // collision_solid && !FLAG_BODY, CHUNK*CHUNK
  void chunk_solid_mask(const Chunk& chunk, const MaterialTable& table, Mask& out);
  // Pixel-edge outlines of the solid cells, in cell coords [0,w]x[0,h]. Cells outside the
  // mask are empty. Collinear points are merged. Winding: right normal (d.y,-d.x) of every
  // edge points out of the solid. Cells touching only at a corner belong to separate loops.
  std::vector<Loop> trace_loops(const Mask& mask, int w, int h);
  Loop simplify_loop(const Loop& loop, float epsilon);  // DP on a closed loop; keeps >= 3 points
  // Convex hull of the opaque pixels' corners, reduced to <= B2_MAX_POLYGON_VERTICES by
  // repeatedly dropping the vertex whose removal loses the least area. Empty if no pixels.
  Loop pixel_hull(const Mask& mask, int w, int h);
  struct PixelRect { int x, y, w, h; };
  std::vector<PixelRect> pixel_boxes(const Mask& mask, int w, int h);  // greedy row runs merged downwards
  ```

- [ ] **Step 1: Write the failing tests** (`test_outline.cpp`):
  - `"single cell is one square loop"`: 1×1 solid → one loop `(0,0),(1,0),(1,1),(0,1)` (this
    winding: top edge left→right, so its right normal is (0,−1), out of the solid).
  - `"every edge's right normal points into empty space"`: for an L shape, a 3×3 ring (2 loops)
    and random 16×16 masks (seeded `hash5`), the cell at each edge midpoint + 0.5 × right
    normal is empty, and − 0.5 × right normal is solid.
  - `"ring gives outer and hole loops"`: 5×5 with a 3×3 hole → 2 loops, the hole's signed area
    opposite to the outer's.
  - `"diagonal neighbours are separate loops"`: cells (0,0) and (1,1) → 2 loops of 4 points.
  - `"collinear points merge"`: 10×1 bar → 4 points.
  - `"simplify keeps shape within epsilon"`: a 20-step staircase loop simplified with ε = 1
    has fewer points, every original point is within 1.0 of the result, and ≥ 3 points remain
    even for a 1×1 square with ε = 5.
  - `"pixel hull"`: a full 8×8 mask → 4 vertices with area 64; a radius-8 disc → ≤ 8 vertices,
    area within 10% of the pixel count; an empty mask → empty loop.
  - `"pixel boxes cover exactly the pixels"`: an L shape and a random 12×12 mask — boxes don't
    overlap and their union equals the mask.
  - `"chunk mask skips body pixels and non-solids"`: a chunk with rock, sand (collision-solid
    in the test table), water and a rock cell with `FLAG_BODY` → mask 1 only for rock and sand.
- [ ] **Step 2:** `scons tests` → fails, `outline.h` missing.
- [ ] **Step 3:** Implement. Tracing: collect boundary edges per cell side with the winding
  above, then follow edges vertex to vertex; at a vertex with two outgoing edges (a diagonal
  saddle), take the one that turns right relative to the incoming edge, which keeps
  corner-touching cells separate.
- [ ] **Step 4:** `scons tests && build/tests/run_tests` → all pass.
- [ ] **Step 5:** Commit: `feat(core): pixel outline tracing, Douglas-Peucker, pixel hull and box cover`.

### Task 3: `PhysicsWorld` — bodies, terrain shapes, zones, hits

**Files:** Create `src/core/physics.h`, `src/core/physics.cpp`, `tests/core/physics_test_util.h`;
modify `src/core/chunk.h`, `src/core/world_grid.cpp`; test `tests/core/test_physics.cpp`

**Interfaces:**
- Consumes: Task 2; `WorldGrid`, `Chunk`, `ChunkRect`, `chunk_key`.
- Produces:
  ```cpp
  enum class BodyShape { Hull, Circle, Boxes };
  struct BodyDef {
      int w = 0, h = 0;
      std::vector<MatId> pixels;  // w*h row-major, NO_MAT = transparent
      BodyShape shape = BodyShape::Hull;
      float friction = 0.6f, restitution = 0.1f;
      float x = 0, y = 0, angle = 0;  // the image centre's world position, radians
  };
  struct BodyHit { int a, b; float x, y, nx, ny, speed; };  // b = -1 for terrain
  struct PhysicsStats { int bodies = 0, awake = 0, terrain_bodies = 0, shape_builds = 0;
                        double physics_ms = 0, shape_ms = 0, stamp_ms = 0; };
  class PhysicsWorld {
  public:
      PhysicsWorld(WorldGrid& grid, float gravity);  // gravity in cells/s^2, +y
      ~PhysicsWorld();                               // destroys the b2World
      int add_body(const BodyDef& def);              // handle >= 0, or -1 if no opaque pixels
      void remove_body(int handle);
      bool has_body(int handle) const;
      b2BodyId body_id(int handle) const;            // for forces / velocity / transform
      void set_active_rect(std::optional<ChunkRect> rect);
      void update_terrain();   // shapes for chunks near bodies; drops shapes of unloaded chunks
      void step();             // support forces, b2World_Step(1/60, 4), zones, hits
      const std::vector<BodyHit>& hits() const;      // from the last step
      std::vector<int> take_left_world();            // handles that just left the loaded world
      bool raycast(float x0, float y0, float x1, float y1, int& handle, float& hx, float& hy,
                   float& nx, float& ny) const;      // closest body along the ray
      const PhysicsStats& stats() const;
  };
  ```
  `Chunk` gains `std::atomic<bool> shape_dirty{true}`, set true wherever `render_dirty` is.

**Decided here:**
- Body local frame: origin at the image centre; pixel (px, py) occupies local
  `[px − w/2, px + 1 − w/2] × [py − h/2, py + 1 − h/2]`. Shape density per shape = the sum of
  its pixels' material densities ÷ the shape's area, so mass = Σ pixel densities.
  `CIRCLE`: radius = half the larger opaque extent, centred on the opaque pixels' bounds.
  Polygons get a `0.5`-cell rounding radius (glides over chunk seams).
- Body `userData` = handle + 1; terrain bodies have null `userData`. Shapes enable hit
  events; world `hitEventThreshold` stays the default (1 m/s = 10 cells/s).
- `update_terrain()`: needed chunks = chunks within `BODY_SHAPE_MARGIN` of every enabled
  body's AABB. For each needed loaded chunk that has no terrain body or has `shape_dirty`:
  clear the flag, compute the mask, and if it differs from the stored one, destroy the old
  static body and build a new one (loops simplified, each a chain loop, filter CAT_TERRAIN).
  Terrain bodies of chunks no longer loaded are destroyed. Building a chunk's shapes wakes
  bodies whose AABB overlaps it (`b2Body_SetAwake`).
- `step()` zones: a body is enabled only if its chunk is inside the active rect and every
  chunk its AABB + margin touches is loaded; otherwise `b2Body_Disable`. A body whose own
  chunk is not loaded at all is reported once by `take_left_world()` (and again only after
  it was enabled in between).

- [ ] **Step 1: Write the failing tests** (`test_physics.cpp`, using `physics_test_util.h`: a
  table with rock (Solid, 3.0, cs), sand (Powder, 1.6, cs), water (Liquid, 1.0), wood (Solid,
  0.5, cs), acid (Liquid, 1.1, reacts acid + rock → air + air at p = 1); helpers to load a
  rock-bordered air arena and to make a w×h `BodyDef` of one material):
  - `"a crate rests on a rock floor"`: 12×12 wood crate at (64, 20), floor at y = 100; after
    180 steps (`update_terrain` + `step` each tick) its y is within 1 cell of 94, rotation
    ≈ 0, and it's asleep or its speed < 1.
  - `"a crate rests on sand"`: same with a sand floor 20 deep → rests on the sand surface.
  - `"digging under a resting body drops it"`: crate at rest, then `fill` air under it
    (via `grid.set`) → after 120 more steps it's lower by ≥ 10.
  - `"fast body does not tunnel"`: floor 2 cells thick at y = 600 in a 128×700 arena (load
    the chunks), crate at y = 10 with initial velocity (0, 2000) → ends above y = 600.
  - `"hit events report terrain hits"`: crate dropped from 60 cells → at least one hit with
    `b == -1`, `speed > 10`.
  - `"bodies outside the active rect are disabled"` and re-enabled when the rect covers them;
    `"a body next to an unloaded chunk is disabled"`.
  - `"unloading a chunk destroys its terrain body"`: `stats().terrain_bodies` drops after
    `grid.unload` + `update_terrain`; a body in that chunk shows up once in
    `take_left_world()`.
  - `"raycast hits a body"`: ray across a resting crate returns its handle and a point on its
    edge.
  - `"overlapping spawn does not explode"`: two crates spawned at the same spot and one half
    inside the floor → after 120 steps all positions and rotations are finite.
- [ ] **Step 2:** `scons tests` → fails.
- [ ] **Step 3:** Implement `PhysicsWorld` and the `shape_dirty` flag.
- [ ] **Step 4:** `scons tests && build/tests/run_tests` → all pass.
- [ ] **Step 5:** Commit: `feat(core): PhysicsWorld with Box2D bodies, per-chunk terrain chain loops, zones and hits`.

### Task 4: Stamping, inert body pixels and buoyancy

**Files:** Modify `src/core/physics.h/.cpp`, `src/core/sim_rules.cpp`, `src/core/world_grid.h/.cpp`;
test `tests/core/test_physics.cpp`

**Interfaces:**
- Produces:
  ```cpp
  void PhysicsWorld::stamp();    // before Simulator::tick
  void PhysicsWorld::unstamp();  // after it
  void WorldGrid::wake(int wx, int wy);  // mark_next on the 3x3, no render/shape flags
  ```

**Decided here:**
- `stamp()`: for every enabled body, walk the world cells in its AABB, map each cell centre
  into body-local space, and take the pixel there (inverse mapping: no holes when rotated).
  - Target cell is air, gas, powder or liquid (and not `FLAG_BODY`): save it in the body's held
    list, write the body pixel's material (`spawn_cell`) with `FLAG_BODY`.
  - Target is a non-body Solid or already `FLAG_BODY`: skip that pixel.
  - Counts per body for buoyancy: held liquid pixels, their summed density and position.
  - Awake bodies `wake()` the cells they write; sleeping bodies stamp silently.
- `unstamp()`: every stamped cell still carrying `FLAG_BODY` gets its held cell back (or air);
  awake bodies `wake()` it. Then the held lists clear.
- Inert in `sim_rules.cpp`: `update_cell` returns for `FLAG_BODY`; `passable` is false for a
  `FLAG_BODY` target; `react` and `exchange_heat` skip a `FLAG_BODY` neighbour.
- Buoyancy in `step()` from the last stamp's counts: upward force `Σ held liquid density ×
  gravity` at the submerged centroid; linear drag `−2.0 × submerged fraction × mass × v`;
  angular drag `−2.0 × submerged fraction × inertia × ω`.

- [ ] **Step 1: Write the failing tests:**
  - `"stamp and unstamp restore held cells exactly"`: crate over a water pool partly
    submerged; snapshot all cells, `stamp`, check body cells carry `FLAG_BODY` and the wood
    material, `unstamp`, cells equal the snapshot byte for byte.
  - `"sand piles on a body"`: a resting crate, a sand column dropped above it; each tick
    `stamp → sim.tick → unstamp`; after 200 ticks sand rests on the crate's top (sand at
    the cell just above the crate's top centre) and no sand is inside its footprint.
  - `"stamped pixels are inert"`: a stamped rock body pixel next to acid: 30 sim ticks while
    stamped → acid still present; a stamped sand-material body (powder) doesn't fall.
  - `"stamping never overwrites terrain or another body"`: body half inside rock and a second
    body overlapping it → after stamp/unstamp, rock count unchanged and no `FLAG_BODY` left.
  - `"wood floats and rock sinks"`: pool 40 deep; a wood crate ends with its centre above the
    pool's mid-depth and within 8 cells of the surface; a rock crate ends on the pool floor.
- [ ] **Step 2:** Run → fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `scons tests && build/tests/run_tests && scons tests tsan=yes && build/tests/run_tests` → pass, TSan clean.
- [ ] **Step 5:** Commit: `feat(core): stamp bodies into the grid with held cells, inert body pixels, buoyancy`.

### Task 5: Joints

**Files:** Modify `src/core/physics.h/.cpp`; test `tests/core/test_physics.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct RevoluteParams { bool limit = false; float lower = 0, upper = 0;
                          bool motor = false; float speed = 0, max_torque = 0; };
  struct WheelParams { float axis_x = 0, axis_y = 1;  // world-space suspension axis
                       float hertz = 4, damping = 0.7f;
                       bool motor = false; float speed = 0, max_torque = 0; };
  b2JointId PhysicsWorld::add_revolute(int a, int b, float x, float y, const RevoluteParams&);
  b2JointId PhysicsWorld::add_wheel(int a, int b, float x, float y, const WheelParams&);
  void PhysicsWorld::remove_joint(b2JointId);  // no-op if already destroyed with a body
  ```
  The anchor (x, y) is in world cells. Local frames come from `b2Body_GetLocalPoint`; the
  wheel frame A's rotation aligns its x axis with `axis` in A's frame.

- [ ] **Step 1: Write the failing tests:** `"a motor car drives along a floor"`: 40×8 rock chassis,
  two radius-5 `CIRCLE` wheels on wheel joints with motors at 20 rad/s (sign so it drives
  +x) → after 180 steps the chassis x grew by ≥ 60 and it's upright (|angle| < 0.5).
  `"revolute hinge holds"`: two crates on a revolute joint stay within 1.5 cells of the anchor.
  `"removing a body removes its joints"`: `remove_joint` afterwards is a no-op, no crash.
- [ ] **Step 2:** Run → fail. **Step 3:** Implement. **Step 4:** All pass.
- [ ] **Step 5:** Commit: `feat(core): revolute and wheel joints`.

### Task 6: Godot nodes, `PixelWorld` integration, demo

**Files:** Create `src/godot/pixel_rigid_body.h/.cpp`, `src/godot/pixel_joints.h/.cpp`,
`demo/bodies_demo.tscn`, `demo/bodies_demo.gd`; modify `src/godot/pixel_world.h/.cpp`,
`src/godot/register_types.cpp`, `build.sh` (class list), `tests/godot/run_tests.gd`

**Interfaces (GDScript-visible):**
- `PixelWorld`: `body_gravity: float = 600.0` (read in `_ready`); adds itself to group
  `&"pixel_world"`; `raycast` gains `RAY_BODIES = 16`, default mask
  `RAY_SOLID | RAY_POWDER | RAY_BODIES`, and a `"collider"` key (the `PixelRigidBody`, or
  null for terrain) — the closer of the grid hit and the body hit wins. Stats: `bodies`,
  `bodies_awake`, `terrain_bodies`, `shape_builds`, `physics_ms`, `shape_ms`, `stamp_ms`.
- `PixelRigidBody` (`Node2D`): `texture: Texture2D`, `pixel_material: StringName`,
  `palette: PixelRoomPalette` (used when set; colours not in it are errors),
  `shape: int` (`SHAPE_HULL`, `SHAPE_CIRCLE`, `SHAPE_BOXES`), `friction`, `restitution`,
  `world: NodePath` (empty → first node in group `pixel_world`). Methods: `apply_force(force,
  position = global_position)`, `apply_impulse(impulse, position = global_position)`,
  `apply_torque(t)`, `linear_velocity` / `angular_velocity` (get/set), `get_mass()`,
  `is_sleeping()`, `teleport(position, rotation)`. Signals: `hit(other: Node, point: Vector2,
  normal: Vector2, speed: float)`, `left_world()`. Draws its texture centred on itself.
- `PixelRevoluteJoint`, `PixelWheelJoint` (`Node2D`): `body_a`, `body_b` (NodePaths to
  `PixelRigidBody`), anchor = the joint node's global position; the revolute exposes
  `enable_limit`, `lower_angle`, `upper_angle`, `enable_motor`, `motor_speed`,
  `max_motor_torque`; the wheel exposes `axis` (default `Vector2(0, 1)`), `frequency`,
  `damping_ratio`, `enable_motor`, `motor_speed`, `max_motor_torque`. Setters apply live.

**Decided here:**
- **Registration is lazy:** a body or joint registers on its first `_physics_process` (every
  `_ready` has run by then); a joint waits until both bodies are registered. Bodies
  unregister in `NOTIFICATION_EXIT_TREE`; `PixelWorld` keeps `ObjectID`s and on its own
  `PREDELETE` tells registered nodes their handles are gone, so neither side outlives the
  other's `b2` ids.
- **Tick** (`tick_world`): `physics->update_terrain()` → `physics->step()` → `stamp()` →
  `sim->tick()` → `unstamp()` → `particles->step()`; then set every registered body's
  `global_position`/`global_rotation` from Box2D and emit `hit` (to both nodes) and
  `left_world`. `set_active_rect` gets the same rect as the simulator. `evict()` needs no
  hook: `update_terrain` drops shapes of unloaded chunks.
- The body texture is read once with `get_image()`, converted to RGBA8; alpha < 0.5 is
  transparent.

- [ ] **Step 1: Write the failing Godot tests:** `test_body_falls_and_rests` (crate from an
  `Image`-built `ImageTexture`, `pixel_material = &"wood"`, in the arena: after `step(240)` its
  y is within 2 of the floor minus half its height, and `hit` fired at least once);
  `test_body_bad_definitions` (null texture, empty texture, unmapped palette colour, unknown
  material → `get_stats().bodies == 0`, no crash); `test_body_free_order` (free a body
  mid-run, then free a world with a live body and a joint → no crash, `step` still works on
  the first); `test_raycast_reports_body`; `test_left_world` (teleport the body 100 000 cells
  away → `left_world` once); `test_wheel_joint_motor` (chassis + 2 wheels with motor drives
  +x by ≥ 40 in 180 steps).
- [ ] **Step 2:** `./build.sh --verify` → fails. **Step 3:** Implement. **Step 4:**
  `./build.sh --test --verify` → all pass.
- [ ] **Step 5: Demo** `demo/bodies_demo.tscn` (generated world, camera follows the car):
  arrow keys drive the car (motor speed ±, `Space` brakes); `C` drops a crate and `B` a
  boulder at the mouse; middle mouse explodes; the paint keys of `paint_demo.gd` are not
  needed. Car, crate and boulder textures are built in code from `Image`s. A label shows the
  body stats. Manual check (screenshots via a throwaway script in `build/`): crates rest on
  dirt and on sand without sinking or shaking, sand piles on a crate, a wood crate floats in
  water, the car drives across ground and a sand area and can flip on a slope.
- [ ] **Step 6:** Commit: `feat(godot): PixelRigidBody, revolute and wheel joint nodes, bodies in the world tick, bodies demo`.

### Task 7: Benchmark and checkpoint

**Files:** Modify `bench/bench_world.gd`, `docs/bench-agent.md`; create `bench/results/macos-m1-plan4.json`

- [ ] **Step 1:** Workload: every 10 frames spawn 3 bodies (2 wood crates 12×12 `HULL`, 1 rock
  boulder radius 7 `CIRCLE`) at camera offsets (−100..100, −100) and free any body more
  than 400 cells from the camera or that emitted `left_world`; cap 60 alive. Samples and JSON
  keys: `bodies_mean`, `bodies_awake_mean`, `physics_ms_mean/p99`, `shape_ms_mean/p99`,
  `stamp_ms_mean/p99`, `shape_builds_mean`. Gate per Global Constraints.
- [ ] **Step 2:** `./build.sh --release --test --verify`, then
  `godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/macos-m1-plan4.json`
  → `budget_pass: true`. If the body gate fails, profile `update_terrain` first.
- [ ] **Step 3:** `docs/bench-agent.md`: Plan 4 result path (replaces plan3 as the owed Linux
  run), new keys and gate, this machine's numbers.
- [ ] **Step 4:** Commit: `bench: bodies in bench_world, macOS M1 Plan 4 results`.
- [ ] **Step 5:** Linux checkpoint: push and hand off — needs the user's go-ahead.
