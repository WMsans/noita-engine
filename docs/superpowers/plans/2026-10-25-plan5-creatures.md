# Plan 5 — Creatures (`PixelCharacter`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The player, enemies and other creatures move through the pixel world as upright
Box2D bodies: they collide with rigid bodies and each other with real mass, and a grid pass
makes them stand on and climb sand, step up ledges, knock sand out of their way, wade and
measure how deep they are in liquid. Dozens on screen within budget.

**Architecture:** Godot-free core. `PhysicsWorld` gains creatures: dynamic bodies with
rotation locked, one box shape, category `CAT_CREATURE`, colliding with bodies and creatures
but not terrain shapes. After each Box2D step, `creature_grid_pass` (new, `src/core/creature.*`)
sweeps each creature's hitbox from its pre-step to its post-step position through the cells,
axis by axis in whole-cell steps, and writes the corrected pose and velocity back. Godot gets a
`PixelCharacter` node modelled on `CharacterBody2D`'s read-modify-write `velocity`.

**Tech Stack:** C++17, Box2D v3.2.0, godot-cpp 10.0.0-stable, doctest, GDScript, SCons.

**Spec:** `docs/superpowers/specs/2026-10-25-particles-bodies-creatures-design.md` (section
"Creatures: `PixelCharacter` (stage 5)"); Plan 4 (`docs/superpowers/plans/2026-10-25-plan4-rigid-bodies.md`)
for the `PhysicsWorld` it extends.

## Global Constraints

- Everything in Plan 4's Global Constraints still holds (units in cells, 60 Hz, 4 sub-steps,
  categories, inert stamped pixels).
- **A creature's hitbox** is the axis-aligned box `[x − w/2, x + w/2) × [y − h/2, y + h/2)`
  around its Box2D position; the cells it covers are `floor(edge + 1e-4)` to
  `floor(far edge − 1e-4)` on each axis.
- **Blocking cells** for creatures: class `Solid` or `Powder`, or an unloaded chunk.
  Liquid, gas and `FLAG_BODY` cells never block (bodies collide in Box2D).
- `step_height` default `3`; step-up only when the creature was on the floor last tick.
- Creatures are never stamped and never need terrain shapes.
- **Bench gate** (`bench_world`, all of): Plan 4's conditions plus
  `creature_ms_mean ≤ 1.0` and `creatures_mean ≥ 30`.
- `./build.sh --test --verify` passes; `scons tests tsan=yes` stays clean.

## Review Focus

1. **Buried creature.** Sand poured onto a creature, or `fill_rect` of rock across it, never
   leaves it inside blocking cells for more than one tick: it's pushed up out of rock, and
   powder inside it is knocked out. (Test in Task 1.)
2. **Fast creature.** A creature moving 2000 cells/s into a 1-cell rock wall stops at the wall,
   never past it, in both axes. (Test in Task 1.)
3. **Pinned under a body.** A crate dropped on a standing creature rests on its head without
   the creature sinking into the ground or the crate jittering by more than half a cell.
   (Test in Task 2.)
4. **Crowds.** 40 creatures packed in a pit don't overlap each other (Box2D) or the walls
   (grid), and the pass stays within budget. (Tests in Task 2 and the bench.)
5. **Freeing.** Freeing a `PixelCharacter`, or the world first, never touches a destroyed id;
   setting `velocity` on an unregistered character is a no-op. (Test in Task 3.)

---

## File Structure

```
src/core/creature.h / .cpp          # NEW: blocking rule, sweep, unstick, powder knock-out, liquid scan
src/core/physics.h / .cpp           # creatures: add_creature, grid pass after the step, contacts floor, stats
src/godot/pixel_character.h / .cpp  # NEW node
src/godot/pixel_world.h / .cpp      # registry generalised to Node2D (bodies and characters), stats
src/godot/pixel_rigid_body.cpp      # unchanged API; registry type change only
src/godot/register_types.cpp, build.sh
tests/core/test_creature.cpp        # NEW
tests/godot/run_tests.gd            # + character tests
demo/creatures_demo.tscn / .gd      # NEW: player, crowd, crates, car
bench/bench_world.gd, bench/results/macos-m1-plan5.json, docs/bench-agent.md
```

---

### Task 1: The grid pass in the core

**Files:** Create `src/core/creature.h`, `src/core/creature.cpp`, `tests/core/test_creature.cpp`

**Interfaces:**
- Consumes: `WorldGrid`, `MaterialTable`, `ParticlePool::emit`, `hash5`.
- Produces:
  ```cpp
  struct GridPass {
      float x, y;            // corrected centre
      bool floor = false, ceiling = false;
      int wall = 0;          // -1 left, +1 right, 0 none
      bool stopped_x = false, stopped_y = false;  // the sweep hit something on that axis
      int submerged = 0;     // liquid cells in the hitbox
      MatId liquid = MAT_AIR;  // the most common of them
  };
  bool creature_blocks(const WorldGrid& grid, int x, int y);
  // Sweeps a w x h box from (x0, y0) to (x1, y1): x first, then y, one cell boundary at a time.
  // A blocked horizontal step tries to rise 1..step_height cells first (only if `can_step`).
  // Then: unstick (rise out of blocking solids, up to h cells), knock powder inside the box out
  // as particles (away from the centre), splash liquid if moving fast, count liquid.
  GridPass creature_grid_pass(WorldGrid& grid, ParticlePool* pool, float x0, float y0, float x1,
                              float y1, int w, int h, int step_height, bool can_step,
                              float speed, uint32_t salt);
  ```
  Floor also counts when the row just under the box has a blocking cell under any of its
  columns (so a creature standing still is on the floor).

- [ ] **Step 1: Write the failing tests** (`test_creature.cpp`, Plan 4's `phystest` table, box 8×16):
  - `"walks on flat ground"`: floor at y = 100; from (60, 92) to (70, 92) → x 70, floor true,
    no wall.
  - `"stops at a wall"`: rock column x = 80..81; from (60, 92) to (90, 92) → right edge at 80
    (x = 76), wall +1, stopped_x.
  - `"steps up a ledge of step_height"`: a 3-high step at x ≥ 80 → ends on top (y = 89); a
    4-high step stops it; `can_step = false` stops it at 3 too.
  - `"lands on sand and climbs a sand pile"`: sand floor → floor true; a 3-high sand step is
    climbed.
  - `"liquid does not block and is measured"`: water 0..100 rows 90..100 → falls through to the
    floor, `submerged == 8 * 8`, `liquid == water`.
  - `"body pixels do not block"`: a `FLAG_BODY` rock cell in the path → passes.
  - `"no tunnelling"` (Review Focus 2): from (20, 92) to (2020, 92) through a 1-cell wall at x = 80
    → stops at the wall; same vertically through a 1-cell floor.
  - `"buried creature is freed"` (Review Focus 1): rock painted over the whole box → after
    one pass it's above the rock; sand painted over it → the sand inside the box is gone
    (particles emitted, sand count = before − particles in pool).
  - `"negative coordinates"`: the walk and wall cases mirrored to x < 0, y < 0.
- [ ] **Step 2:** `scons tests` → fails. **Step 3:** Implement. **Step 4:** `scons tests &&
  build/tests/run_tests` → pass.
- [ ] **Step 5:** Commit: `feat(core): creature grid pass — sweep, step-up, unstick, powder knock-out, liquid`.

### Task 2: Creatures in `PhysicsWorld`

**Files:** Modify `src/core/physics.h/.cpp`; test `tests/core/test_physics.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct CreatureDef { int w = 8, h = 16, step_height = 3; float mass = 128, friction = 0.6f,
                       gravity_scale = 1; float x = 0, y = 0; };
  struct CreatureState { bool floor, ceiling; int wall; float submerged_fraction; MatId liquid; };
  int PhysicsWorld::add_creature(const CreatureDef& def);  // same handle space as bodies
  bool PhysicsWorld::is_creature(int handle) const;
  const CreatureState& PhysicsWorld::creature_state(int handle) const;
  void PhysicsWorld::set_particle_pool(ParticlePool* pool);  // for knock-out and splashes
  ```
  `PhysicsStats` gains `creatures` and `creature_ms`.

**Decided here:**
- Creature body: dynamic, `motionLocks.angularZ = true`, one box `w × h`, density `mass / (w·h)`,
  filter category `CAT_CREATURE`, mask `CAT_BODY | CAT_CREATURE`, hit events on,
  `enableSleep = false` (a creature is always driven), `gravityScale` from the def.
- `step()`: record every enabled creature's position, `b2World_Step`, then for each run
  `creature_grid_pass` from the recorded to the new position (`can_step` = last tick's floor,
  speed = |v|, salt = tick), `b2Body_SetTransform` to the result, and zero the velocity
  component it stopped on (x if `stopped_x`; y if floor while vy > 0 or ceiling while vy < 0).
- Floor also comes from Box2D contacts: a touching contact whose normal points from the
  creature down onto the other shape (normal · (0, 1) > 0.7 with the creature as A, or < −0.7
  as B) counts as floor.
- Creatures skip stamping, buoyancy and the terrain-needs scan; the zone rule is the same as
  for bodies.

- [ ] **Step 1: Write the failing tests:**
  - `"a creature falls and stands on the floor"`, `"walking into a crate pushes it"` (set
    vx = 100 each tick toward a crate on the floor → crate x grows), `"a creature stands on a
    crate"` (floor true, y on the crate top), `"two creatures don't overlap"`.
  - `"a crate rests on a standing creature"` (Review Focus 3): after 180 ticks the creature's
    bottom is within 0.5 of the floor and the crate's y varies by < 0.5 over the last 60 ticks.
    If this fails, the fix is a per-creature kinematic floor proxy: a segment under its feet at
    the grid floor, colliding only with creatures; ledger it as a ruling.
  - `"a crowd in a pit"` (Review Focus 4): 40 creatures dropped into a 100-wide pit → after
    240 ticks no two boxes overlap by more than 0.5 cell and none is inside a blocking cell.
  - `"a creature in water reports submersion"`: `creature_state(h).submerged_fraction > 0.5`
    and `liquid == water`.
- [ ] **Step 2:** Run → fail. **Step 3:** Implement. **Step 4:** All pass, TSan clean.
- [ ] **Step 5:** Commit: `feat(core): creatures in PhysicsWorld — upright Box2D bodies corrected by the grid pass`.

### Task 3: `PixelCharacter` node, demo

**Files:** Create `src/godot/pixel_character.h/.cpp`, `demo/creatures_demo.tscn/.gd`; modify
`src/godot/pixel_world.h/.cpp`, `src/godot/register_types.cpp`, `build.sh`, `tests/godot/run_tests.gd`

**Interfaces (GDScript-visible):**
- `PixelCharacter` (`Node2D`): `hitbox_size: Vector2i = (8, 16)`, `step_height: int = 3`,
  `mass: float = 128`, `friction: float = 0.6`, `gravity_scale: float = 1`, `world: NodePath`;
  `velocity: Vector2` (get = Box2D's velocity after the last tick, set = applied now);
  `apply_impulse(impulse)`, `apply_force(force)`, `teleport(position)`; `is_on_floor()`,
  `is_on_wall()`, `get_wall_side() -> int`, `is_on_ceiling()`, `is_in_liquid()`,
  `get_liquid_material() -> StringName`, `get_submerged_fraction() -> float`. Signals `hit`
  and `left_world` as `PixelRigidBody`. It draws nothing; game code adds sprites as children.
- `PixelWorld`: the registry holds any `Node2D` (bodies and characters), `register_character`,
  stats `creatures`, `creature_ms`; `raycast`'s `RAY_BODIES` also hits characters.

**Decided here:** registration, unregistration and world-first freeing work exactly like
`PixelRigidBody` (lazy on the first physics frame, `detach()` on world `PREDELETE`).

- [ ] **Step 1: Write the failing Godot tests:** `test_character_walks_and_lands` (falls onto the
  arena floor, `is_on_floor()`; `velocity = Vector2(100, 0)` for 30 ticks moves it right);
  `test_character_jumps_and_hits_ceiling`; `test_character_swims` (water pool →
  `is_in_liquid()`, `get_liquid_material() == &"water"`); `test_character_free_order`
  (Review Focus 5); `test_character_pushes_body`.
- [ ] **Step 2:** Run → fail. **Step 3:** Implement. **Step 4:** `./build.sh --test --verify` → pass.
- [ ] **Step 5: Demo** `demo/creatures_demo.tscn` (extends the bodies demo script): a player
  character with a coloured sprite, `A`/`D` walk, `W`/`Space` jump on the floor and swim up in
  liquid; 20 wanderers that walk, turn at walls and hop; crates, the car, explosions as in the
  bodies demo; the camera follows the player. Manual check via a capture script in `build/`:
  the player climbs a sand pile and a 3-px ledge, wades into water, pushes a crate and stands
  on it, the car bumps a wanderer, a crate dropped on the player rests on its head.
- [ ] **Step 6:** Commit: `feat(godot): PixelCharacter, creatures demo`.

### Task 4: Benchmark and checkpoint

**Files:** Modify `bench/bench_world.gd`, `docs/bench-agent.md`; create `bench/results/macos-m1-plan5.json`

- [ ] **Step 1:** Workload: every 10 frames spawn 8 wanderer characters in a row above the
  camera (walk at 60 cells/s, turn when `is_on_wall()`, hop every 90 ticks); free those more
  than 600 cells away or that left the world; cap 48. Samples and keys: `creatures_mean`,
  `creature_ms_mean/p99`. Gate per Global Constraints.
- [ ] **Step 2:** `./build.sh --release --test --verify`; run the bench to
  `bench/results/macos-m1-plan5.json` → `budget_pass: true`.
- [ ] **Step 3:** `docs/bench-agent.md`: Plan 5 replaces Plan 4 as the owed Linux run; new
  keys, gate and numbers.
- [ ] **Step 4:** Commit: `bench: creatures in bench_world, macOS M1 Plan 5 results`.
- [ ] **Step 5:** Linux checkpoint: push and hand off — needs the user's go-ahead.
