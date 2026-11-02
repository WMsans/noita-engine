# Addon Layout and GDScript Characters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the engine as a source-only Godot addon under `addons/noita_engine/`, with editable rules in their own folder, and replace the C++ creature system with GDScript characters built on `PixelRigidBody` and batched C++ grid queries.

**Architecture:** First a pure move into the addon layout (no behaviour change, all tests green). Then generic C++ additions: grid queries in core, rigid-body properties and cell proxies in `PhysicsWorld`, and their Godot bindings plus a `bodies_stepped` signal. Then the C++ creature code is deleted in the same task that adds the GDScript `PixelCharacter`/`CharacterSystem`, so `class_name PixelCharacter` never collides with a registered native class. The car controller, bench and READMEs come last.

**Tech Stack:** C++17, godot-cpp (Godot 4.7), Box2D v3.2.0, SCons, doctest, GDScript, bash.

**Spec:** `docs/superpowers/specs/2026-11-02-addon-layout-design.md`. Read it before any task; this plan only adds the decisions the spec leaves to the builder.

## Global Constraints

- Godot 4.7 only; `compatibility_minimum = "4.7"` stays.
- Addon root: `addons/noita_engine/`. All internal resource paths are `res://addons/noita_engine/...`; resource `uid=` values are kept unchanged.
- Folders: `engine/core`, `engine/godot` (non-editable C++, `.gdignore`), `rules/` (editable C++, `.gdignore`), `textures/blocks`, `textures/rooms`, `demo/` (scripts, scenes), `demo/resources/{materials,worldgen,rooms}` (`.tres`).
- `addons/noita_engine/build.sh` and `addons/noita_engine/SConstruct` are the only build files; root `build.sh`/`SConstruct` are moved, not copied.
- Pinned thirdparty: `godot-cpp` `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`, `box2d` `178495ba35695aefe9c321e569d2277f06db3b90` (v3.2.0).
- No C++ creature code remains: no `creature.*`, `CreatureDef`, `CreatureState`, `CAT_CREATURE`, `add_creature`, `creature_pass`, native `PixelCharacter`, `register_character`, `stats.creatures`, `stats.creature_ms`.
- Grid queries: unloaded cells always block; `FLAG_BODY` cells never block; box sizes ≤ 0 or > 64 are rejected with an error and an empty result.
- Constants carried from `src/core/creature.cpp` / `physics.cpp`: `SPLASH_SPEED = 60` cells/s, `KNOCK_SPEED = 2` cells/tick, unstick 64 up then 32 each side, `FOOT_REACH = 4`, `PROXY_REACH = 2`, contact normal threshold 0.7.
- Particle velocities in `displace_cells` are in `ParticlePool::emit` units (cells/tick).
- The bench's character time is report-only: it never affects `budget_pass`.
- Every task ends with `addons/noita_engine/build.sh --test --verify` passing (from Task 1 on).

## Review Focus

1. **A `bodies_stepped` handler frees a character, a crate or the world mid-tick.** Expect no crash and the tick to continue (or end cleanly if the world itself is gone). Test lives in Task 6.
2. **A character standing across a chunk border, or next to an unloaded chunk.** Expect it to treat unloaded cells as walls, not fall through. Test in Task 4 (`sweep stops at an unloaded chunk`).
3. **Boxes at negative coordinates and with fractional centres on cell boundaries.** Expect the same results as the old `creature_grid_pass` (`EDGE = 1e-4` rule). Tests in Task 4 (`negative coordinates`, `edge on a cell boundary covers no extra cell`).
4. **The addon copied into a fresh host project builds and loads with only `--thirdparty` given.** Expect `ClassDB.class_exists("PixelWorld")` there. Check in Task 3.
5. **Mismatched array lengths passed to a batched query from GDScript.** Expect an error and an empty result, not a read past the end. Test in Task 6.

---

### Task 1: Move everything into the addon layout

Pure relocation: identical behaviour, all tests green.

**Files:**
- Move (`git mv`): `src/core/*.{h,cpp}` except `sim_rules.*` → `addons/noita_engine/engine/core/`; `src/core/sim_rules.{h,cpp}` → `addons/noita_engine/rules/`; `src/godot/*` → `addons/noita_engine/engine/godot/`; `src/.gdignore` → `addons/noita_engine/engine/.gdignore` and a copy as `addons/noita_engine/rules/.gdignore`; `textures/blocks/*` → `addons/noita_engine/textures/blocks/`; `rooms/*.png{,.import}` → `addons/noita_engine/textures/rooms/`; `rooms/*.tres` → `addons/noita_engine/demo/resources/rooms/`; `materials/*` → `addons/noita_engine/demo/resources/materials/`; `worldgen/*` → `addons/noita_engine/demo/resources/worldgen/`; `demo/*` → `addons/noita_engine/demo/`; `build.sh`, `SConstruct` → `addons/noita_engine/`; `bin/noita_engine.gdextension{,.uid}` → `addons/noita_engine/noita_engine.gdextension{,.uid}`.
- Modify: every moved `.tres`, `.tscn`, `.import`, `.gdextension`; `tests/godot/run_tests.gd`; `bench/*.gd`, `bench/*.tscn`; `project.godot`; `.gitignore`.
- Delete: stray `*.o`/`*.os` files under `src/` and `tests/` (untracked build junk), then the empty `src/`.

**Interfaces:**
- Produces: the layout above; `SConstruct` variables `ADDON` (its own dir) and `THIRDPARTY` (default `<repo>/thirdparty`); CPPPATH `engine/`, `engine/core`, `rules/`. The `#include "core/physics.h"` style in `engine/godot` keeps working because `engine/` is on CPPPATH.

- [ ] **Step 1: Delete build junk and `git mv` the files listed above.** Run `git status --short | grep -v '^R'` afterwards: expected output only the untracked spec/plan docs.

- [ ] **Step 2: Rewrite `res://` paths** with one `sed` over the files listed under Modify, applying the spec's Path migration table plus `res://bin/` → `res://addons/noita_engine/bin/`. Then run
  `grep -rn 'res://\(materials\|worldgen\|rooms\|textures\|demo\|bin\)/' --include='*.tres' --include='*.tscn' --include='*.import' --include='*.gd' --include='*.gdextension' --include='project.godot' .`
  Expected: no output.

- [ ] **Step 3: Adapt `SConstruct`.** Paths relative to its own directory: library to `bin/libnoita_engine<suffix>`; sources `engine/core/*.cpp`, `engine/godot/*.cpp`, `rules/*.cpp`, `<THIRDPARTY>/box2d/src/*.c`; `SConscript(<THIRDPARTY>/godot-cpp/SConstruct)`. Objects go to `build/obj/` via `env.VariantDir("build/obj", ".", duplicate=0)` so no `.o` lands in `engine/` or `rules/`. The tests target globs `engine/core`, `rules`, and `ARGUMENTS["tests_dir"]` (default `<repo>/tests/core`), and outputs `build/tests/run_tests`.

- [ ] **Step 4: Adapt `build.sh`.** `ADDON` = its own dir; `REPO` = `git -C "$ADDON" rev-parse --show-toplevel` if `$REPO/project.godot` exists, else empty. scons runs with `-C "$ADDON"`; `--verify` runs Godot with `--path "$REPO"`. Thirdparty resolution is Task 3; for now pass `thirdparty=$REPO/thirdparty`.

- [ ] **Step 5: Update `.gitignore`**: `addons/noita_engine/bin/`, `addons/noita_engine/build/`, `addons/noita_engine/thirdparty/`; drop the `src/**` lines. Update `project.godot` `run/main_scene` to the moved scene path.

- [ ] **Step 6: Verify.** Run `addons/noita_engine/build.sh --test --verify`. Expected: doctest summary with 0 failures, then `OK: ALL GODOT TESTS PASSED.` Also run `git ls-files addons/noita_engine | grep -E '\.(o|os)$'`; expected no output.

- [ ] **Step 7: Commit.** `git commit -m "refactor: move the engine into addons/noita_engine"`

---

### Task 2: Split the simulation rules

**Files:**
- Create: `addons/noita_engine/rules/rules_common.h`, `rules/movement.cpp`, `rules/chemistry.cpp`, `rules/README.md`
- Delete: `rules/sim_rules.cpp`
- Keep: `rules/sim_rules.h` (unchanged signature)

**Interfaces:**
- Produces: `update_cell(Neighborhood&, int lx, int ly, uint32_t tick, uint32_t seed, const MaterialTable&)`, unchanged, defined in `movement.cpp`. `rules_common.h` declares the `Mover` and `Chem` structs and the shared inline helpers. `movement.cpp` holds `passable`, `step`, `run`, `dir_for`, `next_speed`, `spread_of`, `move_powder`, `move_liquid`, `move_gas`. `chemistry.cpp` holds `react`, `exchange_heat`, `phase_change`, `burn`, `decay`, exposed to `movement.cpp` through one declared function `void run_chemistry(Chem& c)` in `rules_common.h`. The split follows the existing `sim_rules.cpp` order.

- [ ] **Step 1: Split the file by moving code only; no edits to any function body.** The existing `tests/core/test_movement.cpp` and `test_chemistry.cpp` are the tests.
- [ ] **Step 2: Write `rules/README.md`.** Cover, in this order:
  1. Most new rules are data: add a `PixelMaterial` or `PixelReaction` `.tres` (point at `demo/resources/materials/default_set.tres` as the example).
  2. What each C++ file owns.
  3. A worked example: making a powder that turns to liquid next to fire, naming the function to edit.
  4. Rebuild with `../build.sh`.
- [ ] **Step 3: Verify.** Run `addons/noita_engine/build.sh --test --verify`. Expected: same doctest count as Task 1, 0 failures, and `OK: ALL GODOT TESTS PASSED.` Run the determinism check `tests/core` `test_parallel.cpp` cases (part of `--test`).
- [ ] **Step 4: Commit.** `git commit -m "refactor: split simulation rules into movement and chemistry"`

---

### Task 3: Build script works from a copied addon

**Files:**
- Modify: `addons/noita_engine/build.sh`, `addons/noita_engine/SConstruct`
- Create: `addons/noita_engine/README.md`

**Interfaces:**
- Produces: `build.sh --thirdparty <dir>`, env `NOITA_THIRDPARTY`; SCons arg `thirdparty=<dir>`.

- [ ] **Step 1: Thirdparty resolution in `build.sh`.** Order: `--thirdparty`, then `$NOITA_THIRDPARTY`, then `$REPO/thirdparty`, then `$ADDON/thirdparty`. A candidate counts only if it has both `godot-cpp/SConstruct` and `box2d/include/box2d/box2d.h`. If none qualifies: `git clone` both repos into `$ADDON/thirdparty/` at the pinned commits from Global Constraints (`git -C ... checkout <sha>`, then `git submodule update --init` inside `godot-cpp`). Fail with a clear message if `git` is missing or the clone fails.
- [ ] **Step 2: `--test`/`--verify` without `$REPO`.** Print `--test/--verify need the noita-engine development repo (tests/ is not part of the addon); skipping` and exit 0 after the library build.
- [ ] **Step 3: Write `addons/noita_engine/README.md`.** Cover:
  - Copying the folder into `res://addons/`.
  - Prerequisites: scons, a C++17 compiler, git.
  - Running `build.sh` (flags, `--thirdparty`).
  - Where the rules are (`rules/README.md`).
  - That `demo/resources` is example data to copy, not to depend on.
  - The global `class_name`s `PixelCharacter`, `CharacterSystem`, `CarController` and the risk of collisions.
- [ ] **Step 4: Verify in a host project** (Review Focus 4).
  1. In the scratchpad, create `host/project.godot` (minimal `config_version=5`).
  2. Copy `addons/noita_engine/` (without `bin/` and `build/`) to `host/addons/noita_engine/`.
  3. Run `host/addons/noita_engine/build.sh --thirdparty "$PWD/thirdparty"`, then `godot --headless --path host --script <scratch>/check.gd`, where `check.gd` prints `ClassDB.class_exists("PixelWorld")` and quits.

  Expected: build succeeds, the script prints `true`, and `host/addons/noita_engine` contains no `.o`.
- [ ] **Step 5: Verify the clone path** (needs network). Run the same copy with no `--thirdparty` and `NOITA_THIRDPARTY` unset, from a directory outside this repo. Expected: `host/addons/noita_engine/thirdparty/{godot-cpp,box2d}` checked out at the pinned SHAs and the build succeeds. If there's no network, report the step as skipped.
- [ ] **Step 6: Verify in repo.** Run `addons/noita_engine/build.sh --test --verify`. Expected: passes.
- [ ] **Step 7: Commit.** `git commit -m "build: addon build script resolves or fetches thirdparty"`

---

### Task 4: Core grid queries

**Files:**
- Create: `addons/noita_engine/engine/core/grid_query.h`, `grid_query.cpp`
- Test: `tests/core/test_grid_query.cpp`

**Interfaces:**
- Consumes: `WorldGrid::get/set`, `MaterialTable`, `ParticlePool::emit(x, y, vx, vy, cell)`, `RayMask` from `raycast.h`.
- Produces (all coordinates world cells, `y` down, box = centre + integer size):
```cpp
enum BoxSide : int { SIDE_FLOOR = 1, SIDE_CEILING = 2, SIDE_LEFT = 4, SIDE_RIGHT = 8 };
constexpr int MAX_QUERY_BOX = 64;
bool cell_blocks(const WorldGrid&, int x, int y, uint32_t mask);   // unloaded → true, FLAG_BODY → false
bool box_blocked(const WorldGrid&, float x, float y, int w, int h, uint32_t mask);
int  box_sides(const WorldGrid&, float x, float y, int w, int h, uint32_t mask);  // cells in the adjacent row/column
struct BoxSweep { float x, y; bool stopped_x, stopped_y; int sides; };
BoxSweep sweep_box(const WorldGrid&, float x0, float y0, float x1, float y1, int w, int h, uint32_t mask);
struct BoxScan { int solid = 0, powder = 0, liquid = 0; MatId liquid_mat = MAT_AIR; };
BoxScan scan_box(const WorldGrid&, float x, float y, int w, int h);  // FLAG_BODY cells skipped
int displace_box(WorldGrid&, ParticlePool&, float x, float y, int w, int h, uint32_t mask,
                 float vx, float vy, int max_cells, bool outward, float spawn_dy);
```
`sweep_box` is the x-then-y sub-stepped sweep from `creature_grid_pass`, without step-up, floor snap, unstick or knock-out. It uses the same `EDGE = 1e-4f` cell-range rule, and `sides = box_sides(final)`. `displace_box` follows the spec's `displace_cells` rule: top row first, then left to right, up to `max_cells` cells, and the `outward` sign is set by `cx + 0.5 < x`. Port the helpers (`first_cell`, `last_cell`, `Cells`) from `creature.cpp`.

- [ ] **Step 1: Write the failing tests** in `test_grid_query.cpp`, using `phystest::arena` / `sim_test_util.h` as `test_creature.cpp` does. Port the cell-level cases from `test_creature.cpp` lines 32–127, with their geometry and expected values:
  - `sweep walks on flat ground`: `sides & SIDE_FLOOR`, y unchanged.
  - `sweep stops at a wall`: `stopped_x`, x flush with the wall, `SIDE_RIGHT`.
  - `sweep does not tunnel`: a 1-cell wall, moving 40 cells in one call, stops before it.
  - `liquid does not block with solid|powder mask, blocks with RAY_LIQUID`.
  - `body pixels do not block`.
  - `negative coordinates`.
  - `sweep stops at an unloaded chunk`: an arena edge; expected `stopped_x` at the chunk border.
  - `edge on a cell boundary covers no extra cell`: an 8-wide box centred at x = 4.0 against a wall at x = 8 is not blocked; at 4.01 it is.
  - `box_blocked sees powder only with RAY_POWDER`.
  - `scan_box counts`: a 4×4 box half water; expected `liquid == 8`, `liquid_mat == water`, `solid == 0`.
  - `displace_box knocks powder outward`: a 4×4 box of sand, `outward = true`, `vx = 2`; expected returns 16, the cells are air afterwards, `pool` holds 16 particles with negative vx left of centre and positive vx right of it.
  - `displace_box splash takes at most max_cells from the top row first`: a box in water, `max_cells = 3`; expected 3 cells removed, all from the box's top row.
- [ ] **Step 2: Run** `addons/noita_engine/build.sh --test`. Expected: compile error, `grid_query.h` not found.
- [ ] **Step 3: Implement** `grid_query.{h,cpp}`.
- [ ] **Step 4: Run** `addons/noita_engine/build.sh --test`. Expected: all pass.
- [ ] **Step 5: Commit.** `git commit -m "feat(core): batched-friendly grid box queries"`

---

### Task 5: Generic rigid-body properties, cell proxies and contacts in PhysicsWorld

Creature code stays for now. This task only adds.

**Files:**
- Modify: `addons/noita_engine/engine/core/physics.h`, `physics.cpp`
- Test: `tests/core/test_physics.cpp`

**Interfaces:**
- Consumes: `cell_blocks`, `box_sides` (Task 4).
- Produces:
```cpp
struct BodyProps {
	float mass = 0;  // 0 = from pixels
	float gravity_scale = 1, linear_damping = 0, angular_damping = 0;
	bool lock_rotation = false, terrain_collision = true, can_sleep = true;
	bool cell_proxies = false;
	int proxy_step_height = 0;
};
// BodyDef gains: int box_w = 0, box_h = 0;  // used when pixels is empty: an unstamped box
//                BodyProps props;
void PhysicsWorld::set_props(int handle, const BodyProps&);   // live; rebuilds the filter / proxies as needed
const BodyProps& PhysicsWorld::props(int handle) const;
b2Vec2 PhysicsWorld::previous_position(int handle) const;      // recorded at the start of step()
void PhysicsWorld::place_cell_proxies();                       // every enabled body with props.cell_proxies
int  PhysicsWorld::contact_sides(int handle) const;            // BoxSide bits from touching contacts, own proxies excluded
```
`add_body` accepts an empty `pixels` with `box_w, box_h > 0`; it then needs `props.mass > 0` (returns -1 otherwise). Density is `mass / (box_w * box_h)`. The body is never stamped and gets no buoyancy. With `terrain_collision = false`, the `CAT_TERRAIN` bit is dropped from the shape's mask, and terrain shapes stop masking that body. Proxy geometry and placement are moved from `place_proxies` / `set_proxy` / the proxy creation in `add_creature`, generalised over `Body` with `props.cell_proxies`. The blocking test is `cell_blocks(..., RAY_SOLID | RAY_POWDER)`. Wall segments and the wall-column search skip the bottom `proxy_step_height` rows (physics.cpp:170 and :449 today).

- [ ] **Step 1: Write failing tests** in `test_physics.cpp`:
  - `a box body without pixels is unstamped and falls`: `box_w = 8, box_h = 16, mass = 128`, `terrain_collision = true`. Expected: rests on the floor, and `stamp()` writes no `FLAG_BODY` cell.
  - `a box body without mass is refused`: expected `add_body` returns -1.
  - `terrain_collision off falls through terrain shapes`: expected y keeps increasing past the floor row after 60 steps.
  - `cell proxies stop a crate resting on a grid-driven box`: port `a crate rests on a standing creature` using a box body with `terrain_collision = false, cell_proxies = true`, and hold it on the floor by calling `sweep_box` from `previous_position` each tick. Expected: the box stays at the floor row.
  - `proxies leave the bottom proxy_step_height rows open`: a 3-cell ledge next to the box, `proxy_step_height = 3`. Expected: Box2D moves the box's x into the ledge column, so `previous_position.x` < x.
  - `contact_sides reports a crate under the box as floor and ignores own proxies`.
  - `set_props live: lock_rotation keeps angle 0 under torque; gravity_scale 0 floats`.
  - `previous_position is the pose before the step`.
- [ ] **Step 2: Run** `addons/noita_engine/build.sh --test`. Expected: compile errors for the new members.
- [ ] **Step 3: Implement** the code above.
- [ ] **Step 4: Run** `addons/noita_engine/build.sh --test --verify`. Expected: all pass, and the creature tests still pass.
- [ ] **Step 5: Commit.** `git commit -m "feat(physics): body props, unstamped box bodies, cell proxies, contact sides"`

---

### Task 6: Godot bindings: rigid-body properties, `joined`, `bodies_stepped`, batched queries

**Files:**
- Modify: `engine/godot/pixel_rigid_body.{h,cpp}`, `pixel_physics_node.{h,cpp}`, `pixel_world.{h,cpp}`
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Consumes: Task 4 and Task 5 APIs.
- Produces (GDScript-visible):
  - `PixelRigidBody` properties `body_size: Vector2i`, `mass: float`, `gravity_scale`, `linear_damping`, `angular_damping: float`, `lock_rotation`, `terrain_collision`, `cell_proxies`, `can_sleep: bool`, `proxy_step_height: int`. All are live through `PhysicsWorld::set_props` once joined. A null `texture` with a nonzero `body_size` builds a box `BodyDef`.
  - `get_previous_position() -> Vector2`, in node space, converted the same way as the node transform.
  - `teleport(position: Vector2, rotation: Variant = null)`: null keeps the current rotation. Existing two-argument calls are unchanged.
  - `PixelPhysicsNode` `signal joined()`, emitted right after `try_join` gets a handle.
  - `PixelWorld` `signal bodies_stepped()`, emitted between the Box2D step and `place_cell_proxies()`.
  - Constants `PixelWorld.SIDE_FLOOR = 1`, `SIDE_CEILING = 2`, `SIDE_LEFT = 4`, `SIDE_RIGHT = 8`, `STOPPED_X = 1`, `STOPPED_Y = 2`, plus the existing `RAY_SOLID = 1`, `RAY_POWDER = 2`, `RAY_LIQUID = 4`.
  - `sweep_boxes(from: PackedVector2Array, to: PackedVector2Array, sizes: PackedVector2Array, mask: int) -> Dictionary` with keys `position` (`PackedVector2Array`), `stopped` (`PackedInt32Array`), `sides` (`PackedInt32Array`).
  - `probe_boxes(centres: PackedVector2Array, sizes: PackedVector2Array, mask: int) -> PackedByteArray`.
  - `scan_boxes(centres, sizes) -> Dictionary` with keys `solid`, `powder`, `liquid` (`PackedInt32Array`) and `liquid_material` (`PackedStringArray`, `""` for none).
  - `displace_cells(centres, sizes, mask: int, velocities: PackedVector2Array, max_cells: PackedInt32Array, outward: bool, spawn_dy: float) -> PackedInt32Array`.
  - `body_contacts(bodies: Array) -> PackedInt32Array`, giving 0 for an entry that is not a joined `PixelRigidBody`.

  Positions and sizes are in the world's cell space, the same space as `raycast`. Mismatched lengths, or any size ≤ 0 or > 64, give `ERR_PRINT` plus an empty result. Sizes round to the nearest int.
- `tick_world` order: `update_terrain` → `step` → record `ObjectID` of self → `emit_signal("bodies_stepped")` → if `ObjectDB::get_instance(self_id)` is null, return → `place_cell_proxies` → `stamp` → `sim->tick` → particles → `unstamp` → `sync_bodies`.

- [ ] **Step 1: Write failing Godot tests**, reusing the `_make_world` / `_register` / `_make_body` helpers:
  - `test_box_body_properties`: a `PixelRigidBody` with `body_size = Vector2i(8, 16)`, `mass = 128`, `lock_rotation = true` falls and lands. Expected: rotation stays 0 and `get_mass()` is about 128.
  - `test_joined_signal_fires_once`.
  - `test_bodies_stepped_order`: the handler records `get_previous_position()` and `position`. Expected: they differ while falling, and a `teleport` done in the handler shows in `position` the same frame.
  - `test_bodies_stepped_handler_may_free` (Review Focus 1): handlers that free a crate, then in another run the world itself. Expected: no crash, the test returns true.
  - `test_sweep_probe_scan_displace_batched`: three boxes in one call each, results matching Task 4's expectations per box.
  - `test_batched_query_rejects_bad_input` (Review Focus 5): length mismatch and a size of 65. Expected: empty results.
  - `test_teleport_keeps_rotation_when_omitted`.
- [ ] **Step 2: Run** `addons/noita_engine/build.sh --verify`. Expected: the new tests fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `addons/noita_engine/build.sh --test --verify`. Expected: all pass. Add the new test names to the `build.sh` list of tests whose stderr is expected, where they print errors on purpose (`test_batched_query_rejects_bad_input`).
- [ ] **Step 5: Commit.** `git commit -m "feat(godot): rigid-body props, joined and bodies_stepped signals, batched grid queries"`

---

### Task 7: Replace the C++ creature with the GDScript character

**Files:**
- Delete: `engine/core/creature.{h,cpp}`, `engine/godot/pixel_character.{h,cpp}`, `tests/core/test_creature.cpp`
- Modify: `engine/core/physics.{h,cpp}` (remove the creature members listed in Global Constraints and the `step()` creature pass; `PhysicsStats` loses `creatures` and `creature_ms`), `engine/godot/pixel_world.{h,cpp}` (remove `register_character` and the creature stats keys), `engine/godot/pixel_physics_node.h` (comment), `engine/godot/register_types.cpp`
- Create: `addons/noita_engine/demo/pixel_character.gd`, `addons/noita_engine/demo/character_system.gd`
- Test: `tests/godot/run_tests.gd`, `tests/core/test_physics.cpp`

**Interfaces:**
- Consumes: everything from Task 6.
- Produces:
  - `class_name PixelCharacter extends PixelRigidBody`.
    - Exported `hitbox_size: Vector2i = Vector2i(8, 16)` (setter writes `body_size`) and `step_height: int = 3` (setter writes `proxy_step_height`).
    - `_init` sets `mass = 128`, `friction = 0.6`, `gravity_scale = 1`, `terrain_collision = false`, `cell_proxies = true`, `lock_rotation = true`, `body_size = hitbox_size`, `proxy_step_height = step_height`.
    - `var velocity: Vector2` proxies `linear_velocity`.
    - Queries: `is_on_floor()`, `is_on_ceiling()`, `is_on_wall()`, `get_wall_side() -> int` (-1, 0 or 1), `is_in_liquid()`, `get_submerged_fraction() -> float`, `get_liquid_material() -> StringName`.
    - Internal state the system writes: `_floor`, `_ceiling`, `_wall`, `_submerged`, `_liquid`, and `_was_on_floor`.
    - On `joined` it calls `CharacterSystem.of(get_world()).add(self)`; on `tree_exiting` it calls `remove(self)`.
  - `class_name CharacterSystem extends Node`.
    - `static func of(world: PixelWorld) -> CharacterSystem` finds or creates a child of the world named `CharacterSystem`.
    - `func add(c: PixelCharacter)` and `func remove(c: PixelCharacter)`.
    - `var last_tick_usec: int`, used by the bench.
    - It connects to `bodies_stepped` and runs the spec's steps 1–7 (section "`demo/character_system.gd`") with the Global Constraints constants. Invalid (freed) characters are dropped at the start of each tick.

- [ ] **Step 1: Port the Godot character tests.** Keep `test_character_walks_and_lands`, `jumps_and_hits_ceiling`, `swims`, `free_order`, `pushes_body` and `script_subclasses_join_the_world` with their thresholds. Replace the `stats.creatures` checks with `stats.bodies`; the scripted test now expects 2 bodies. Add these tests, which port `test_creature.cpp` lines 51–339:
  - `test_character_steps_up_3_cell_ledge`: walks onto it, final y is 3 higher.
  - `test_character_refuses_4_cell_ledge`: x stops at the wall.
  - `test_character_climbs_sand_pile`.
  - `test_character_knocks_sand_out`: sand dropped onto the head; expected the hitbox cells are free of powder within 2 ticks and `get_stats().particles_material > 0`.
  - `test_character_splashes`: falls into water at > 60 cells/s; expected material particles appear.
  - `test_character_stands_on_crate`: `is_on_floor()`.
  - `test_crate_rests_on_character`: the character's y holds.
  - `test_buried_character_is_freed`: rock filled over it; expected out within 1 tick, deep rock (40 cells) included.
  - `test_character_walks_down_staircase_on_floor`: `is_on_floor()` every tick.
  - `test_two_characters_dont_overlap`.
  - `test_crowd_in_a_pit`: 20 characters, none inside solid after 120 ticks.
- [ ] **Step 2: Delete the C++ creature files and members, and write the two GDScript files.**
- [ ] **Step 3: Remove the creature-only cases from `test_physics.cpp`.** Also check `grep -rn "creature\|Creature\|CAT_CREATURE\|PixelCharacter" addons/noita_engine/engine tests/core`; expected no output.
- [ ] **Step 4: Run** `addons/noita_engine/build.sh --test --verify`. Expected: all pass. Run `godot --headless --path . --quit-after 600 addons/noita_engine/demo/creatures_demo.tscn` and `lighting_demo.tscn`; expected no script errors.
- [ ] **Step 5: Commit.** `git commit -m "feat: GDScript PixelCharacter on grid queries; remove the C++ creature"`

---

### Task 8: Car controller

**Files:**
- Create: `addons/noita_engine/demo/car_controller.gd`, `addons/noita_engine/demo/textures_util.gd`
- Modify: `addons/noita_engine/demo/bodies_demo.gd`, `creatures_demo.gd`
- Test: `tests/godot/run_tests.gd`

**Interfaces:**
- Produces:
  - `class_name TexturesUtil`, holding static `box(w: int, h: int, colour: Color) -> ImageTexture` and `disc(diameter: int, colour: Color) -> ImageTexture`, moved from `bodies_demo.gd`.
  - `class_name CarController extends Node2D` with these exports and today's values as defaults:

    | export | default |
    | --- | --- |
    | `chassis_size` | `Vector2i(40, 8)` |
    | `chassis_colour` | `Color(0.75, 0.2, 0.15)` |
    | `chassis_material` | `&"wood"` |
    | `wheel_diameter` | `12` |
    | `wheel_colour` | `Color(0.15, 0.15, 0.15)` |
    | `wheel_material` | `&"rock"` |
    | `wheel_offsets` | `PackedVector2Array([Vector2(-14, 10), Vector2(14, 10)])` |
    | `wheel_friction` | `1.5` |
    | `motor_speed` | `12.0` |
    | `motor_torque` | `3e6` |
    | `wheel_frequency` | `5.0` |

    `func drive(direction: float)` (-1..1, multiplied by `motor_speed`); `func set_braking(on: bool)`; `var chassis: PixelRigidBody`, read-only once built. Bodies are built in `_ready` as children, at the controller's position.

- [ ] **Step 1: Point the existing car test** (around `run_tests.gd:557`) at a `CarController` with `drive(1.0)`. Keep the ≥ 40 cells threshold; add `test_car_brakes`: after `set_braking(true)` at speed, |chassis linear_velocity.x| < 1 within 120 ticks.
- [ ] **Step 2: Run** `addons/noita_engine/build.sh --verify`. Expected: fail, `CarController` not found.
- [ ] **Step 3: Implement**, and make `bodies_demo.gd` use `CarController` with the camera on `car.chassis`.
- [ ] **Step 4: Run** `addons/noita_engine/build.sh --test --verify`. Expected: pass. Run `bodies_demo.tscn` headless for 600 frames; expected no script errors.
- [ ] **Step 5: Commit.** `git commit -m "feat(demo): GDScript car controller"`

---

### Task 9: Bench reports the GDScript character cost

**Files:**
- Modify: `bench/bench_world.gd`, `docs/` bench agent doc if it names `creature_ms`

**Interfaces:**
- Consumes: `CharacterSystem.of(world).last_tick_usec`, `PixelCharacter`.

- [ ] **Step 1: Replace the creature stats.** `stats.creatures` becomes the count of live `PixelCharacter`s the bench spawned. `creature_ms` becomes `last_tick_usec / 1000.0`, sampled each tick and reported as `character_ms_mean` and `character_ms_p99`. `CREATURE_BUDGET_MS` stays in the output as `character_budget_ms` but is removed from the `budget_pass` expression. `CREATURES_BUDGET` (30 alive on average) stays in `budget_pass`.
- [ ] **Step 2: Run** `godot --headless --path . bench/bench_world.tscn`. Expected: the JSON contains `character_ms_mean`, the creatures-alive mean is ≥ 30, and `budget_pass` doesn't depend on `character_ms_mean`. Report `character_ms_mean` next to the last committed `creature_ms` from `bench/results/` for the same machine, if there is one.
- [ ] **Step 3: Commit.** `git commit -m "bench: report GDScript character time (report-only)"`
