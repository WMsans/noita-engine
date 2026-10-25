# Plan 2 — World Generation and Streaming — Design

Date: 2026-10-25
Status: draft, awaiting approval
Parent spec: `docs/superpowers/specs/2026-10-25-noita-engine-design.md` (its "World, chunks
and streaming" and "World generation" sections). This doc narrows those sections for Plan 2
and records what changed since.

## Intent

Replace Plan 1's fixed box arena with a real, endless, generated world that streams in
around the player. The feel is **Terraria, not Noita**: an open surface with sky, and below
it a world the player explores sideways as much as downwards. Plan 2 is engine work. It
provides the generator, the biome/room system and streaming. Game content (real biomes, real
rooms) comes later.

**Success (agreed):** fly a camera through an endless generated world — along the surface,
down through the layers, sideways through caves and back over ground already visited — with
no hitches and no holes, and the benchmark passing.

## Goals

- **World shape.**
  - Endless sideways, endless downwards, air forever upwards.
  - A **surface line** with hills (1-D noise over x). Air above it, ground below it.
  - **Biomes vary by depth and by x**: a depth band says what layer you're in (surface,
    underground, caverns, deep), and within a band, biomes alternate along x in
    seed-chosen regions (for example forest / desert side by side on the surface).
  - The deepest band continues forever downwards.
- **Caves** from 2-D noise, stretched so they tend to run sideways (Terraria-like
  horizontal exploration), tapered near the surface so the surface isn't Swiss cheese.
- **Material regions** inside the ground: a biome's base material, a surface layer (for
  example grass or sand on top), pockets of a secondary material, and liquid pools in caves.
- **Rooms (pixel scenes)**: PNG images placed on a seeded coarse grid, which come out
  identical no matter which chunk loads first.
- **Three zones** around the follow target: active (simulates), loaded margin (in memory,
  asleep), discarded beyond (regenerated from the seed on return).
- **Generation on background threads** that never block a frame and barely touch the sim's
  cores.
- **Deterministic**: chunk (cx, cy) is identical regardless of load order, thread count or
  what was loaded before.
- Everything is configured through **Godot Resources**, editable in the editor, like Plan 1's
  materials.

## Non-goals (this plan)

- **Persistence.** Changes don't survive unloading: a discarded chunk regenerates fresh,
  including a dug tunnel or a drained lake (accepted by the user). Saving to disk is a
  likely later task, so discarding a chunk goes through **one function** that a save step
  can hook into later. Nothing more is built for it now.
- **Sky / sunlight.** The sky is plain air. Treat the world as night. Plan 5's lighting
  deals with sunlight later, if ever.
- **Room markers doing anything.** Marker colours (light spot, body spawn) are reserved in
  the palette now. They generate as air until Plans 4 and 5 give them meaning.
- A planned, finite Terraria-style world layout (spawn in the middle, oceans at the edges).
- Structures bigger than one room-placement cell, connected dungeons, ore veins beyond
  pocket noise, background walls.
- A public plug-in API for custom C++ generators. The internal `ChunkGenerator` interface
  stays.
- Per-pixel generation in GDScript (too slow; already ruled out by the parent spec).
- Real game content. The starter biomes and rooms exist to test and benchmark the engine.

## Anti-goals (failure even if it technically works)

- A **hole** on screen: any chunk inside the camera view not loaded, at the benchmark's
  camera speed.
- A **visible reset**: a chunk regenerating where the player can see it or just was.
  Freshly generated sand and liquid visibly *settling* is not a reset and is acceptable
  (user).
- **Hitches** when the camera crosses chunk borders, from main-thread insert, evict or
  first-upload spikes.
- **Regressing the sim budget** (≤ 6 ms mean on the M1) because generation threads
  compete with it.
- **Non-determinism**: a chunk that differs depending on load order or thread timing.

## Constraints

Unchanged from the parent spec and Plan 1:

- Godot 4.7.2, godot-cpp 10.0.0-stable, C++17.
- `src/core/` stays Godot-free.
- Targets: macOS arm64 (M1) and Linux x86_64 (Iris Xe).
- 64×64 chunks, 60 Hz tick.
- The M1's 4 performance cores belong to the sim's `JobPool`.

New here: **this session runs on the Mac**, so the roles flip. This machine builds,
tests and benchmarks macOS. The Linux run is the hand-off checkpoint. Plan 1 never committed
a Linux result either, so the Linux checkpoint runs both benches (see Verification).

## Approaches considered (where chunk generation runs)

Research before deciding (user's request) covered BS::thread_pool v5.1.0 (MIT, one header,
C++17, task priorities; on macOS its "low priority" only lowers the pthread priority / nice
value, it doesn't set Apple's QoS class, and it can't pin threads to cores there), enkiTS
(zlib, two files, up to 5 task priorities, pinned tasks; Box2D's own samples use it), and
Taskflow / oneTBB (task-graph systems, more than needed; oneTBB is a shared library).

- **A. Own small background generator threads (chosen).** One or two plain `std::thread`s,
  separate from the sim's `JobPool`. Each always takes the **nearest chunk that's still
  wanted**. They run at low OS priority: Apple's QoS class `QOS_CLASS_UTILITY` on macOS, so
  the scheduler prefers the M1's 4 idle efficiency cores, and a raised nice value on
  Linux. About 100 lines, no new dependency, unit-testable in the core.
  - Why: the scheduling we need is "nearest still-wanted chunk, re-evaluated as the camera
    moves". A library queue fixes a task's priority when it's submitted, so stale work
    would need cancel / skip code anyway, which is more code than the pool. The QoS call
    has to be written by hand with any library.
- **D. Move everything onto enkiTS now** (sim, generation, later Box2D). It's the only real
  reason to adopt a library, but it rewrites the working, TSan-clean `JobPool`. Also, a
  generation task already running on a performance core delays the sim's next phase,
  because tasks can't be interrupted. **Deferred:** whether to adopt enkiTS is decided in
  Plan 4, when hooking up Box2D's task callbacks actually needs a scheduler (user's
  decision).
- **B. Run generation on the sim's `JobPool` after each tick (rejected).** `parallel_for`
  blocks the main thread, so generation time becomes frame time, and fast camera moves
  cause spikes.
- **C. Godot's WorkerThreadPool (rejected).** The streaming logic would move into the
  Godot layer, out of reach of the core tests. It also shares threads with Godot's own work
  and gives no control over which cores it uses.

## Design

### Zones and streaming

All zone sizes are in chunks, as half-extents around the follow target's chunk. They are
live, tunable `PixelWorld` properties. The defaults are mine, sized against the
480×270-pixel view, which is about 3.75×2.1 chunks of half-extent:

| zone | default half-extent | meaning |
|---|---|---|
| active | `active_radius = (8, 5)` → 17×11 chunks, about 2.3×2.6 screens | simulates every tick |
| loaded | active + `loaded_margin = 3` → (11, 8) → 23×17 chunks, about 12.5 MB of cells | in memory, asleep; generation targets this whole area |
| discard | loaded + `discard_hysteresis = 2` → (13, 10) | only chunks outside this are discarded |

The hysteresis band stops a target sitting on a zone border from making chunks load and
discard repeatedly. The nearest discard is about 9 chunks (576 px) beyond the view's edge.
That is the "no visible resets" guarantee.

**Sleeping semantics (Simulator change).**
- `Simulator::tick` takes the active rect, or reads it from a setter.
- Chunks outside it are **not** dirty-swapped. Their `next` rect keeps accumulating marks,
  for example water spilling in from an active neighbour, so the work is waiting when the
  chunk wakes.
- A freshly generated chunk's whole-chunk `next` (Plan 1's `load` behaviour) waits the
  same way. Loose generated sand or liquid starts settling when its chunk enters the active
  zone. The active zone reaches about 4 chunks past the view edge, so that happens off
  screen.
- Active chunks may write into sleeping neighbours' cells exactly as into active ones. The
  checkerboard rule is unchanged, because sleeping chunks aren't processed.
- Unloaded chunks still act as solid (Plan 1).

**Inserting a chunk wakes its neighbours' borders.**
- Matter resting against an unloaded (solid) edge must react once the neighbour appears.
  For example, water held back by an unloaded chunk should flow into a newly loaded cave.
- So inserting a chunk marks the touching edge rows / columns of each loaded 8-neighbour as
  `next`-dirty.
- `WorldGrid` gains `insert(std::unique_ptr<Chunk>)`. Plan 1's synchronous `load` becomes
  generate + `insert`. Inserts happen on the main thread, between ticks, as now.

**Discarding.**
- Chunks outside the discard rect go through **one function** (`evict`, name is the
  builder's call). It drops the canvas item and texture and unloads the cells.
- That function is the later persistence hook.

### Generation pipeline (`ChunkStreamer`, core)

- **Owner and threads.** A core class owns the generator threads (`generator_threads`,
  default 2, at least 1). The constructor applies the low-priority OS settings to each
  thread (`#ifdef __APPLE__` / `__linux__`).
- **Main thread, once per frame:**
  1. Hand the streamer the current *wanted* rect (the loaded zone).
  2. Drain up to `max_inserts_per_frame` finished chunks (default 16) into the grid.
  3. Evict anything outside the discard rect.
- **Workers:**
  1. Under a mutex, pick the **nearest** wanted coord that is neither loaded, in flight nor
     ready.
  2. Generate it into a fresh heap `Chunk` without the lock.
  3. Push it to the ready list.
- **Stale work.** A finished chunk that's no longer wanted when drained is dropped. In-flight
  work isn't cancelled: a chunk is about half a millisecond of work.
  - The pick is a linear scan of at most about 400 coords. Mark that with a `ponytail:`
    comment; a priority structure is the upgrade if it ever shows up in a profile.
- **Thread safety.** `ChunkGenerator::generate` is `const` and must be safe to call from
  several threads at once: no mutable state, and noise objects read-only after setup.
  `BoxGenerator` already qualifies.
- **Shutdown** joins the workers before the generator or the material table is destroyed.
  `PixelWorld` owns the streamer and destroys it first.
- **Startup.** `_ready` generates the **active zone synchronously** (on the streamer's
  threads, waiting for them) so the first frame has no holes. The rest of the loaded zone
  streams normally. A short wait at startup is acceptable; one during play is not.
- **Teleports.** If game code moves the target far away, the new area streams in, and
  until then it reads as solid and draws nothing. `PixelWorld.is_area_ready(rect)` lets
  game code wait, for example behind a fade. No other teleport handling.

### Terrain generator (`TerrainGenerator : ChunkGenerator`, core)

- **Config.** A plain core struct `TerrainConfig` holds everything, with materials resolved
  to `MatId`s. The Godot Resources below compile into it once at `_ready`, the same pattern
  as `PixelMaterialSet` → `MaterialTable`.
- **Noise.** **FastNoise Lite** (MIT, single header), vendored at
  `thirdparty/FastNoiseLite/FastNoiseLite.h`. Each noise field is seeded from the world seed
  plus a fixed per-field salt.
- **Determinism rule.** Every decision is a pure function of (world seed, world x, y) or
  (world seed, placement-cell coordinate). Generating a chunk never reads another chunk.
- **Cross-platform equality is not required.** The same seed may differ slightly between
  macOS arm64 and Linux x86_64, since FastNoise Lite uses floats and clang contracts them
  into FMA on arm64. Determinism is required within one build: across load order and
  thread count. If a later save step needs saves to move between machines, it adds
  `-ffp-contract=off` and a golden chunk-hash test then. (My call, from the builder check.)

For each cell (wx, wy) the generator computes, in order:

1. **Surface height** `h(wx) = surface_y + surface_amplitude * noise1d(wx)`, fractal, and
   `depth = wy - h(wx)`. Depth is measured from the **local surface**, not from absolute y,
   so bands follow the hills. This is my call; it keeps the first metres under a hill the
   same as under a valley.
2. **Biome weights.** Biomes don't switch at a line. Each cell gets a **weight per biome**
   that sums to 1, and borders become transition bands (approach A below; user's choice).
   - **Depth band.** Biomes declare `[min_depth, max_depth)`. Band edges are warped by 2-D
     noise (`band_warp` cells) so layer boundaries wobble instead of running flat.
   - **Within the band**, x is split into **regions** of about `region_width` cells, also
     noise-warped. Each band has its own region split. Each region picks one of that band's
     biomes by weighted hash of (seed, band, region index).
   - **Weights are computed directly from the layout.** No scattered sampling is needed,
     because the layout is separable: regions along x within bands along depth.
     - Across a (warped) region border, the two regions' weights ramp with a smoothstep
       over `region_transition` cells.
     - Across a (warped) band border, the two bands ramp over `band_transition` cells.
     - A cell's weight for a biome is (its band's depth weight) × (its region's x weight
       within that band). At most 4 biomes contribute, at a corner where a band border
       meets region borders.
     - Away from borders, a single biome has weight 1, and that's the fast path.
   - The last band's `max_depth` is open, so the deepest biome continues forever.
   - Above the surface (`depth < 0`) is air, always.
   - **The cell's biome** is a **dithered pick**: `hash5(seed, 0, wx, wy, salt)` against
     the cumulative weights. Inside a transition band this gives a ragged pixel fringe, with
     one biome's material thinning out into the other's, instead of a line or a muddy
     average. The cell's materials come from its picked biome.
   - **Prior art (researched).** Minecraft 1.18+ drives terrain shape from shared noise and
     lets biomes pick surface materials. Terraria converts a shared base world in later
     passes and breaks the edges up with scattered patches. KdotJPG's scattered biome
     blending computes biome weights that sum to 1 and blends numeric terrain parameters by
     them. Our separable layout lets us get exact weights without the scattered sampler.
     Climate-based biome selection (Minecraft-style, so neighbours make sense) was
     considered and deferred; it can sit on top of this later.
3. **Biomes don't own the shape.** Every noise field (caves, pockets, pools) is **one
   shared field** for the whole world, with a fixed frequency and stretch. Biomes only set
   **thresholds**, and thresholds are **blended by weight**: `t = Σ wᵢ · tᵢ`. So a cave
   that crosses a border continues smoothly and only grows or shrinks across the
   transition. It never ends in a flat wall where one biome's noise stopped. Frequency and
   stretch aren't per-biome, because blending them distorts a field.
4. **Ground material.**
   - The top `surface_depth` cells of the ground (blended by weight, then rounded) use the
     picked biome's `surface_material` (grass, sand).
   - Below that, the picked biome's `base_material`, except where the shared pocket field
     exceeds the blended `pocket_threshold`, which gives the picked biome's
     `pocket_material` (sand pockets in rock, and so on).
5. **Caves**, from two shared fields:
   - a **tunnel** field, sampled with y scaled by a global `cave_stretch > 1`, so tunnels
     run wider than they are tall (Terraria-like sideways exploration),
   - a blobby **cavern** field for big open rooms.

   A cell is air if either field passes its blended threshold (`tunnel_threshold`,
   `cavern_threshold`). A biome mixes the two styles by setting both thresholds.
   - The thresholds taper to "no caves" over the first `cave_taper_depth` cells below the
     surface. A separate low-frequency field occasionally cancels the taper, giving surface
     cave entrances.
6. **Liquid pools.** Inside cave air, a shared low-frequency pool field above the blended
   `liquid_threshold` fills the cell with the picked biome's `liquid_material` (water near
   the top, oil and lava deeper).
   - **Visible settling is acceptable (user).** Pools generated in mid-air, and sand over
     caves, start falling when their chunk activates. At camera speed that's often on
     screen, and it repeats on every return because chunks regenerate. The generator does
     **not** try to produce terrain already at rest.
   - Cave-floor-only pool placement stays an optional builder's call, not a requirement.
7. **Rooms** overwrite the result (next section).
8. Cells spawn through Plan 1's `spawn_cell`, so shade, temperature and life match painted
   cells.

### Rooms (pixel scenes)

**Resources** (user's decision: **one** shared palette, plus one Resource per room):

- **`PixelRoomPalette`, one for all rooms.** It maps an exact RGBA8 colour to an entry. An
  entry is either a material name, or one of these special kinds:
  - **keep**: leave the terrain as generated. Fully transparent pixels are always *keep*,
    so rooms can be any shape.
  - **marker-light** and **marker-body**: reserved. They generate as air for now.

  Air is just the material `"air"`.
- **`PixelRoom`, one per room.**
  - `image: Texture2D` (a PNG, imported lossless, with no mipmaps and no filter).
  - `weight: float = 1.0`.
  - `anchor`: one of `SURFACE`, `EMBEDDED`, `CAVE`.
  - `biomes: Array[StringName]`: the biomes it may appear in; empty means any. My call to
    put this on the room rather than the biome, since the user wants room-specific info on
    the room.

**Compile.**
- At `_ready`, every room image is read once (`get_image()`, converted to RGBA8) and mapped
  through the palette into a core `RoomDef`: width, height, a per-pixel `MatId` or *keep*,
  weight, anchor and biome mask.
- **A colour that isn't in the palette is an error** naming the room, the pixel position
  and the colour. A room larger than the placement cell is also an error.
- On any error the world refuses to start, with `push_error`, like a bad material set.

**Placement.**
- The world is divided into a coarse **placement grid** of `room_cell_size` cells (default
  256×256, 4×4 chunks).
- For each placement cell, a hash of (seed, cell) decides:
  - whether the cell holds a room (`room_chance`),
  - which room: weighted among the rooms eligible for the **highest-weight** biome at the
    cell's centre (not the dithered pick, so the choice is stable),
  - an offset that keeps the whole room **inside its placement cell**, so rooms never
    overlap and a chunk only consults the 1–4 placement cells it overlaps.
- **Anchors** are resolved analytically, so no other chunk is read:
  - **SURFACE**: bottom row at `h(x)` under the room's centre column. Valid only for
    placement cells whose vertical range contains that height; otherwise no room.
  - **EMBEDDED**: anywhere fully below the surface; stamped into the rock.
  - **CAVE**: valid only if the cave field says the room's centre is open air.
- **Crossing chunk borders.** A room that crosses a chunk border is stamped by each chunk
  for its own part, from the same decision. That's why it comes out identical in any load
  order.

### Godot API

**`PixelWorld` (changed):**
- New properties:
  - `generator: PixelWorldGenerator`. When it's null, Plan 1's `arena` / `BoxGenerator`
    path stays **exactly as Plan 1 built it**:
    - `load_radius`, synchronous loading on the main thread,
    - every loaded chunk active, with no sleeping,
    - no streamer.

    That keeps the Plan 1 tests (including `test_chunks_follow_target`'s two-frame
    expectation), the Plan 1 bench and its committed `macos-m1-plan1.json` baseline valid.
    In practice the `Simulator` active rect is optional: none means everything is active.
    (My call, from the builder check.)
  - `active_radius: Vector2i`, `loaded_margin: int`, `discard_hysteresis: int`. These are
    used on the generator path only (live). `load_radius` stays bound for the arena path.
  - `generator_threads: int = 2` and `max_inserts_per_frame: int = 16` (read at `_ready`).
- New method: `is_area_ready(rect: Rect2i) -> bool`. True when every chunk overlapping the
  world-cell rect is loaded.
- `get_stats()` gains:
  - `chunks_active`
  - `gen_pending`: wanted but not yet loaded
  - `gen_ms`: mean worker time per generated chunk since the last call
  - `chunks_generated`: cumulative
  - `stream_ms`: main-thread insert + evict + first-upload time this frame
  - `view_missing`: chunks overlapping the camera view that aren't loaded. The view comes
    from the viewport and canvas transform. This is the hole detector.

**New Resources:**
- `PixelWorldGenerator`:
  - `surface_y`, `surface_amplitude`, `surface_frequency`
  - `band_warp`, `region_width`, `region_transition`, `band_transition`, `cave_taper_depth`
  - shared fields: `tunnel_frequency`, `cave_stretch`, `cavern_frequency`,
    `pocket_frequency`, `pool_frequency`
  - `room_cell_size`, `room_chance`
  - `biomes: Array[PixelBiome]`, `rooms: Array[PixelRoom]`, `room_palette: PixelRoomPalette`
  - `validate(material_set) -> String`, matching `PixelMaterialSet.validate()`. It checks
    material names, depth bands with no gaps, `region_transition ≤ region_width / 2`,
    `band_transition` no more than half the thinnest band, and every room's colours and
    size.
- `PixelBiome`:
  - `name`, `min_depth`, `max_depth` (−1 = open), `weight`
  - `surface_material`, `surface_depth`, `base_material`
  - `pocket_material`, `pocket_threshold`
  - `tunnel_threshold`, `cavern_threshold`
  - `liquid_material`, `liquid_threshold`

  An empty material name means none.
- `PixelRoomPalette` and `PixelRoom`, as described in Rooms.

Exact property names and noise defaults are the builder's call, as is whether a frequency
field is a raw FastNoise frequency or a feature size in cells.

**Starter content** (test and bench only):
- New materials `dirt` (solid, collision) and `grass` (solid, collision), appended *after*
  the existing 10 so Plan 1's material ids don't move. The Godot test that pins the starter
  set's count and order is updated to match.
- About five biomes:
  - surface forest: grass over dirt
  - surface desert: sand over sand, with rock pockets
  - underground: dirt / rock with sand pockets and water pools
  - caverns: rock with wide caves and oil and water pools
  - deep: rock with lava pools, open-ended
- Three or four rough rooms: a surface hut of wood, an embedded rock shrine with an air
  interior, a cave platform with a water basin. Each exercises one anchor, and at least one
  room is bigger than a chunk, so it crosses borders.
- One palette, `worldgen/` for the `.tres` files, `rooms/` for the PNGs.

### Benchmark and the demo

- **Plan 1 bench stays** as is, with the arena and no generator, so its numbers stay
  comparable.
- **New `bench/bench_world.tscn`.**
  - The default generator and a fixed seed.
  - The camera flies a fixed scripted path at a fixed speed (about 600 px/s; builder
    tunes):
    1. along the surface,
    2. diving down through every depth band,
    3. a long horizontal run through the caverns,
    4. **back over ground already visited**, so discard and regenerate are exercised.
  - **A sim workload that travels with the camera.** Without it, sparse starter pools
    would pass the sim gate trivially and prove nothing about generation threads competing
    with the sim. So the bench paints Plan 1-style fixtures at fixed offsets from the
    camera: sand and water faucets, and a burning oil patch, in the active zone ahead of
    the camera. The JSON records `chunks_simulated_mean` and `cells_updated_mean`. The run
    fails (`budget_pass: false`) if `cells_updated_mean` is below 50% of Plan 1's M1
    figure (about 54k), so a gutted workload can't pass. (My call, from the builder check.)
  - Same command-line contract and JSON style as Plan 1's bench. It writes
    `bench/results/<os>-<machine>-plan2.json`.
  - Keys: Plan 1's keys plus `stream_ms_mean/p99`, `gen_ms_mean`, `chunks_generated`,
    `view_missing_frames` (frames with `view_missing > 0`, warm-up excluded), and
    `budget_pass`.
- **`budget_pass` requires all of:**
  - `sim_ms_mean ≤ 6.0`
  - `stream_ms_mean ≤ 1.0` and `stream_ms_p99 ≤ 3.0`
  - `frame_ms_p99 ≤ 16.6`
  - `view_missing_frames == 0`
- **Demo.** `demo/paint_demo` gains a generated-world mode, or a sibling scene. A WASD
  camera flies freely at a selectable speed, with stats on screen and painting as before.
  This is the user's "fly around and see" check.

### Verification

Core tests (doctest, Godot-free):
- **Determinism.** A chunk generated alone equals the same chunk generated after a scatter
  of others, in a different order, and through the `ChunkStreamer` with 1 and with 4
  threads (byte-compare cells).
- **Rooms across borders.** A room spanning a chunk border, with the chunks loaded in both
  orders, matches the room's pixels exactly, with *keep* pixels showing terrain.
- **Surface.** With caves and rooms off, every column is air above `h(x)` and ground below
  it.
- **Biome selection.** Depth bands and x regions pick the expected biome on a hand-built
  config. Weights sum to 1 everywhere, including at corners.
- **Transitions are gradual.** Across a region border between a sand biome and a dirt
  biome, the fraction of sand cells measured in slices across the transition band falls
  from about 1 to about 0, with no slice jumping by more than a bound the builder sets.
  Outside the band, the fraction is exactly 1 or 0.
- **Biomes don't own the shape.** With two biomes that differ only in materials, the cave
  mask (air or not) is identical to a single-biome world. So assigning biomes never cuts a
  cave.
- **Streamer.**
  - It delivers every wanted chunk.
  - It delivers nearer chunks before farther ones.
  - It drops chunks that are no longer wanted.
  - Shutdown with work in flight is clean.
  - The suite passes under `tsan=yes`.
- **Sleeping.** A chunk outside the active rect isn't simulated, keeps its marks, and
  simulates them once it's inside again.
- **Insert wakes borders.** Water resting against an unloaded neighbour flows into it once
  that neighbour is inserted as an open cave.
- **Bad config.** An unknown material in a biome, a band gap, a room colour missing from
  the palette and an oversized room each give an error naming the culprit.

Godot tests (headless):
- The generated world loads the active zone around the target at `_ready`.
- `is_area_ready` turns true.
- Far chunks are discarded.
- An untouched chunk that's discarded and then reloaded is identical.
- `PixelWorldGenerator.validate` reports a palette error.
- The Plan 1 tests still pass on the arena path.

Checkpoints:
- **Mac (this machine):** builds, all tests, and `bench_world` with `budget_pass: true`,
  committed as `bench/results/macos-m1-plan2.json`. Also a visual fly-through in the demo.
- **Linux hand-off:** `docs/mac-agent.md` becomes a two-platform "bench agent" doc (the
  builder decides the exact form). It tells the Linux machine to build, run both test
  suites, and run **both** benches (Plan 1 and Plan 2). The results are committed as
  `bench/results/linux-irisxe-plan1.json` and `-plan2.json`.

## Left to the builder

- Exact noise types, octaves, frequencies and thresholds for the starter biomes, as long
  as caves read as Terraria-like horizontal exploration.
- Exact property names, and whether frequencies are exposed as feature sizes.
- The streamer's internal structure, and how `view_missing` gets the view rect.
- Optional cave-floor-only liquid placement.
- Whether the demo gains a mode or a sibling scene.
- The form of the two-platform bench-agent doc.
- The camera path and speed of `bench_world`, within "surface, dive, cavern run, return".

## Decisions made in conversation

- Terraria-style: descend and explore sideways (user).
- An open surface with sky; ignore sky light, treat it as night (user).
- An endless world, not a finite planned layout (user).
- Regenerate on every return is acceptable for now; disk persistence may come in a later
  task, so discarding goes through one hook point (user).
- One room palette Resource for all rooms, plus a per-room Resource (user).
- Success is a hitch-free, hole-free fly-through plus the benchmark (user confirmed).
- Generation on its own low-priority threads (approach A). enkiTS is decided in Plan 4
  (user).
- Generated sand and liquids may visibly settle on screen; the generator doesn't produce
  pre-settled terrain (user).
- Smooth biome transitions (user raised it; researched Minecraft, Terraria, Noita and
  KdotJPG's blending). Chosen approach A (user):
  - weights computed directly from the region/band layout,
  - shared noise fields with weight-blended thresholds,
  - dithered material pick in transition bands.

  Rejected or deferred:
  - B, climate-based selection: deferred, can be layered on later.
  - C, porting the scattered blender: it only pays off with an arbitrary 2-D biome map.
- Mine, marked in the text: zone default sizes, depth measured from the local surface,
  room-to-biome eligibility on the room, rooms confined to their placement cell, and the
  bench gate thresholds.
