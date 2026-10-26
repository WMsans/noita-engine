# Particles, Rigid Bodies and Creatures — Design

Date: 2026-10-25
Status: draft, awaiting approval
Parent spec: `docs/superpowers/specs/2026-10-25-noita-engine-design.md` (its "Free particles",
"Player-style character" and "Rigid bodies" sections, and the matching parts of the
game-facing API). This doc replaces those sections. Where the two disagree, this one wins.

## Intent

Everything that moves through the pixel world, as the backbone of a game built on this
engine (user). Three things:

- **Free particles**: pixels knocked out of the grid that fly in arcs and land back as
  cells, plus purely visual particles that game code spawns and that fade away.
- **Rigid bodies**: Box2D v3.2 bodies (crates, boulders, cars) that fall, roll, tip, spin,
  push sand and water aside and float or sink, joined by joints so a car can be built.
- **Creatures**: the player, enemies and other creatures, all `PixelCharacter`s (user).
  Upright Box2D bodies that collide with bodies and with each other with real mass, and
  that walk on the pixel grid: standing on sand, stepping up ledges, knocking sand aside,
  swimming.

The engine does not know what a horse, car, enemy or player is. A car is bodies plus
joints; a creature is a hitbox plus settings. Game code gives them meaning (user).

**Success:** a demo where a creature walks through generated terrain, climbs sand piles,
wades through water, pushes and stands on crates, gets bumped by a car that drives across
desert sand, bounces over rubble and can flip, among a crowd of other creatures that block each other, while explosions throw
debris, and the benchmark with a crowd, bodies and particles holds 60 fps on the M1.

## What changed from the parent spec (and why)

- **Creatures are Box2D bodies** (user). The parent spec had the character collide with the
  grid only, invisible to Box2D. The user wants creatures to have all of Box2D's properties
  and to collide with bodies and each other, so a crate lands on a head and a car bumps an
  enemy by mass. The grid still handles terrain (see Approaches).
- **Engine reports contacts; it never deals damage** (user). Noita's "smash a body onto an
  enemy and it dies instantly" is explicitly unwanted. Damage is game code's rule, computed
  from the contacts the engine reports.
- **Joints** are new: a wheel joint (spring plus motor) and a revolute joint (hinge), so
  game code can build a car that bounces on suspension and can flip (user).
- **Visual particles** are new: never material, they fade out (user).
- **Explosions affect pixels only** (user). No impulse on bodies or creatures.
- **Roadmap renumbered.** Creatures now depend on Box2D, so the order is:
  3. Particles, `explode`, `raycast`, `spawn_particles`.
  4. Rigid bodies: Box2D, `PixelRigidBody`, terrain shapes, stamping, buoyancy, joints,
     contacts, demo car.
  5. Creatures: `PixelCharacter`.
  6. Lighting (was Plan 5).

  Each stage gets its own implementation plan, written after the previous one lands, as
  with Plans 1 and 2.

## Goals

- A free-particle pool: material particles that turn back into cells, and visual particles
  that fade out, both drawn in one batch.
- `PixelWorld.explode`, `raycast` and `spawn_particles`.
- `PixelRigidBody` defined by a texture and a colour→material mapping, non-breakable, with
  forces, impulses and velocity available to game code.
- Box2D joints exposed to game code: at least wheel and revolute.
- A demo car built from those, in a demo scene, not as an engine type.
- Bodies stamped into the grid each tick so sand piles on them and water flows around them;
  displaced sand and water become particles.
- Bodies float or sink in liquid (buoyancy and drag), and rest on sand: they settle in by a
  pixel or two at most and stop, firmly enough that a car drives across a desert normally
  (user).
- `PixelCharacter`: upright, collides with bodies and other creatures through Box2D and with
  terrain through the grid; stands on and climbs sand, steps up ledges, knocks sand out of
  its hitbox, reports floor / wall / ceiling / liquid / submerged fraction.
- Contacts reported to game code: who hit whom, where, and how hard.
- Dozens of creatures on screen at once (user: enemies and creatures are all
  `PixelCharacter`s).
- Benchmark extended with a crowd, about 50 bodies, a car and particle load.

## Non-goals

- **Riding or mounting** (user). No attaching a character to a body or another character,
  so joints connect bodies only, never creatures (my call, following from this).
- **Horses** or anything horse-specific (user).
- **Damage** of any kind (user). Only contact reports.
- **Explosion impulses** on bodies or creatures (user).
- **Creatures tipping or rotating** (user). Creatures always stay upright. Bodies may spin.
- Breakable / shattering bodies, and bodies burning, melting or reacting (parent spec
  non-goal). Stamped body pixels are inert while stamped (my call; see Stamping).
- Material particles reacting chemically in flight (my call: cheaper, and a few frames of
  flight aren't noticeable). They react again after rejoining the grid.
- Visual particles bouncing, or glowing. Glow is left room for (a per-particle emission
  value can be added for lighting); it is built in the lighting plan.
- Engine-applied buoyancy or swimming for creatures (my call). The engine reports the
  submerged fraction and liquid material; game code does swimming, as the parent spec said.
- Room `@body` markers spawning bodies (my call). They stay inert; game code spawns bodies.
- Persistence of bodies, creatures or particles across chunk discard.
- Material hardness / explosion resistance.

## Anti-goals

- **Missing 60 fps** on the M1 or the Iris Xe laptop with a crowd of creatures, about 50
  bodies, a car, particles and the existing liquid/fire workload.
- **Regressing the sim budget** (≤ 6 ms mean on the M1).
- **Jitter**: a creature or body visibly shaking while resting on sand, on a body, against
  another creature, or pinned between a body and terrain. This is the main risk of the
  chosen approach and the demo must exercise it.
- **Lost or duplicated material** from stamping: a body erasing sand that moved in next to
  it, or leaving ghost body pixels behind.
- **Hard-coded game concepts** in the engine: no vehicle, mount, enemy or damage types.
- **Tunnelling**: a fast creature, body or particle passing through a thin wall.
- **Mushy sand**: a body visibly sinking into sand, or a car bogging down or wobbling on a
  desert surface (user: "no muddiness can be felt").

## Constraints

Unchanged from the parent spec and Plans 1–2:

- Godot 4.7.2, godot-cpp 10.0.0-stable, C++17. `src/core/` stays Godot-free (Box2D may be
  used from core; it is plain C).
- Targets: macOS arm64 (M1, this machine) and Linux x86_64 (Iris Xe). The Linux run is
  the hand-off checkpoint, as in Plan 2.
- 64×64 chunks, fixed 60 Hz tick. The M1's 4 performance cores belong to the sim's `JobPool`.
- Box2D v3.2.0 (MIT), vendored under `thirdparty/box2d`, compiled by SCons into the
  extension and into the core test binary.

## Approaches considered (how creatures meet both Box2D and the grid)

A creature is blocked by two things that live in different places: bodies and other
creatures live only in Box2D, while sand, water and ledges live only in the grid.

- **A. A creature is a real Box2D body, and a grid pass corrects it (chosen by the user).**
  Each creature is a dynamic Box2D body with rotation locked. Box2D resolves creature↔body
  and creature↔creature contacts with real mass. Creatures do **not** collide with Box2D
  terrain shapes; instead, after each Box2D step, a grid pass sweeps each creature's hitbox
  against the cells (sand, solids, ledges, liquid) and writes the corrected position and
  velocity back. Gains: every Box2D property for free. Costs: two collision systems on one
  object, so a body pinning a creature against sand can jitter until tuned.
- **B. A creature is a kinematic mover (rejected).** Box2D v3's character-mover helpers
  would solve grid and shape contacts in one pass, but kinematic objects are infinitely
  heavy to Box2D: a car would stop dead on an enemy unless mass-based shoving were
  hand-written both ways. That contradicts "all the properties of Box2D".
- **Sand and water as Box2D shapes (dropped).** They change every tick; rebuilding shapes
  would blow the budget, and sand would become a wall rather than something you wade through.
- **Own physics for creatures (dropped).** Reinvents Box2D.

## Design

### Tick order

One 60 Hz tick, on the main thread except where noted:

1. **Box2D step** (bodies and creatures), against the terrain shapes swapped in at the last
   tick boundary.
2. **Creature grid pass**: sweep, step-up, displace powder, measure liquid, write back.
3. **Support forces**: apply liquid buoyancy and sand support to bodies for the next step.
4. **Stamp** awake bodies into the grid; displaced powder and liquid become particles.
5. **Cell simulation** (`Simulator::tick`, unchanged phases and threading).
6. **Unstamp.**
7. **Particle step**: move, collide, settle into cells or expire.
8. Swap in any finished terrain shape builds; start builds for chunks that changed.

The exact order of 2–4 and 7 is the builder's call as long as the anti-goals hold; this
order is a starting point. The sim keeps its existing `step()` / `simulating` behaviour:
one `step(1)` runs the whole tick above, so the benchmarks stay frame-locked.

### Free particles (stage 3)

- **Pool** in core: a flat array (structure-of-arrays is the builder's call) with a hard cap
  (default about 20 000, a `PixelWorld` property). When full, new material particles are
  written straight back as cells at their spawn spot if empty, else dropped; new visual
  particles are dropped (my call).
- **Material particle**: float position and velocity, gravity, and the full `Cell` it came
  from (material, temperature, shade, life). Each tick it moves along its velocity in
  sub-steps no longer than one cell, so it can't tunnel. It stops when the next cell is
  blocked: anything that isn't air or gas, an unloaded chunk, or a stamped body pixel.
  On stop, or when its speed falls below a threshold, it becomes a cell in the nearest empty
  spot (searching back along its path, then upward, a few cells at most). If there's no
  room, it's dropped (my call: rare, and keeps the pool bounded). Landing marks the cell
  dirty so it settles normally.
- **Visual particle**: position, velocity, gravity scale, colour, lifetime, fade (alpha
  over lifetime), and a per-spawn `collides` flag: with it, the particle dies on the first
  blocked cell; without it, it passes through terrain. Never written to the grid.
- **Streaming**: particles outside the loaded zone are removed. Particles in sleeping chunks
  keep moving (they're cheap and few); the builder may freeze them instead.
- **Rendering**: all particles drawn on top of the world in one batch per frame (MultiMesh,
  a particle image layer, or a canvas `draw_*` batch — builder's call within budget).
- **Threading**: single-threaded to start. `ponytail:` mark it; move to the `JobPool` if
  `particle_ms` exceeds its budget.

### `PixelWorld` additions (stage 3)

- `spawn_particles(material, position, velocity, count, spread)` for material particles.
- `spawn_visual_particles(color, position, velocity, count, spread, lifetime, gravity_scale,
  collides)` for visual ones. Exact signatures (or one method with a parameters Dictionary
  or Resource) are the builder's call.
- `explode(position, radius, power)`: every non-air cell within the radius that is not a
  stamped body pixel is removed. Solid cells vanish (my call: thrown solid pixels would land
  as static specks stuck to walls and ceilings, and dirty terrain shapes). Loose cells
  (powder, liquid) are thrown outward as material particles with speed rising with `power`
  and falling with distance from the centre; some may vanish. No effect on bodies or creatures (user). The
  fraction and speed curve are the builder's call; the demo should look like Noita.
- `raycast(from, to, mask)`: walks the grid (DDA) and, from stage 4, also Box2D. Returns a
  Dictionary with the hit position, the material, a surface normal, and the hit body or
  creature node (null for terrain), or empty on no hit. `mask` picks material classes
  (solid, powder, liquid, gas) and whether bodies and creatures count. Unloaded cells count
  as a hit on solid.
- Stats: `particles_material`, `particles_visual`, `particle_ms`.

### Rigid bodies (stage 4)

- **Box2D**: one `b2World` owned by `PixelWorld`, stepped once per tick (1/60 s, sub-step
  count is the builder's call). Gravity matches the creatures'. Box2D runs single-threaded
  to start (`ponytail:` mark; hook Box2D's task callbacks to the `JobPool` or adopt enkiTS
  only if its step exceeds budget — this settles the enkiTS question Plan 2 deferred).
- **Units**: 1 world cell = 1 Godot unit (existing). Box2D runs in metres; the scale (for
  example 10 cells per metre) is the builder's call, chosen so creature- and crate-sized
  objects fall in Box2D's comfortable size range.
- **`PixelRigidBody`** (`Node2D`): a texture plus a `PixelRoomPalette` (the existing
  Colour→material resource) maps each opaque pixel to a material; a single `material`
  property is the shortcut when the whole body is one material. Mass comes from the
  materials' densities. The collision shape is built once from the pixels: marching squares
  → Douglas-Peucker → convex decomposition into Box2D polygons, cached per texture+palette.
  The node syncs its transform from Box2D each frame and draws its texture. Game code gets
  `apply_force`, `apply_impulse`, `apply_torque`, `linear_velocity`, `angular_velocity`,
  `mass`, `sleeping`, and can teleport it.
- **Joints**: `PixelRevoluteJoint` and `PixelWheelJoint` nodes (modelled on Godot's
  `Joint2D`: two node paths plus an anchor and, for the wheel, an axis), exposing limits,
  motor speed and max torque, and the wheel's spring frequency and damping. Joints connect
  any two of: bodies, creatures. Names and the exact property set are the builder's call;
  more joint types later, on demand. Joints connect two bodies, never a creature (riding is
  out of scope, and a jointed creature would fight its own grid pass).
- **Terrain shapes**: only `collision_solid` materials. Built per chunk, only for chunks
  within a margin of an awake body, only when that chunk's solid pixels changed since the
  last build. Marching squares → Douglas-Peucker → Box2D chain loops (or segments; builder's
  call). Builds run off the main thread and swap in at a tick boundary, so a body never
  sees a half-built chunk. A chunk with no shape yet near an awake body counts as solid
  (the body waits) rather than letting the body fall through. Creatures never use these
  shapes. Unloaded chunks' shapes are destroyed with the chunk.
- **Stamping** (after FallingSandSurvival): before the cell sim, each awake body's
  pixels are rasterised from its transform into the grid as their own material with
  `FLAG_BODY` set. Powder or liquid already in a target cell is **held**: saved in a
  per-body side buffer and put back on unstamp, so no material is lost or duplicated and the
  water a body sits in doesn't leave a hole (my call; this replaces the parent spec's
  "pushed out as free particles", which made buoyancy unmeasurable). A solid terrain cell is
  left alone (the body overlaps it slightly; Box2D separates them). After the sim, every
  cell still carrying `FLAG_BODY` gets its held cell back, or air; nothing can move into a
  stamped cell, so nothing is overwritten. Splash or sand-spray particles when a body moves
  fast through liquid or sand (or a car's wheel spins on sand) are the builder's call, for
  looks. While stamped, body pixels
  are inert solids to the movement rules and reactions (my call: bodies don't burn or
  dissolve, matching "non-breakable"). Sleeping bodies stay stamped between ticks rather
  than re-stamping each tick (builder's call; an optimisation).
- **Buoyancy and drag**: for each awake body, count its pixels whose held cell is liquid.
  Upward force = submerged pixel area × liquid density × gravity, applied at the centroid
  of the submerged pixels (so a tipped crate rights itself). Plus linear and angular drag
  in proportion to the submerged fraction. Constants are the builder's call; crates of wood
  float, rock sinks.
- **Sand support** (user): the same mechanism for pixels whose held cell is powder, but
  stiff: the support force rises steeply with penetration depth (a strong spring with
  damping, not a density ratio), so any body settles in by a pixel or two at most and
  stops, with no drag that feels like mud. Friction against sand is close to ground
  friction, so the car's wheels grip and it drives across a desert like solid ground. Sand
  still piles on top of bodies and is still knocked away by explosions and creatures. The
  spring and damping constants are the builder's call, judged in the desert demo.
- **Contacts**: Box2D hit events (above a minimum approach speed) are reported on both
  nodes as a signal, for example `hit(other, point, normal, speed)`, where `other` is a
  body or creature node. Body↔terrain hits are reported with `other = null`. The engine
  computes no damage (user).
- **Streaming**: bodies outside the active zone are disabled in Box2D and stay where they
  are. Bodies whose position falls outside the loaded zone are not freed by the engine;
  they emit `left_world` and stay disabled, and game code decides whether to free them
  (my call: the engine never frees game-owned nodes).
- **Demo car** (demo scene only): a chassis body, two wheel bodies, two wheel joints with
  motors driven from input. It must drive over rubble, bounce on its springs, flip on steep
  ground, float or sink in water, and bump creatures.
- Stats: `bodies_awake`, `physics_ms` (Box2D step), `shape_builds`, `shape_ms`, `stamp_ms`.

### Creatures: `PixelCharacter` (stage 5)

- **Node** (`Node2D`): hitbox size (width, height in cells), `step_height` (default 3),
  `mass`, `friction`, `gravity_scale`. Backed by a dynamic Box2D body with rotation locked
  and one box shape of the hitbox size (small rounding radius is the builder's call). Its
  collision filter accepts bodies and other creatures and rejects terrain shapes.
- **Control** (my call, modelled on `CharacterBody2D` so it feels familiar): a read/write
  `velocity` that game code reads, changes (input, jumping) and writes back each physics
  frame; after the tick it holds the post-collision result, so a car's bump shows up and
  game code keeps it unless it overwrites it. Also `apply_impulse` and `apply_force`.
  Gravity comes from the Box2D world and `gravity_scale`. Movement feel (acceleration,
  jump height) is game code's.
- **Grid pass**, after the Box2D step, per creature:
  - Sweep the hitbox from its pre-step to its post-step position against blocking cells:
    `Solid` and `Powder` classes and unloaded chunks block; liquid, gas and stamped body
    pixels don't (bodies already collided in Box2D). Axis-separated sweep in whole cells,
    no tunnelling at any speed.
  - **Step-up**: a horizontal block of at most `step_height` cells with room above lifts
    the creature onto it. Walking into a sand pile climbs it, as in Noita.
  - **Powder displacement**: powder inside the hitbox after the sweep (sand that fell onto
    it, or a body that shoved the creature into a pile) is knocked out as material particles,
    away from the creature's centre.
  - **Liquid**: never blocks. Moving through it throws splash particles in proportion to
    speed (my call, for Noita's look). The pass counts liquid cells in the hitbox for the
    submerged fraction and records the dominant liquid material.
  - Write the corrected position back to Box2D and zero the velocity component into any
    surface hit.
  - **Pinned creatures hold bodies up**: a crate landing on a creature's head rests there,
    and a car pushing a creature into a wall stops against it; the body doesn't push the
    creature into terrain or overlap it. The mechanism (for example feeding grid support
    back into Box2D before its step) is the builder's call; this is the jitter anti-goal's
    main test case.
- **Reports**, updated every tick: `is_on_floor`, `is_on_wall` (and which side),
  `is_on_ceiling`, `is_in_liquid`, `liquid_material`, `submerged_fraction`. Floor includes
  standing on a body or another creature (from Box2D contacts), not just terrain.
- **Contacts**: the same `hit` signal as bodies.
- **Crowds**: the grid pass costs O(hitbox perimeter + area) per creature per tick, bounded
  and independent of the rest of the world. Creatures far from bodies need no terrain
  shapes, so a crowd causes no shape builds.
- **Streaming**: as bodies. The follow target (usually the player) is always active.
- Stats: `creatures`, `creature_ms`.

### Performance budget (M1, per tick, mean)

- Cell sim and chemistry: ≤ 6 ms (unchanged).
- Box2D step + terrain shape builds + stamping: ≤ 2 ms (parent spec's figure, now
  including creatures).
- Particles: ≤ 1 ms at the bench's particle load.
- Creature grid pass: ≤ 1 ms for the bench's crowd.

### Verification

- **Core unit tests** (doctest, no Godot): particle landing and the no-room drop, visual
  particle expiry, explosion ejecta conservation (every removed cell is either a particle
  or counted as vanished), DDA raycast against cells, marching squares + Douglas-Peucker +
  convex decomposition on known shapes, stamp/unstamp restoring held cells exactly and
  leaving no `FLAG_BODY` behind, buoyancy force sign and magnitude, sand support settling a
  body within two pixels, creature sweep (wall, floor,
  ceiling, step-up at and above `step_height`, no tunnelling at high speed), powder
  displacement count.
- **Demo scene** (`demo/`): a controllable creature in generated terrain, a crowd of
  wandering creatures, crates and boulders, the car (including a desert run), explosions on
  click, visual particle
  sparks. This is how the jitter anti-goal is judged.
- **Benchmark**: `bench_world` (or a sibling scene) gains a crowd of about 40 creatures, about
  50 bodies, the car and periodic explosions travelling with the camera, plus the new
  stats in its JSON. Each stage adds its own load when it lands. Mac numbers here, Linux at
  the hand-off checkpoint, as in Plan 2.

## Left to the builder

- Particle pool layout and cap, the landing search, explosion ejecta fraction and speed.
- Particle rendering method.
- Box2D units scale, sub-step count, chain loops vs segments, the shape-build thread.
- Exact node, signal, method and property names and signatures.
- Whether sleeping bodies stay stamped between ticks.
- Buoyancy and drag constants; creature hitbox rounding.
- The exact tick order within the anti-goals.

## Decisions from the conversation

- These three systems are the backbone of a game, not a demo (user).
- Enemies and other creatures are all `PixelCharacter`s, so dozens are on screen (user).
- Cars bounce and can flip like physics objects, so joints are exposed (user).
- No riding, no horse (user). Nothing hard-codes what a horse or car is (user).
- Visual particles exist alongside material particles (user).
- Explosions only blow apart pixels (user).
- Creatures are Box2D bodies, collide with bodies and each other; the engine never deals
  damage (user, citing Noita's instant-kill smash as unwanted).
- Creatures stay upright; bodies spin and tip (user).
- Approach A (user).
- Bodies settle into sand partway and stop, firmly: a car drives across a desert normally,
  with no muddy feel (user).
- From the builder check, settled as my calls: creatures climb sand rather than wade
  through it (parent spec); a pinned creature holds the body up; joints are body-only;
  explosions destroy solids and throw only loose cells; stamping holds liquid and powder
  rather than ejecting them, so buoyancy can be measured and material is conserved.
