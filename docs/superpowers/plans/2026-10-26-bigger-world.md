# Bigger World Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scale the default world (biomes, hills, depth bands, caves, rooms) up so caves are roomy next to the 8×16 player, by changing data and art only.

**Architecture:** Every spatial setting in `worldgen/default_generator.tres` and the five biome `.tres` files is scaled by one factor S (start at 4). Cave shape is then tuned (`cave_stretch`, biome thresholds) against a throwaway measuring program until openings hit the target size. The three room PNGs are redrawn larger, and the demo spawn raycasts are widened to cover the taller hills.

**Tech Stack:** Godot 4 `.tres` resources, C++17 (measuring program only, links `src/core/terrain.cpp`), Python + Pillow 13 (room PNGs), GDScript (demos).

**Spec:** `docs/superpowers/specs/2026-10-26-bigger-world-design.md`

## Global Constraints

- Do not edit `src/core/terrain.cpp`, `src/core/terrain.h`, any generator C++ code, the C++ `TerrainConfig` defaults or the core test configs (`tests/core/terrain_test_util.h`).
- Thin slivers and pinch points stay. Nothing fills or carves them.
- No camera zoom change.
- `room_cell_size` must be a multiple of 64 (`CHUNK`), and every room must fit inside one cell.
- The measuring program lives in the scratchpad and is never committed.
- Target (area-weighted, meaning each cave cell counts the length of the air run it sits in): median vertical opening ≥ 48 cells, median horizontal opening ≥ 24 cells.

## Review Focus

1. **Surface rooms:** a hut (anchor Surface) taller than before must still fit between the surface and its cell's top edge (`room_in_cell` returns nullopt otherwise). Rooms must still show up in the forest. Task 3 checks this with the measuring program.
2. **Spawning on a hill or in a valley:** with hills about ±190, the demo spawn raycast must start above the highest possible surface. Otherwise it begins underground and hits the ceiling of a cave. Task 4 covers this.
3. **Surface openings:** `cave_taper_depth` ×S keeps caves from breaking the surface everywhere. Entrances must still exist (`entrance_frequency` ÷S). Task 2 reports the number of surface openings.
4. **Overall cave amount:** making caves rounder (a lower `cave_stretch`) and thresholds higher must not turn the underground into mostly air. Task 2 reports the cave fraction and keeps it within ±25% of today's.
5. **The streaming Godot test** (`tests/godot/run_tests.gd:646`) loads `default_generator.tres` and must still pass with the new values. Task 5 runs it.

---

### Task 1: Measuring program

**Files:**
- Create: `$SCRATCH/measure.cpp`, where `$SCRATCH=/tmp/claude-1000/-mnt-windows-Godot-noita-engine/1ac1d4b4-62b9-4a50-bb65-e5332239fd97/scratchpad` (not committed)

**Interfaces:**
- Produces: `$SCRATCH/measure <repo_root> [seed]`, which prints these lines:
  - `vertical p10=.. p25=.. p50=.. p75=.. p90=..`
  - `horizontal p10=.. p25=.. p50=.. p75=.. p90=..`
  - `cave_fraction=..`: cave cells ÷ underground cells in the sampled area
  - `surface_openings=..`: columns whose first ground row is a cave, per 1000 columns
  - `rooms=<name>:<count> ...`: `room_in_cell` hits over the sampled cells

- [ ] **Step 1: Write `measure.cpp`.** It reads `worldgen/default_generator.tres` and the five biome files with a tiny `key = value` line parser, fills a `TerrainConfig` and its `BiomeDef`s, uses a stub `MaterialTable` (rock, dirt, sand, grass as Solid; water, oil, lava as Liquid), and calls `TerrainGenerator::compile(cfg, table, seed, err)`. Rooms: `room_in_cell` only needs dimensions, so read each PNG's width and height from its IHDR header (big-endian u32s at byte offsets 16 and 20). Take the anchor and biomes from the room `.tres` files, fill every pixel with `0x000000FF` (air in the palette), and add that one air entry to `cfg.palette`. Sample columns x ∈ [−6000, 6000) step 4 over the depths [surface, surface + 3000) for vertical runs, and rows every 4 cells for horizontal runs, counting only cells at or below `surface_height(x)`. Use area weighting: push each run's length once per cell in the run.
- [ ] **Step 2: Build and run against the current files.**
  Run: `g++ -O2 -std=c++17 -I src/core -I thirdparty $SCRATCH/measure.cpp src/core/terrain.cpp src/core/material.cpp src/core/generator.cpp -o $SCRATCH/measure && $SCRATCH/measure .`
  Expected: vertical p50 is about 9 and horizontal p50 about 19. That matches the brainstorming measurement, which confirms the parser. Write down the baseline `cave_fraction` and `surface_openings`.

### Task 2: Scale the generator and biomes, then tune the cave shape

**Files:**
- Modify: `worldgen/default_generator.tres`, `worldgen/{forest,desert,underground,caverns,deep}.tres`

**Interfaces:**
- Consumes: `$SCRATCH/measure` (Task 1)
- Produces: the final `room_cell_size` value (Task 3 sizes rooms against it)

- [ ] **Step 1: Apply S = 4.**
  - In `default_generator.tres`, multiply these by 4: `surface_amplitude` (192), `band_warp` (96), `region_width` (3072), `region_warp` (384), `region_transition` (384), `band_transition` (128), `cave_taper_depth` (160), `room_cell_size` (512).
  - Divide these by 4: `surface_frequency` (0.001), `tunnel_frequency` (0.003), `cavern_frequency` (0.0015), `pocket_frequency` (0.0075), `pool_frequency` (0.005), `entrance_frequency` (0.0005).
  - Leave `surface_y`, `entrance_threshold` and `room_chance` unchanged.
  - In the biome files, multiply `min_depth` / `max_depth` by 4 (120→480, 500→2000, 1500→6000) and `surface_depth` by 4 (4→16, 6→24).
- [ ] **Step 2: Measure.** Run `$SCRATCH/measure .`. Expect vertical p50 of about 33, which is still under 48.
- [ ] **Step 3: Tune until the target is met.** First lower `cave_stretch` (try 1.5, then 1.2). Then, if needed, raise every biome's `tunnel_threshold` by the same factor. Keep the `cavern_threshold`s unless the cave fraction drops. Stop when all of these hold: vertical p50 ≥ 48, horizontal p50 ≥ 24, `cave_fraction` within ±25% of the Task 1 baseline, and `surface_openings` > 0.
- [ ] **Step 4: Check that it validates.** Run `./build.sh --verify`. Expected: `test_default_generator_validates` passes. Other failures caused by these values get fixed here. Failures from Task 3 or 4 territory get noted for those tasks.
- [ ] **Step 5: Commit.**
  ```bash
  git add worldgen/
  git commit -m "feat(worldgen): scale the default world 4x and widen caves"
  ```

### Task 3: Redraw the rooms

**Files:**
- Modify: `rooms/hut.png` (today 48×32, Surface, forest), `rooms/shrine.png` (96×72, Cave), `rooms/platform.png` (70×24, Embedded), plus their `.tres` files only if the anchor or biomes change (they shouldn't)
- Create: `$SCRATCH/draw_rooms.py` (not committed)

**Interfaces:**
- Consumes: `room_cell_size` = 512 (Task 2), and the palette colours in `rooms/palette.tres`: air `#000000`, wood `(0.478,0.322,0.188)`, rock `(0.420,0.396,0.376)`, dirt `(0.420,0.290,0.180)`, water `(0.125,0.376,0.753)`, `@light` `(1,0.933,0.4)`, `@body` `(1,0.4,0.8)`. Any other colour is `MAT_KEEP` (transparent). Use exact 8-bit values that round-trip to these.

- [ ] **Step 1: Write `draw_rooms.py`.** It uses Pillow to draw each room with the same layout idea as the current PNG, at its new size, using only palette colours plus transparency. Size the interiors by the 2–3× player rule: an open air space at least 48 tall and 24 wide wherever the player is meant to stand. Suggested sizes: hut about 128×96, shrine about 256×192, platform about 192×64. Keep every `@light` and `@body` marker the current room has, and keep each marker's position relative to the room. Wall and floor thickness is about 4–6 cells, so the room still reads as solid at the new scale.
- [ ] **Step 2: Generate and check them.** Run it, then open each PNG (with the Read tool) next to the old one. Each must be under 512 on both sides and use only palette colours or alpha 0.
- [ ] **Step 3: Check that rooms still place.** Run `$SCRATCH/measure .` (it re-reads the PNG sizes). Expected: all three names have a count > 0 in the `rooms=` line. If the hut is 0, shrink its height (Review Focus 1).
- [ ] **Step 4: Commit** (let Godot re-import first: `godot --headless --path . --editor --quit-after 30`).
  ```bash
  git add rooms/
  git commit -m "feat(rooms): redraw rooms for the bigger world"
  ```

### Task 4: Demo spawns

**Files:**
- Modify: `demo/bodies_demo.gd:21`, `demo/creatures_demo.gd:19-24`, and the `Camera2D` position in `demo/world_demo.tscn`, `demo/creatures_demo.tscn` and `demo/bodies_demo.tscn` if it is used before the first follow

**Interfaces:**
- Consumes: hill range `surface_y ± surface_amplitude` = −80 ± 192 (Task 2)

- [ ] **Step 1: Widen the raycasts.** Change both spawn raycasts to run from y = −400 to y = 400. Spawn heights relative to `ground_y` (−20 for the player, −60 for wanderers, whatever bodies_demo uses for the car) stay as they are.
- [ ] **Step 2: Check the world_demo start.** world_demo has no raycast and its camera starts at (0, −60). Run the scene and confirm the start view shows the surface. If it doesn't, add the same raycast to `paint_demo.gd`'s `_ready`, but only when the generator is set, and move the camera to `ground_y`.
- [ ] **Step 3: Run each demo.**
  Run: `godot --path . demo/creatures_demo.tscn`, then do the same for `bodies_demo.tscn` and `world_demo.tscn`.
  Expected: the player, car and wanderers land on ground, not inside rock or a cave ceiling. Walking or flying around shows roomy caves and the new rooms.
- [ ] **Step 4: Commit.**
  ```bash
  git add demo/
  git commit -m "fix(demo): spawn raycasts cover the taller hills"
  ```

### Task 5: Full verification

- [ ] **Step 1:** Run `./build.sh --test --verify`. Expected: the core suite and the Godot tests (including the streaming test at `run_tests.gd:646`) pass, with no new errors.
- [ ] **Step 2:** Run `$SCRATCH/measure .` one last time and paste its output into the final report, alongside the chosen S, `cave_stretch` and the threshold values.
