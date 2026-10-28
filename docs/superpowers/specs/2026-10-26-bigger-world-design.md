# Bigger world — design (small change)

## Intent

world_demo and creatures_demo (and bodies_demo, which shares the generator) produce biomes,
terrain and caves that are too small next to the 8×16 player on a 480×270 view. Make
everything bigger, in proportion, so caves feel roomy for the player.

## Goals

- Typical cave openings are 2–3× the player: about 32–48 cells tall and 16–24 cells wide.
- Biomes, hills, depth bands and every other spatial feature scale up in proportion.
- The three rooms (hut, shrine, platform) are redrawn to suit the bigger world.

## Non-goals / anti-goals

- **No change to how the world is generated.** `terrain.cpp`, `terrain.h` and the generator
  code stay as they are. Thin slivers and pinch points at tunnel tips stay; nothing fills or
  carves them. (The user explicitly ruled this out.)
- No change to the C++ `TerrainConfig` defaults or the core test configs.
- No camera zoom change.

## Approaches considered

- **A. Opening filter** (erase cave cells a min-size box can't fit through, per chunk with a
  margin): guarantees the minimum, but changes generation. Rejected by the user.
- **B. Per-cell width estimate from the noise, filling thin cells**: the user took the "tune
  the noise" half only, with no filling. **Chosen, as pure data tuning.**
- **C. Post-pass in the streamer once neighbours load**: breaks chunk independence. Dropped.

## What changes

1. **`worldgen/default_generator.tres`**: scale by a single factor `S` (starting point 4):
   lengths ×S (`surface_amplitude`, `band_warp`, `region_width`, `region_warp`,
   `region_transition`, `band_transition`, `cave_taper_depth`, `room_cell_size`, which must stay
   a multiple of 64), frequencies ÷S (`surface_frequency`, `tunnel_frequency`,
   `cavern_frequency`, `pocket_frequency`, `pool_frequency`, `entrance_frequency`).
   `surface_y` stays the same unless the spawn needs it to change.
2. **The five biome `.tres` files**: `min_depth` / `max_depth` ×S. `surface_depth` (grass and
   sand skin) scales too, which is my call; the builder can keep it thin if 4× looks wrong.
3. **Cave shape tuning, still data only**: today's measured (area-weighted) median vertical
   opening is 9 cells; plain 4× gives about 33. Lower `cave_stretch` (today 2.5, which flattens
   tunnels) and/or nudge the biome `tunnel_threshold` / `cavern_threshold` values until the
   median vertical opening is ≥ 48 and the median horizontal opening is ≥ 24, while caves stay
   tunnel-like and the amount of solid ground looks about the same.
4. **Rooms**: redraw `hut.png`, `shrine.png`, `platform.png` larger, using the same palette
   colours, anchors and biome tags, with `@light` / `@body` markers kept. My call on size:
   interiors are sized to the player by the same 2–3× rule (for example the hut interior is
   about 48 tall), not blindly ×4. Each room must fit inside one `room_cell_size` cell.
5. **Demos**: the spawn raycasts in `bodies_demo.gd` / `creatures_demo.gd` (from y = −150 to
   300) must cover the new hill range (about ±190 around `surface_y`). Widen them so the
   player, car and wanderers still land on the ground.

## Verification

- A throwaway measuring program (not committed) samples `cave_at` over a few thousand cells
  using the shipped values and reports opening-height and opening-width percentiles. It is the
  check for step 3.
- `./build.sh --verify` passes (includes `test_default_generator_validates`).
- All three demos run: the player spawns on the ground, rooms appear, and caves look roomy.

## Left to the builder

The exact value of `S`, `cave_stretch` and the thresholds, as long as they meet the
measurements in step 3, plus the art of the room redraws.
