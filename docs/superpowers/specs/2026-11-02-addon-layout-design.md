# Addon layout, GDScript characters and grid queries — design

Date: 2026-11-02

## Intent

Make this project droppable into another Godot 4.7 project as an addon, shipped as C++
source, not a prebuilt library, so the host project can still edit and add simulation rules
and compile its own library. The rules must be easy to find and change.

At the same time, move gameplay out of C++. The C++ creature system (`PixelCharacter`, the
creature grid pass, creature bodies in `PhysicsWorld`) is removed completely. Characters
become plain `PixelRigidBody` nodes whose behaviour is written in GDScript. C++ keeps the
heavy lifting: Box2D, and a set of batched, generic terrain queries that GDScript calls a few
times per frame for all characters at once. The car demo gets its own GDScript controller.

## Goals

- One folder, `addons/noita_engine/`. A host project copies it in, runs its build script,
  and nothing else.
- Four clearly separated kinds of content in that folder: engine C++ (`engine/`), editable
  C++ simulation rules (`rules/`), texture assets (`textures/`), demo GDScript + scenes
  (`demo/`).
- A compile script inside the addon that works in a host project without this repo.
- No C++ creature code left: `creature.{h,cpp}`, `CreatureDef/CreatureState`,
  `CAT_CREATURE`, `add_creature`, `creature_pass`, `PixelCharacter` (C++) all removed.
- `PixelRigidBody` exposes the body properties GDScript needs to drive a character
  (velocity, mass, gravity scale, damping, rotation lock, terrain collision on/off, …).
- Batched C++ terrain queries on `PixelWorld`, generic, not character-specific.
- Every current creature behaviour reproduced in GDScript (`demo/`): walking and the
  floor/ceiling/wall flags, stepping up ledges of up to `step_height` cells, getting pushed out
  of cells that close over the body, knocking powder out of the hitbox as particles, liquid
  detection (submerged fraction + dominant liquid), splashing when entering liquid fast,
  standing on rigid bodies and other characters, pushing bodies.
- Performance target: **dozens** of GDScript characters at once (bench budget stays 30
  alive on average). The number of query calls per frame must not grow with the character
  count.
- Existing tests and benches keep working (character tests ported, not dropped).

## Non-goals

- Moving Box2D, joints, lighting, streaming or worldgen out of C++.
- Godot versions other than 4.7; prebuilt binaries for other platforms.
- An editor plugin (`plugin.cfg`). A GDExtension registers its classes without one.

## Anti-goals

- Character behaviour hidden in C++ again under another name. The C++ API answers
  questions about cells ("where does this box stop", "what is in this box") and does bulk
  edits; it never decides to step, swim or splash.
- A per-character, per-cell GDScript loop. Any GDScript loop over cells is a failure of the
  query API.

## Approaches considered

1. **Box2D terrain + GDScript extras.** Character is an ordinary rotation-locked
   `PixelRigidBody` colliding with Box2D terrain shapes; GDScript adds floor/liquid checks and
   ledge teleports. Least C++, but terrain shapes trail the pixels by up to 50 ms, powder is
   rigid terrain (no wading, no knock-out), and stepping over chain edges is jerky. Rejected.
2. **Grid-driven rigid body — chosen.** The character body opts out of Box2D terrain
   collision, so Box2D handles only body/body contacts. Right after the Box2D step,
   `PixelWorld` hands control to GDScript, which corrects every character against the cells
   with a handful of batched C++ queries. Today's feel is kept; the C++ side is generic.
3. **Keep the C++ grid pass as a switch on `PixelRigidBody`.** Rejected: the behaviour would
   stay in C++, which is exactly what the user wants out.

Layout: an `addons/noita_engine/` folder (conventional Godot addon path, all internal paths
`res://addons/noita_engine/...`) was chosen over a root-level split, which would collide with
host project folders.

## Layout

```
addons/noita_engine/
  README.md                 # copy in, build, where the rules are, how to add one
  noita_engine.gdextension  # libraries at res://addons/noita_engine/bin/...
  build.sh                  # the compile script
  SConstruct
  bin/                      # build output (gitignored)
  engine/                   # ALL non-editable C++   (.gdignore)
    core/                   # was src/core/ minus creature.*, sim_rules.*
    godot/                  # was src/godot/ minus pixel_character.*
  rules/                    # EDITABLE C++ — the simulation rules   (.gdignore)
    README.md               # what each file does, how to add a rule
    sim_rules.h             # update_cell() entry point, unchanged signature
    movement.cpp            # powder / liquid / gas movement
    chemistry.cpp           # reactions, heat, phase change, burning, decay
    rules_common.h          # the Mover / Chem helpers both files share
  textures/
    blocks/*.png            # was textures/blocks/
    rooms/*.png             # was rooms/*.png
  demo/
    *.gd *.tscn             # was demo/
    resources/              # example data; a host project makes its own
      materials/*.tres      # was materials/
      worldgen/*.tres       # was worldgen/
      rooms/*.tres          # was rooms/*.tres
    pixel_character.gd      # NEW: one character's properties, state and input surface
    character_system.gd     # NEW: runs all characters' grid behaviour, batched
    car_controller.gd       # NEW: the car, pulled out of bodies_demo.gd
```

Outside the addon, kept for development only: `project.godot`, `thirdparty/`, `tests/`,
`bench/`, `docs/`, `icon.svg`. The root `build.sh` and `SConstruct` are removed; the
addon's are the only ones. `project.godot`'s main scene moves to the addon's demo path.

The `.tres` data (materials, reactions, worldgen, rooms) lives in `demo/resources/`
(user's decision): it is example content. A host project is expected to write its own
material set and generator and point its `PixelWorld` at them; the demo set is a starting
point to copy from.

### Rules refactor

`sim_rules.cpp` (344 lines) mixes movement and chemistry with private helpers. It is split
along that seam into `movement.cpp` and `chemistry.cpp`, with the shared helpers in
`rules_common.h`; `update_cell()` stays the single entry point the simulator calls. Pure
move, no behaviour change. The core tests must pass unchanged. Every C++ file the engine
needs from the rules is listed in `rules/README.md`, along with a worked "add a rule"
example. Material/reaction data (`demo/resources/materials`) is how most new rules are added
and the README says so first.

## Path migration

| old | new |
| --- | --- |
| `res://materials/` | `res://addons/noita_engine/demo/resources/materials/` |
| `res://worldgen/` | `res://addons/noita_engine/demo/resources/worldgen/` |
| `res://rooms/*.tres` | `res://addons/noita_engine/demo/resources/rooms/` |
| `res://rooms/*.png` | `res://addons/noita_engine/textures/rooms/` |
| `res://textures/` | `res://addons/noita_engine/textures/` |
| `res://demo/` | `res://addons/noita_engine/demo/` |
| `res://bin/` | `res://addons/noita_engine/bin/` |

Affected: all `.tres`, `.tscn`, `.import` `source_file`, `.gdextension`,
`tests/godot/run_tests.gd`, `bench/*.gd`, `bench/*.tscn`, `project.godot`. Resource `uid=`
values are kept; the `path=` fallback is what makes the copy work in a host project.

## C++ changes

### Removed

- `core/creature.{h,cpp}`, `tests/core/test_creature.cpp`.
- From `PhysicsWorld`: `CreatureDef`, `CreatureState`, `add_creature`, `is_creature`,
  `creature_state`, `creature_pass`, `place_proxies`, `set_proxy`, the creature fields of
  `Body`, `CAT_CREATURE`, `stats_.creatures`, `stats_.creature_ms`.
- `godot/pixel_character.{h,cpp}`, its registration, `PixelWorld::register_character`.

### `PixelRigidBody` additions

Properties, live-settable after joining except `body_size`:

| property | meaning |
| --- | --- |
| `body_size: Vector2i` | used when `texture` is null: an axis-aligned box of that size, no pixels, never stamped. This is how a character body is made. Read at join only: setting it on a joined body warns, because a live resize would mean restamping the cells. |
| `mass: float` | 0 = from pixels (today's behaviour); > 0 overrides. Required > 0 for a `body_size` body. |
| `gravity_scale: float` | Box2D gravity scale. |
| `linear_damping`, `angular_damping: float` | Box2D damping. |
| `lock_rotation: bool` | Box2D angular motion lock. |
| `terrain_collision: bool` | default true. False: the body's filter drops `CAT_TERRAIN`, so the cells are GDScript's job. |
| `cell_proxies: bool` | default false; see below. Only meaningful with `terrain_collision = false`. |
| `proxy_step_height: int` | default 0. The bottom rows the side proxies leave open so GDScript can step up ledges; `PixelCharacter` sets it to its `step_height`. |
| `can_sleep: bool` | Box2D sleep allowed. |

Existing `get_mass()` stays and reports the effective mass.

**Cell proxies** (moved, not removed, my call). Today's creature proxies tell Box2D about
the blocking cells next to the box (four frictionless kinematic segments) so a crate on a
character's head, or a character pushing a crate into a wall, is stopped instead of
pressing the character into cells. That is heavy per-tick collision plumbing, not
behaviour, so it stays in C++ as a generic rigid-body option. Proxies are placed after the
GDScript correction (see tick order), using the same "blocks" rule as `sweep_boxes`
with `RAY_SOLID | RAY_POWDER`. Geometry is today's: the left/right segments cover only the
box above `proxy_step_height` and the wall-column search skips those bottom rows (otherwise
Box2D would stop a character at every ledge before GDScript could step it up);
`FOOT_REACH = 4` and `PROXY_REACH = 2` stay constants.

**Joining.** `PixelPhysicsNode` gains `signal joined()`, emitted right after a successful
join (my call). `PixelCharacter` registers with `CharacterSystem` on it.

Category: body-sized boxes keep `CAT_BODY`, so they collide with bodies and with each other
exactly like creatures did.

### Tick order and the hand-off to GDScript

```
update_terrain → step (Box2D) → emit bodies_stepped → place cell proxies
  → stamp → sim tick → particles → unstamp → sync_bodies
```

`PixelWorld` gains `signal bodies_stepped()`, emitted synchronously once per tick right
after the Box2D step. Handlers may move bodies (`teleport`, velocity) and edit cells.
Before emitting, `PixelWorld` records each body's pre-step position, available as
`PixelRigidBody.get_previous_position()`: the start of this tick's sweep.

Bodies already sync their node transform in `sync_bodies`, which runs after the handler,
so a correction is visible the same frame.

### Batched grid queries on `PixelWorld`

All inputs and outputs are packed arrays, one entry per box, in world cell coordinates,
`y` down. Boxes are given as centre + size. Mask uses the existing `RAY_SOLID`,
`RAY_POWDER`, `RAY_LIQUID` bits for "what blocks". Unloaded chunks always block. Stamped
body pixels (`FLAG_BODY`) never block.

1. `sweep_boxes(from: PackedVector2Array, to: PackedVector2Array, sizes:
   PackedVector2Array, mask: int) -> Dictionary`
   Moves each box from `from` to `to`, x first, then y, one cell per sub-step, so
   nothing tunnels. Returns `position: PackedVector2Array` (where it stopped),
   `stopped: PackedInt32Array` (bit 0 x, bit 1 y), `sides: PackedInt32Array` (touching
   bits: floor, ceiling, left, right, measured at the final position).

2. `probe_boxes(centres, sizes, mask) -> PackedByteArray`
   1 if the box overlaps a blocking cell. Used for "is a step up of k cells free" (GDScript
   submits all candidates for all characters in one call) and "is the box stuck inside
   cells".

3. `scan_boxes(centres, sizes) -> Dictionary`
   Per box: `solid`, `powder`, `liquid` cell counts (`PackedInt32Array`), and
   `liquid_material: PackedStringArray` (the most common liquid, `""` if none).

4. `displace_cells(centres, sizes, mask, velocities: PackedVector2Array, max_cells:
   PackedInt32Array, outward: bool, spawn_dy: float) -> PackedInt32Array`
   Scans each box top row first, left to right, and replaces up to `max_cells` cells
   matching `mask` with air, emitting each as a material particle at
   `(cx + 0.5, cy + spawn_dy)`. With `outward = false` the particle velocity is the box's
   `velocities` entry as given. With `outward = true` the x component becomes
   `abs(v.x)` with the sign of the cell's side of the box centre (left half → negative).
   Returns how many cells each box displaced.
   - Knock-out (today): mask powder, `max_cells` = box area, `v = (KNOCK_SPEED,
     -KNOCK_SPEED)`, `outward = true`, `spawn_dy = 0.5`.
   - Splash (today): mask liquid, `max_cells = min(3, speed / 60)`, `v = (±1.5 or 0 along
     the x direction of travel, -3)`, `outward = false`, `spawn_dy = -0.5`.

5. `body_contacts(bodies: Array[PixelRigidBody]) -> PackedInt32Array`
   Per body, the side bits (floor/ceiling/left/right) of its touching Box2D contacts with
   other bodies, from the contact normals (|n| component > 0.7, as today). Its own cell
   proxies are excluded (my call). Cell floors come from the sweep's `sides` instead, which
   checks the row right under the box, so the result matches today's floor flag. Covers
   "standing on a crate or another character counts as floor".

Each call is O(cells covered) in C++, with no GDScript callback inside. Out of range sizes
(≤ 0 or > 64 cells) are rejected with an error and an empty result.

`raycast` and the existing per-cell helpers stay.

## GDScript

### `demo/pixel_character.gd` — `class_name PixelCharacter extends PixelRigidBody`

Sets itself up in `_init`: `body_size = hitbox_size`, `terrain_collision = false`,
`cell_proxies = true`, `lock_rotation = true`, `friction`, `mass`, `gravity_scale` from its
exports. Registers itself with the `CharacterSystem` of its world when it joins, and
unregisters on exit.

Public surface kept from the C++ class so game code, tests and the bench barely change:
`hitbox_size`, `step_height`, `mass`, `friction`, `gravity_scale`, `velocity` (alias for
`linear_velocity`), `apply_impulse`, `apply_force`, `teleport`, `is_on_floor`,
`is_on_ceiling`, `is_on_wall`, `get_wall_side`, `is_in_liquid`, `get_submerged_fraction`,
`get_liquid_material`, signals `hit` and `left_world` (inherited). State is stored on the
node by `CharacterSystem` each tick.

Because it extends `PixelRigidBody`, a script that extends `PixelCharacter` with its own
`_physics_process` still joins: joining is on the internal tick in C++, as today.

### `demo/character_system.gd` — `class_name CharacterSystem extends Node`

One per world (created on demand by the first character, as a child of the `PixelWorld`).
Connects to `bodies_stepped` and, each tick, does for **all** registered characters
together:

1. `sweep_boxes` from previous to current position, mask solid + powder.
2. For those stopped on x and walking: build every 1..`step_height` raised candidate, one
   `probe_boxes`; take the lowest free one; second `sweep_boxes` for the remaining x
   distance from there. At most one step per tick (enough at walking speeds; documented).
3. Floor snap: characters that were on the floor last tick, aren't moving up and have no
   floor now probe 1..`step_height` cells down (one `probe_boxes` round); the highest
   blocked offset snaps them down, so they walk down slopes and small drops instead of
   hopping.
4. Unstick: boxes overlapping **solid** cells (`probe_boxes` with `RAY_SOLID`) try rising
   1..64 cells, then 1..32 cells to each side, as today. Candidates for all buried
   characters go in one call (rare, so the large candidate list is fine).
5. `scan_boxes` on the final boxes; `displace_cells` with mask powder for those holding
   powder (knock-out), then with mask liquid for those in liquid and moving faster than
   60 cells/s (splash).
6. `body_contacts` for floor on bodies; a box with nothing in the row under it is not on
   the floor (today's "standing still on something counts as floor" check comes from the
   sweep's `sides`).
7. Write back with `teleport` (position only), zero the velocity component that was
   stopped, store floor/ceiling/wall/submerged fraction/liquid on each character.

About 8–10 world calls per tick regardless of character count. Thresholds (60 cells/s
splash, knock-out speed, unstick limits) are constants at the top of the file, taken from
today's `creature.cpp`.

### `demo/car_controller.gd` — `class_name CarController extends Node2D`

Builds the chassis, two wheels and two `PixelWheelJoint`s from exported tunables (chassis
and wheel size, colours, materials, motor speed, motor torque, wheel frequency, wheel
friction), exposes `drive(direction: float)` and `set_braking(on: bool)`, owns its
generated textures. `bodies_demo.gd` drops `_build_car` and drives a `CarController`; the
texture helpers move to a small shared `demo/textures_util.gd`.

### Demos

`creatures_demo.gd`, `lighting_demo.gd` keep using `PixelCharacter.new()`; only paths
change. The bench's stats move from `stats.creatures` / `creature_ms` to a GDScript-side
count and timing of `CharacterSystem`'s handler.

## Build script

`addons/noita_engine/build.sh` is the project's only build script: the root `build.sh` and `SConstruct` are moved into the addon (and adapted), not kept alongside. In this repo it is run as `addons/noita_engine/build.sh`; in a host project, from wherever the addon was copied. It:

- Finds `thirdparty/` in this order: `--thirdparty <dir>`, `$NOITA_THIRDPARTY`,
  `<repo>/thirdparty` (this repo), `<addon>/thirdparty`. If none exists, it clones the
  pinned `godot-cpp` and `box2d` revisions (the current submodule commits) into
  `<addon>/thirdparty` (gitignored), so a copied addon builds without this repo.
- Builds `template_debug` (and `template_release` with `--release`) into
  `addons/noita_engine/bin/`.
- Keeps `--test`, `--tsan`, `--verify`, `--clean`, `-j N`. `--test` / `--verify` need this
  repo's `tests/` and print a clear "not available outside the development repo" message
  otherwise.
- `SConstruct` takes `thirdparty=<dir>`, `CPPPATH` = `engine/`, `engine/core`, `rules/`,
  and globs `engine/core/*.cpp`, `engine/godot/*.cpp`, `rules/*.cpp`, plus box2d. The
  tests target globs `engine/core` + `rules` + `tests/core`.
- Object files go to a build dir, not next to the sources, so the copied addon stays clean.

## Tests and bench

- `tests/core/`: `test_creature.cpp` removed. New doctest coverage for the core of each
  batched query (sweep stops without tunnelling, probe, scan counts, displace turns cells
  into particles, liquid stays) on `WorldGrid`-level functions. `test_physics.cpp` drops its
  creature cases; adds terrain-collision-off and cell-proxy cases on rigid bodies.
- `tests/godot/run_tests.gd`: the existing character tests (`walks_and_lands`,
  `jumps_and_hits_ceiling`, `swims`, `free_order`, `pushes_body`, scripted subclass joins)
  keep their assertions and now exercise the GDScript character. Added: step up a 3-cell
  ledge, refuse a 4-cell one, knock sand out, splash, stand on a crate.
- `bench/bench_world.gd`: spawns the GDScript character, same 30-alive budget; the bench
  reports `CharacterSystem` time per tick so the cost of the move is visible against the
  last C++ `creature_ms`. The 1.0 ms `CREATURE_BUDGET_MS` gate becomes **report-only**
  (user's decision): the number is recorded and printed, but it no longer fails
  `budget_pass` and does not block the work.

## Risks

- **GDScript cost per tick.** Dozens of characters → a few packed-array round trips plus
  GDScript loops over characters (not cells). Expect it to be measurably slower than the
  C++ pass. The bench comparison decides whether more work moves into the queries.
- **Behaviour drift.** The C++ pass stepped per sub-step; GDScript steps at most once per
  tick. Fine at walking speed (80 cells/s ≈ 1.3 cells/tick); a fast dash into stairs climbs
  only one stair per tick.
- **Signal re-entrancy.** A `bodies_stepped` handler that frees bodies or the world must
  not crash `tick_world`; handles are re-validated after the emit, the same way
  `sync_bodies` already looks nodes up by `ObjectID`.
- **Global `class_name`s** (`PixelCharacter`, `CharacterSystem`, `CarController`) can
  collide with a host project. Documented in the addon README.
- **Shipping C++** means the host needs scons, a C++ compiler and a first build for their
  platform. That is the accepted cost of editable rules.

## Left to the builder

- Exact packed-array shapes for the per-box results, and whether `sweep_boxes` returns a
  `Dictionary` or several out-arrays, as long as it stays one call per batch.
- How `CharacterSystem` is found or created (child of `PixelWorld`, group, or meta).
- Order of migration commits (move files first, then API, then GDScript port is the
  natural order).
