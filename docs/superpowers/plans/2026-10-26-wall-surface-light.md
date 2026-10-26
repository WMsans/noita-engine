# Wall Surface Light Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Solid cells pick up light from the open space in front of them, fading over a skin
`light_surface_depth` cells deep (default 10), so walls next to a torch are readable while
shadows stay hard.

**Architecture:** One new compute shader (`LIGHT_SURFACE_COMPUTE`) runs N times after
`LIGHT_OUTPUT_COMPUTE` and before bloom, ping-ponging between `light_texture` and a new
`surface_texture`. Each pass: a solid texel (input opacity ≥ 0.5) takes
`max(own, brightest neighbour × falloff)`; open texels copy through untouched. The output pass
writes into whichever texture makes the last surface pass land in `light_texture`, so the
overlay and bloom keep reading the same texture as today. Pass count and falloff come from two
pure helpers in `core/light_input`.

**Tech Stack:** C++17 GDExtension (godot-cpp), GLSL 450 compute on Godot 4.7.2's main
`RenderingDevice`, doctest for core tests, GDScript for the Godot-side tests.

**Spec:** `docs/superpowers/specs/2026-10-26-wall-surface-light-design.md` (parent:
`docs/superpowers/specs/2026-10-26-lighting-design.md`)

## Global Constraints

- Skin depth 8–10 cells; default `light_surface_depth = 10`; 0 turns the pass off.
- Solid = input opacity ≥ 0.5 (rock, dirt, wood, grass, sand). Open texels are never written by
  the spread.
- The setting is live; at `light_resolution` N the same depth in cells is `ceil(depth / N)` texels.
- No CPU work per cell, no readback.
- `bench_world` Plan 6 gate still passes on the M1: frame p99 ≤ 16.6 ms (committed run: 11.05 ms
  full res, 11.08 ms half res). GPU timestamps are stubbed on Metal, so frame p99 is the gate.
- Build: `./build.sh --test` (core), `./build.sh --verify` (Godot-side, headless).

## Review Focus

1. **Air behind a thin wall stays dark.** Light spreads up to 10 cells into the 6-cell demo wall,
   so the wall's far face glows faintly, but the open air past it must not get any (the pass
   never writes open texels). Checked by eye at the Task 2 checkpoint.
2. **`light_surface_depth = 0` is today's look exactly**: zero passes, the output pass writes
   straight into `light_texture`. Pinned by the Task 1 helper tests (0 passes, falloff 1) and
   the Task 2 routing rule.
3. **An odd/even pass-count flip while running** (depth 9 → 10 → 0) must not error or flash
   black: routing is chosen per frame from the pass count, never cached. Task 2's live test.
4. **Half resolution** halves the pass count (`ceil(10 / 2) = 5`), not the depth in cells. Task 1
   tests.
5. **A resize or `light_resolution` change** recreates `surface_texture` and its sets without
   "Attempted to free invalid ID" (sets freed before textures). Covered by the existing
   `light_resolution` live test running under `--verify`'s error-output check.

---

### Task 1: Pass count and falloff helpers

**Files:**
- Modify: `src/core/light_input.h` (next to `spot_source_radius`), `src/core/light_input.cpp`
- Test: `tests/core/test_light_input.cpp`

**Interfaces:**
- Produces:
  - `constexpr int LIGHT_SURFACE_MAX_DEPTH = 32;` — the setter's upper clamp (cells).
  - `constexpr float LIGHT_SURFACE_FADE = 0.05f;` — the light left after a full skin depth.
  - `int light_surface_passes(int depth_cells, int scale);` — `ceil(clamp(depth, 0, MAX) / scale)`;
    0 when depth ≤ 0 or scale < 1.
  - `float light_surface_falloff(int passes);` — per-step factor `pow(LIGHT_SURFACE_FADE, 1 / passes)`;
    1.0 when passes ≤ 0.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("surface light passes and falloff") {
	CHECK(light_surface_passes(10, 1) == 10);
	CHECK(light_surface_passes(10, 2) == 5);
	CHECK(light_surface_passes(9, 2) == 5);   // rounds up: never a shallower skin
	CHECK(light_surface_passes(10, 4) == 3);
	CHECK(light_surface_passes(0, 1) == 0);
	CHECK(light_surface_passes(-5, 1) == 0);
	CHECK(light_surface_passes(1000, 1) == LIGHT_SURFACE_MAX_DEPTH);
	CHECK(light_surface_passes(10, 0) == 0);
	CHECK(light_surface_falloff(0) == 1.0f);
	CHECK(std::pow(light_surface_falloff(10), 10.0f) == doctest::Approx(LIGHT_SURFACE_FADE));
	CHECK(light_surface_falloff(5) < light_surface_falloff(10));  // fewer steps fade faster
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./build.sh --test`
Expected: compile error, `light_surface_passes` not declared.

- [ ] **Step 3: Implement both helpers in `src/core/light_input.cpp`**, with a one-line header
  comment each, in the style of `spot_source_radius`. Mark `LIGHT_SURFACE_FADE` with a
  `ponytail:` comment: tuned by eye, per-material depth would replace it.

- [ ] **Step 4: Run test to verify it passes**

Run: `./build.sh --test`
Expected: all core tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/light_input.h src/core/light_input.cpp tests/core/test_light_input.cpp
git commit -m "feat(light): surface light pass count and falloff"
```

### Task 2: Surface spread pass, `light_surface_depth` setting, M1 checkpoint

**Files:**
- Modify: `src/godot/light_shaders.h` (new `LIGHT_SURFACE_COMPUTE`, after `LIGHT_OUTPUT_COMPUTE`)
- Modify: `src/godot/light_renderer.h`, `src/godot/light_renderer.cpp`
- Modify: `src/godot/pixel_world.h`, `src/godot/pixel_world.cpp` (property + job plumbing)
- Test: `tests/godot/run_tests.gd`
- Modify: `docs/bench-agent.md` (new result lines), `bench/results/macos-m1-plan6*.json` (re-run)

**Interfaces:**
- Consumes: `light_surface_passes`, `light_surface_falloff`, `LIGHT_SURFACE_MAX_DEPTH` (Task 1).
- Produces:
  - `PixelWorld.light_surface_depth: int` — default 10, setter clamps to 0..`LIGHT_SURFACE_MAX_DEPTH`,
    live, `PROPERTY_HINT_RANGE "0,32,1"`, listed in the lighting settings comment in `pixel_world.h`.
  - `LightRenderJob(renderer, texels, area, bloom_threshold, surface_passes, free_only)` and
    `LightRenderer::render(texels, area, bloom_threshold, int surface_passes)`.

**Shader `LIGHT_SURFACE_COMPUTE`** (8×8 groups, one invocation per light texel):
- Push constants: `ivec2 size; float falloff; float falloff_diag;`
- Bindings: 0 `sampler2D input_tex` (the RGBA8 input; `.a` is opacity, one texel per light
  texel), 1 `rgba16f image2D src`, 2 `rgba16f image2D dst`.
- `c = imageLoad(src, p)`. If `texelFetch(input_tex, p, 0).a < 0.5` store `c` unchanged. Else
  store `max(c.rgb, max(max over 4 edge neighbours × falloff, max over 4 diagonals × falloff_diag))`,
  alpha 1. Neighbours clamped to the texture (edge texels see themselves).
- Comment on top: why (a solid's own probe sits inside it and sees nothing) and that light only
  flows into solids, so shadows in open air are untouched.

**Renderer changes:**
- `surface_texture`: RGBA16F, area texel size, sampled + storage; created in `create_textures`
  next to `light_texture`, freed in `destroy_textures` (not retired: the overlay never wraps it).
- `output_sets[2]` replace `output_set`: `[0]` writes `light_texture`, `[1]` writes `surface_texture`.
- `surface_sets[2]`, built in `create_input_texture` (they bind `input_texture`):
  `[0]` reads `light_texture` → writes `surface_texture`, `[1]` the reverse.
- In `render`, with `passes = p_surface_passes`: the output pass uses `output_sets[passes % 2]`;
  then pass `i` in `0..passes-1` uses `surface_sets[(passes - i) % 2]`. Each pass reads where the
  previous one wrote, and the last one writes `light_texture`. Barrier after each. Push `falloff = light_surface_falloff(passes)`, `falloff_diag = pow(falloff, sqrt(2))`.
  Routing is computed every frame from `passes`, nothing cached.
- Free the new sets before the textures, in the same order rules as the existing sets.

**PixelWorld:** the job is built with `light_surface_passes(light_surface_depth, area.scale)`.

- [ ] **Step 1: Write the failing Godot test**

Add `"light_surface_depth": 10` to `defaults` and `"light_surface_depth": 4` to `written` in
`test_lighting_settings_defaults`, and a new test modelled on `test_light_resolution_default_and_live`:

```gdscript
func test_light_surface_depth_clamped_and_live() -> bool:
	var world: PixelWorld = PixelWorld.new()
	var bad: PackedStringArray = []
	for pair: Array in [[0, 0], [32, 32], [-3, 0], [99, 32], [10, 10]]:
		world.light_surface_depth = pair[0]
		if world.light_surface_depth != pair[1]:
			bad.append("light_surface_depth = %d reads %d, expected %d" % [pair[0], world.light_surface_depth, pair[1]])
	world.free()
	# Odd and even pass counts route the output pass differently; flipping while running must not error.
	var live: PixelWorld = await _make_world()
	for value: int in [9, 10, 0, 10]:
		live.light_surface_depth = value
		await process_frame
		if live.light_surface_depth != value:
			bad.append("light_surface_depth = %d reads %d while running" % [value, live.light_surface_depth])
	live.free()
	if not bad.is_empty():
		printerr("  " + ", ".join(bad))
		return false
	return true
```

(`run_tests.gd` picks up every `test_*` method; no registration.)

- [ ] **Step 2: Run to verify it fails**

Run: `./build.sh --verify`
Expected: FAIL, `light_surface_depth` reads `null`.

- [ ] **Step 3: Implement** the shader, renderer and `PixelWorld` changes above.

- [ ] **Step 4: Run to verify it passes**

Run: `./build.sh --test --verify`
Expected: exit 0, no unexpected error output.

- [ ] **Step 5: Tune by eye on the Mac** in `demo/lighting_demo.tscn` (needs a window):
  wall next to the torch shows rock colour and texture; lit skin visibly ~8–10 cells deep then
  black; the 6-cell wall still leaves the corridor past it dark (Review Focus 1); lava room
  walls lit; `H` (half res) looks the same depth; setting depth to 0 in the inspector matches
  the pre-change look. Adjust only `LIGHT_SURFACE_FADE` if the surface reads too dim or too
  bright, and keep Task 1's Approx test consistent with it. If the user wants to see it, stop
  and show them here.

- [ ] **Step 6: Bench on the M1**

Run (per `docs/bench-agent.md`, real window, not headless):
`godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/macos-m1-plan6.json`
and the same with `--light-resolution=2 --out=bench/results/macos-m1-plan6-halfres.json`.
Expected: `budget pass`, frame p99 ≤ 16.6 ms; record both summary lines in `docs/bench-agent.md`
in place of the old ones, noting the delta from 11.05 / 11.08 ms.

- [ ] **Step 7: Commit**

```bash
git add src/godot/light_shaders.h src/godot/light_renderer.h src/godot/light_renderer.cpp \
        src/godot/pixel_world.h src/godot/pixel_world.cpp tests/godot/run_tests.gd \
        docs/bench-agent.md bench/results/macos-m1-plan6.json bench/results/macos-m1-plan6-halfres.json
git commit -m "feat(light): spread light into solid surfaces"
```
