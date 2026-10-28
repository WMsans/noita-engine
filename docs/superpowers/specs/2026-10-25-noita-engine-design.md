# Noita-style Pixel World Engine — Design

Date: 2026-10-25
Status: draft, awaiting approval

## Intent

Build the world engine for a game: a Noita-style falling-sand world in Godot 4.7,
written in C++ as a GDExtension (godot-cpp). Every pixel of the world is a simulated
material. The world is large and streams in as the player explores. Darkness is a
gameplay element: light comes from glowing materials and from lights the game places,
and terrain blocks it.

This is a **foundation**, not a game. Game code (GDScript or C++) builds on top of it
through a clean API. Wherever possible, the engine reuses proven techniques and
libraries (Noita's GDC talk, FallingSandSurvival's source, Box2D, radiance cascades)
instead of inventing new ones.

## Goals

- Cellular-automaton simulation of powders, liquids, gases and static solids, Noita-style.
- Chemistry: data-defined reactions (A touching B → C + D at a probability), per-pixel
  temperature, burning/fire spread, melting/freezing/boiling at thresholds.
- A large, procedurally generated, streamed world: noise-driven caves with pre-made
  rooms ("pixel scenes") placed into them, deterministic from a seed.
- A player-style character that collides directly with the pixel grid, walks on it,
  steps up small ledges, pushes powders aside and can tell when it's in liquid.
- Non-breakable rigid bodies (crates, boulders, carts) that fall, roll and rest on solid
  terrain, displace sand and liquid, and that the character can stand on and push.
- Dynamic 2D lighting: glowing pixels and game-placed lights emit light, solid pixels
  block it, light spreads through open space, and unlit areas are dark. It updates
  every frame as terrain changes. Bloom/glow on bright emitters.
- **Performance: a steady 60 fps on an Apple M1 Mac mini and on an Intel Iris Xe
  (Alder Lake-P) laptop, or better.**

## Non-goals (for now)

- Breakable / shattering rigid bodies (pixel bodies that crack and split). The design
  shouldn't block adding them later. Rigid-body pixels should keep their material
  identity, as Noita's do.
- Saving or loading terrain. Changes do **not** persist: an unloaded chunk is
  discarded and regenerates fresh from the seed when it's loaded again.
- Multiplayer and networking.
- Game content beyond what the engine needs for testing (enemies, spells, items, UI).
- Background parallax layers or a separate "wall" layer behind the terrain.

## Anti-goals (failure even if it technically works)

- Missing 60 fps on the M1 or the Iris Xe laptop in normal play (a busy cave with
  liquid, fire and a few dozen rigid bodies on screen).
- Lighting that lags behind the terrain, so a wall that's dug through, burned or
  dissolved doesn't change the light in the same frame or the next one.
- An engine that only works as a demo, where game code can't place lights, spawn
  materials, query pixels or add materials and reactions without editing C++.
- Visible resets close to the player: chunks must never regenerate where the player
  can see them or just did.

## Constraints

- **Engine:** Godot 4.7.2 (installed at `/usr/bin/godot`). The existing project at the
  repo root uses Forward+. The engine is 2D, so the 3D Jolt setting doesn't matter.
- **Language:** C++ via godot-cpp, built with SCons (installed).
- **Target platforms:** Linux x86_64 (this dev machine, which is the Iris Xe laptop,
  running Vulkan) and macOS arm64 (the M1 Mac mini, Metal backend). **Windows is not a
  target.** Leave the existing D3D12 setting in `project.godot` alone; it's harmless.
- **Two machines, one repo:** this Linux machine builds and benchmarks the Linux side.
  A separate agent on the Mac mini works in the same repo, builds the macOS library
  natively, and runs the benchmark there. So the build must work natively on both
  (`scons platform=linux` and `scons platform=macos arch=arm64`). The plan needs explicit
  Mac checkpoints, where work is pushed, the Mac agent builds and benchmarks it, and
  its numbers come back. Don't assume per-step verification on the M1.
- **CPU budget:** the M1 has 8 cores (4 performance + 4 efficiency). The Iris Xe laptop
  has 20 threads. The M1 is the binding CPU constraint. The simulation must not assume
  more than about 4 fast worker threads.
- **GPU budget:** both GPUs are integrated. Lighting must be cheap at world-pixel
  resolution.
- **Camera:** a Noita-like zoom, about 480×270 world pixels on screen, scaled up with
  nearest-neighbour filtering.

## Approaches considered

### Where the simulation runs

- **CPU, chunked and multithreaded (chosen).** This is Noita's proven model. The player,
  rigid bodies and chemistry all read and write pixels every frame, so keeping the grid
  in CPU memory avoids GPU round trips.
- **GPU compute shaders (rejected).** They can simulate huge areas, but the player,
  rigid bodies and chemistry need GPU→CPU readback every frame. That stalls on
  integrated GPUs and makes debugging much harder. The rules (Margolus-style block
  updates) also behave differently from Noita's.

### Rigid-body physics (the real choice)

- **A. Box2D v3.2 embedded in the extension (chosen by the user).** Box2D v3.2.0
  (released 2026-10-19) is C, MIT-licensed, has SIMD and a native task system. The
  extension owns the physics world and decides exactly when terrain shapes rebuild, on
  worker threads. Trade-off: game code uses the engine's own body nodes and queries,
  and Godot's physics nodes (Area2D, joints, physics layers) don't see these bodies.
- **B. Godot's built-in 2D physics (rejected).** Familiar `RigidBody2D` and `Area2D`
  nodes, but slower, mostly single-threaded, with overhead when rebuilding many shapes
  through the physics server. That's risky for the M1 target.
- **Rapier 2D extension (rejected).** A third-party Rust dependency. Box2D v3 does the
  same job and embeds cleanly in C++.

### Lighting

- **Radiance cascades in GPU compute (chosen).** A recent 2D global-illumination
  technique. Every emissive pixel is a light source, every solid pixel can block light,
  and the cost depends on resolution rather than light count. That fits a
  480×270-plus-margin texture on integrated GPUs.
- **Godot's built-in 2D lights with LightOccluder2D (rejected).** They need occluder
  polygons for all visible terrain, regenerated every frame, and can't treat thousands
  of glowing pixels as light sources.
- **CPU flood-fill light, Terraria-style (rejected).** It's cheap but blocky, and it
  competes with the simulation for the M1's limited CPU.

## Design

### World, chunks and streaming

- The world is an effectively unbounded grid of cells, split into **64×64-cell chunks**
  (Noita's size). Chunks are addressed by integer chunk coordinates.
- Around the camera or player there are three nested zones:
  1. **Active zone**, about 2–3 screens: chunks simulate every tick.
  2. **Loaded margin** beyond that: chunks stay in memory and sleep (no simulation)
     until something wakes them, such as a neighbour's change spilling in or the
     active zone moving.
  3. **Beyond the margin:** chunks are discarded. Loading one again regenerates it from
     the seed.
- Chunk generation runs on worker threads ahead of the player, so the active zone never
  waits on generation. If a chunk isn't ready yet, it's treated as solid until it is.
- The exact zone sizes are tunable settings. The defaults are the builder's call, within
  the anti-goal of no visible resets.

### Cell data

Each cell stores at least:
- a material ID (16-bit, so the material list can grow),
- a temperature,
- a burn or lifetime counter (fire duration, gas decay, and so on),
- small flags (updated-this-tick parity, "is a rigid-body pixel", and so on),
- a per-pixel colour variation, so materials don't render as flat colour.

The exact layout and bit widths are the builder's call. Aim for a compact struct of
about 8 bytes that's friendly to cache lines and SIMD.

### Simulation

- **Fixed 60 Hz tick**, decoupled from the render frame rate. If a frame runs long, the
  tick catches up within a bounded number of steps (no spiral of death).
- **Per-chunk dirty rectangles:** each chunk tracks the region that changed last tick
  and only processes that region next tick. Changes near a chunk edge wake the
  neighbour chunk.
- **Four-phase checkerboard multithreading**, as in Noita's GDC talk. Chunks are split
  into 4 interleaved sets, and each set is processed in parallel. Each chunk may write up
  to half a chunk into its neighbours, which never run in the same phase. The thread
  pool is the builder's call (the engine's own pool, or Godot's WorkerThreadPool).
- **Movement rules by material class**, matching Noita's "look down, then diagonal,
  then sideways" style:
  - powders fall and pile,
  - liquids fall and spread sideways (with a dispersion rate per material),
  - gases rise and spread,
  - static solids stay put.

  Density decides which material swaps with which. Alternate the left/right scan
  direction per tick to avoid bias.
- **Free particles:** a separate pool of particles in float position/velocity space,
  used for splashes, debris, explosion ejecta, and pixels displaced by bodies or the
  character. A particle turns back into a cell when it lands or comes to rest.

### Chemistry

- **Materials and reactions are Godot Resources** (`.tres`), editable in the editor.
  Game code can add more at runtime before the world starts.
- A material defines:
  - its class (powder, liquid, gas or static solid), density, colour or colour palette,
  - emission (colour and intensity; for example lava, embers and burning cells glow),
  - light opacity (how much it blocks light),
  - thermal properties: conductivity, ignition temperature, burn duration, and what it
    becomes when burnt,
  - phase changes: melt into X above temperature T, freeze or solidify into Y below T,
    boil into gas Z,
  - whether it's solid for collision purposes ("collision solid": rock, wood, metal,
    ice).
- A reaction is a rule: *A adjacent to B → A becomes C, B becomes D, with probability P
  per tick*, optionally with a temperature condition. Examples: water + lava → steam +
  rock, acid + rock → acid (consumed) + nothing.
- At startup, materials and reactions compile into dense lookup tables (for example a
  reaction table keyed by material pair) so the hot loop never touches Godot objects.
- Temperature spreads between neighbouring cells at a reduced rate (for example every
  few ticks, or only in dirty regions). The exact scheme is the builder's call, within
  budget.
- Fire is a material (or a burning state) that heats its neighbours, ignites flammable
  ones, consumes its fuel's burn counter and emits light.

### World generation

- **Deterministic per chunk:** generating chunk (cx, cy) gives the same result no
  matter what order chunks load in or what was loaded before. Every random decision is
  seeded from (world seed, coordinates).
- **Terrain:** noise-driven (for example layered simplex or FastNoise) caves, material
  regions (rock, dirt, sand pockets, water and oil pools, lava) and depth-based variation
  ("biomes" by depth).
- **Pre-made rooms (pixel scenes):** authored as images where each colour maps to a
  material, plus optional markers such as light spots or rigid-body spawns. Rooms are
  placed by a deterministic coarse placement grid. Each grid cell decides from the seed
  whether it holds a room, which one, and where. A chunk being generated asks every
  placement cell that overlaps it, so rooms crossing chunk borders come out
  consistently.
- The generator is configured through Resources: noise parameters, biome depth bands,
  the room list with spawn weights. Per-pixel generation in GDScript is not an
  extension point, because it's too slow. Whether game code can plug in a custom C++
  generator is the builder's call.

### Player-style character (grid-collided, not Box2D)

- A `PixelCharacter` node (name is the builder's call) with an axis-aligned box hitbox,
  moved by game code through a `move_and_slide`-like call with a velocity.
- It collides directly with grid cells: static solids, powders and rigid-body pixels
  block it. It steps up ledges up to N pixels (configurable).
- Powders and liquids it moves into are pushed out of its box as free particles. This
  gives Noita's look of walking through sand piles.
- It reports contact and environment info: on floor, on wall, in liquid (and which
  material), and the fraction of the hitbox submerged, so game code can do swimming
  and buoyancy.
- It pushes rigid bodies by applying impulses to Box2D bodies it touches, and stands on
  them because their pixels are stamped into the grid (see below).

### Rigid bodies (Box2D v3.2, non-breakable)

- **Box2D v3.2.0 is vendored into the repo** and compiled into the extension. The
  extension owns one `b2World`, stepped at the same 60 Hz tick.
- A `PixelRigidBody` node (name is the builder's call) is defined by a texture and a
  material mapping (each pixel has a material). Its collision shape is generated once
  from its pixels (marching squares → Douglas-Peucker → convex decomposition). It
  doesn't change, since the body is non-breakable. The node syncs its transform from
  Box2D each frame, and game code can apply forces and impulses and read velocity.
- **Terrain collision, following Noita and FallingSandSurvival:**
  - Only "collision-solid" materials produce shapes. Powders, liquids and gases never
    do.
  - Shapes are built **per chunk**, only for chunks within a margin of an **awake**
    body, and only when that chunk's collision-solid pixels changed since its shapes
    were last built.
  - Pipeline: marching squares outline → Douglas-Peucker simplification → Box2D chain
    loops or segments (no triangulation needed for static terrain in Box2D v3). Whether
    to use chain loops or a convex decomposition is the builder's call.
  - Builds run on worker threads and swap in atomically at a tick boundary. A body
    never sees a half-built chunk.
- **Interaction with powders and liquids:** each tick before the simulation step, every
  awake body's pixels are stamped into the grid as rigid-body pixels. Powders and
  liquids already in those cells are pushed out as free particles. Sand then piles onto
  bodies and water flows around them. After the step, the stamped pixels are cleared,
  with care not to erase material that moved in. This is FallingSandSurvival's
  stamp/unstamp approach.
- **Buoyancy and drag:** a body that overlaps liquid cells gets an upward force
  proportional to the displaced liquid (using liquid density) plus linear and angular
  drag. This is my call, to make crates float and sink plausibly.
- **Sleeping and streaming:** bodies outside the active zone are disabled in Box2D.
  Bodies in a discarded chunk are discarded too. Bodies placed by generated rooms
  respawn when the room regenerates.

### Lighting (radiance cascades, GPU compute)

- Runs through Godot's `RenderingDevice` compute shaders (GLSL compiled by Godot; it
  works on Vulkan on Linux and Metal on macOS). It needs a real window, not `--headless`, since GPU timings need rendering.
- Light is computed over the visible area **plus a margin** (so off-screen lava lights
  the edge of the screen), at world-pixel resolution or half resolution. That's the
  builder's call, within budget.
- Inputs, uploaded per frame from the cell grid (only changed regions where practical):
  - an emissive texture (material emission, plus burning or hot cells),
  - an opacity texture (material light opacity),
  - game-placed lights (position, colour, radius, intensity) rasterised into the
    emissive input or added analytically.
- Output: a light texture, multiplied with the world's albedo in a canvas shader. A low,
  configurable ambient term controls how dark "dark" is. A bloom pass on bright
  emitters gives the Noita glow.
- Lighting reads the terrain from the same tick that's rendered, so it never lags more
  than one frame.
- Reference implementation to port: the 2D radiance cascades formulation (Sannikov), for
  example the TypeGPU radiance-cascades package's pass structure.

### Rendering the world

- The visible world area (plus a small margin) renders from a CPU-side colour buffer.
  Only dirty rows or rectangles upload to a GPU texture each frame, which the camera
  draws with nearest-neighbour filtering.
- Free particles and rigid bodies draw on top. Rigid bodies draw as their own textures
  with the Box2D transform. Particles draw in a batch.
- Godot camera integration: the engine follows a target node (usually the player).

### Game-facing API (GDScript-accessible)

At minimum:
- `PixelWorld` node: seed, material and reaction set, generator config, zone sizes, and
  a follow target. Methods: `get_cell(pos)`, `set_cell(pos, material)`,
  `fill_rect` / `fill_circle(material)`, `explode(pos, radius, power)`,
  `raycast(from, to, mask)`, `query_material_at(pos)`, `spawn_particles(...)`.
- `PixelCharacter` node (see above).
- `PixelRigidBody` node (see above).
- `PixelLight` node: colour, radius, intensity, optional flicker. It's a game-placed
  light that the lighting pass reads.
- Material, Reaction and Generator Resources.
- Signals and callbacks are the builder's call (for example "chunk generated" or
  "character entered liquid").

Exact names and signatures are the builder's call, within these capabilities.

### Performance budget and verification

- Per-frame budget on the M1, at 60 fps (16.6 ms), as guidance:
  - simulation and chemistry ≤ 6 ms wall time with about 4 workers,
  - Box2D and terrain-shape builds ≤ 2 ms,
  - lighting and world rendering ≤ 4 ms GPU.

  The rest is headroom for game code.
- A **benchmark scene** ships with the engine. It can run unattended from the command
  line (for example `godot --path . res://bench/bench.tscn -- --frames=N`), and it writes
  per-subsystem timings (mean and p99) to a machine-readable file, so the Mac agent can
  run it and report results without a human. It's a worst-case-ish cave with flowing
  liquids, burning oil, lava meeting water, falling sand and about 50 rigid bodies, with
  the camera moving through the world to exercise streaming. It shows on-screen
  timings per subsystem.
- Internal profiling counters (per-subsystem ms, active chunks, dirty cells, particles,
  awake bodies, shape rebuilds per second) are available to game code and the debug
  overlay.
- The pure C++ core (cell rules, reactions, marching squares and simplification,
  deterministic generation) is testable without Godot. Unit tests cover movement rules,
  reaction tables, worldgen determinism (same chunk regardless of load order) and
  terrain-shape generation.

## Build and layout

- godot-cpp as a git submodule, pinned to a version compatible with Godot 4.7. It may
  use an `extension_api.json` dumped from the installed Godot 4.7.2
  (`godot --dump-extension-api`). Which godot-cpp revision to use is the builder's call.
- Box2D v3.2.0 vendored as a submodule or a source copy, compiled by the extension's
  SCons build.
- Builds for the debug and release templates. Release builds use `-O3` (or equivalent)
  and SIMD where available (SSE4/AVX2 on x86_64, NEON on arm64).
- The extension's code lives in its own directory (for example `extension/`). The Godot
  project at the repo root loads the built library through a `.gdextension` file.

## Left to the builder

- Exact cell layout, zone sizes, thread pool, temperature diffusion scheme.
- Chain loops versus convex decomposition for terrain shapes.
- Lighting resolution (full or half) and the bloom implementation.
- Node and method names, signals, and the C++ generator extension point.
- The noise library (FastNoise Lite is a reasonable default, since it's a single
  header under the MIT licence).
- The starter material and reaction set for testing and the benchmark scene: at least
  sand, water, oil, lava, rock, wood, fire, steam, smoke and acid.

## Decisions from review

- Windows is not a target (answered by the user).
- The Mac is built and benchmarked by another agent working in the same repo (answered
  by the user). See Constraints and the benchmark section.
- **Lighting is purely visual.** Game code never asks how lit a spot is, so there's no
  CPU readback or CPU light estimate (answered by the user).
- The Iris Xe laptop is this Linux dev machine (confirmed from its hardware).
- Solid terrain that loses support does **not** fall. That would need breakable or
  spawned rigid bodies, which are out of scope. Static solids change only through
  digging, explosions, burning, melting and reactions (my call).
