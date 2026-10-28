# Bench agent: build, test and bench this branch on macOS or Linux

Everything below runs from the repository root with `scons` and Godot 4.7.x on `PATH`. The
commands are the same on both platforms; `./build.sh` picks `macos`/`linux` and `arm64`/`x86_64`
from `uname` and passes them to `scons` explicitly. Copy-pasteable.

Checkpoints: `bench/results/macos-m1-plan1.json` through `macos-m1-plan6.json` are done on
the Apple M1 machine (Plan 6 was red on the frame gate until the 2026-10-26 fixes; see the status
section). The Linux Iris Xe
machine owes `linux-irisxe-plan1.json` and `linux-irisxe-plan6.json`. Each plan's `bench_world`
run replaces the one before: same scene, same camera path, the earlier workload plus the new one
(Plan 3 explosions, Plan 4 bodies, Plan 5 creatures, Plan 6 lighting), and the earlier gates plus
the new ones.

## 1. Get the branch

```bash
git fetch origin
git checkout noita-engine
git pull --ff-only
git submodule update --init --recursive
```

## 2. Build, core tests and Godot tests in one command

```bash
./build.sh --release --test --verify
```

That builds `template_debug` (the library the editor loads) and `template_release`, then builds
and runs the C++ (doctest) suite and the headless Godot suite. Both native libraries carry the
same simulator; the editor always loads `template_debug`, so run the benchmarks with whatever
the editor loads — do not delete the debug library to force the release one, the extension just
fails to load.

Platform specifics:

- **macOS arm64:** both libraries are `bin/libnoita_engine.macos.*.arm64.dylib`.
- **Linux x86_64:** both libraries are `bin/libnoita_engine.linux.*.x86_64.so`. Needs a C++17
  compiler, `scons` and Godot 4.7.x; run the benches under X11/Wayland with a real window (see
  below), not `--headless`.

If you need the steps separately:

```bash
scons platform=macos arch=arm64 target=template_debug     # or platform=linux arch=x86_64
scons tests && build/tests/run_tests                      # core suite
godot --headless --path . -s tests/godot/run_tests.gd     # Godot suite
```

On a cold checkout (no `.godot/` directory yet) run `godot --headless --path . --editor
--quit-after 30` first so the project imports; a cold `--import` can exit 134 from an upstream
Godot bug even after importing successfully. `build.sh --verify` already handles this, and it
also redirects the XDG dirs into `build/` so a sandboxed machine cannot crash Godot on startup.

## 3. Expected tails

`./build.sh --release --test --verify` ends with:

```
==> Build OK: ... libnoita_engine.<platform>.template_debug.<arch>.<ext>
==> Build OK: ... libnoita_engine.<platform>.template_release.<arch>.<ext>
[doctest] test cases:    152 |    152 passed | 0 failed | 0 skipped
[doctest] Status: SUCCESS!

==> Running Godot-side tests (headless)...
    OK: ALL GODOT TESTS PASSED.

==> Done.
```

Exit code 0. Do not accept `Could not find type PixelWorld` or `Cannot infer the type` — those
mean the native library did not load (`build.sh --verify` fails on them).

## 4. Benchmarks

Both need a **real window** — do not pass `--headless`, the point is frame time with rendering.
Run them on an otherwise idle machine (no builds, no browser windows on the GPU).

Plan 1 (arena: fixed painted layout, camera pans across 1024×512 and back):

```bash
godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/macos-m1-plan1.json
godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/linux-irisxe-plan1.json
```

Plan 6 (generated world, the earlier workload plus lighting): the camera flies the
surface/dive/cavern-run/return polyline at 10 cells per frame while chunks stream in, sleep and
get discarded; every 20 frames a radius-24 sand disc is blown up with a burst of visual sparks;
every 10 frames 10 crates and boulders and 8 wandering creatures drop above the camera, and
anything more than 600 cells away is freed; every 60 frames a 40×6 lava pool is painted at
camera offset (−200, 60); and a `PixelLight` of radius 48 with flicker 0.3 is a child of the
camera. About 53 bodies and 43 creatures are alive per frame, and the frame builds its light
inputs, spots and GPU cascades every frame from the first one.

```bash
godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/macos-m1-plan6.json
godot --path . res://bench/bench_world.tscn -- --frames=1800 --light-resolution=2 --out=bench/results/macos-m1-plan6-halfres.json
godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/linux-irisxe-plan6.json
```

`--light-resolution=N` runs the light pass at 1/N linear resolution: `PixelWorld.light_resolution`
is "world cells per light texel", any value in 1..4, where 1 (the default) is full resolution, 2 is
half and 4 is quarter. It trades light detail and shadow sharpness for cost — the input is `1/N²`
the texels, the cascade and bloom textures shrink with it, and probes are spaced in texels so the
cascade reach in world cells grows with N. The committed default stays 1 (unchanged look and cost);
a value outside 1..4 is refused rather than clamped, so a run always measures what it says in its
JSON.

Plan 6 **needs a real GPU**: lighting is the point of the run. Do not pass `--headless`; there is
no `RenderingDevice` then, lighting is a silent no-op with zeroed stats, and the
`light_spots_mean ≥ 1` gate is what catches that. Leave the window size alone (resizing changes
the lit area, which is the view plus `light_margin`).

Pick the `--out` that matches the machine. Defaults: `--frames=1800`; `--out=user://bench.json`
for Plan 1 and `--out=user://bench_world.json` for Plan 2 (`bench/bench.gd` and
`bench/bench_world.gd`); a relative `--out` is resolved against the current directory and
missing parent directories are created. `--frames` at or below 120 is refused without writing
a file.

Expected: exit code 0, the JSON file exists, `budget_pass` is `true`, and one summary line is
printed. Plan 1 (the committed `bench/results/macos-m1-plan1.json`):

```
bench: 1680 frames (+120 warm-up) | sim 3.53 ms mean / 5.09 p99 | frame 8.35 / 10.53 ms | upload 1.02 | chunks 53.0 | cells 108384 | budget pass | bench/results/...
```

That line is the committed artifact, so a fresh run is compared against it; a re-run on an
otherwise idle M1 lands within noise (sim p99 5.14, frame p99 10.56, upload 1.04).

Plan 6 on this M1 (the committed `bench/results/macos-m1-plan6.json`), re-run with the surface
light pass on (`light_surface_depth` 10, ten extra full-screen compute passes):

```
bench world: 1680 frames (+120 warm-up) | sim 3.15 ms mean / 5.15 p99 | frame 8.95 / 11.19 ms | stream 0.11 / 0.45 ms | gen 0.88 ms | chunks 6099 | cells 345155 | view-missing 0 frames | particles 313 / visual 44, 0.037 ms mean / 0.083 p99, dropped 1890 | bodies 53 (awake 42), physics 0.16 + shapes 0.78 + stamp 0.43 = 1.37 ms mean / 2.74 p99, 3.8 builds | creatures 43, 0.172 ms mean / 0.273 p99 | light 0.66 ms cpu mean / 1.15 p99 | light 0.000 ms gpu mean / 0.000 p99 (timestamps stubbed), spots 5.3 | budget pass | bench/results/...
```

`--light-resolution=2` (the committed `macos-m1-plan6-halfres.json`, five surface passes) lands at
frame 8.70 / 11.02 ms. Before the surface pass the two were 11.05 and 11.08 ms p99, so the pass
costs about 0.1 ms of frame p99 at full resolution and nothing measurable at half.
Two runs of each agreed within 0.1 ms at p99.

`frames` in the JSON is the number of sampled frames, i.e. `--frames` minus the 120 warm-up
frames. `frame`/`sim`/`upload`/`stream` `*_ms_mean` and `*_p99` are over those samples; p99 is
the nearest-rank 99th percentile, not the maximum. `gen_ms_mean` is the mean over the frames
that generated anything, `chunks_generated` is cumulative, `chunks_simulated_mean` is how many
chunks were awake per frame, and `view_missing_frames` counts frames with a hole in the view.
`worker_threads` is the node's configured value; `-1` means auto (`JobPool::default_workers()`:
`min(8, hardware_concurrency - 1)`, so 7 on the M1). `particle_ms_*` is the particle step per tick,
`particles_material_mean` / `particles_visual_mean` the particles in flight per frame, and
`particles_dropped` the cumulative material particles that found nowhere to land.
`bodies_mean` / `bodies_awake_mean` are rigid bodies alive / awake per frame;
`physics_ms_*` (Box2D step), `shape_ms_*` (terrain shape builds), `stamp_ms_*` (stamp +
unstamp) and their sum `body_ms_*`; `shape_builds_mean` is chunk shape rebuilds per tick.
`creatures_mean` is creatures alive per frame and `creature_ms_*` the creature grid pass.
`light_cpu_ms_*` is the light-input build for one frame — grid walk into the RGBA8 texels (the
whole area when the lit area changed, otherwise only the chunks that changed), marker and spot
collection, spot discs, and the copy of those texels into the render job's upload buffer — the
plan's 1 ms CPU budget (the copy is inside the timer, not a separate untimed step);
`light_gpu_ms_*` is the light + bloom compute pass from RenderingDevice timestamps;
`light_spots_mean` is how many lights the frame used (the camera torch plus room markers);
`light_gpu_ms_supported` says whether the driver reported any GPU time at all, and
`light_gpu_ms_stubbed` says the run was allowed to report none because it is the known Metal stub
(both are recorded so the exemption is auditable). `light_resolution` is the setting the run used
(1..4, i.e. 1 full, 2 half, 4 quarter), read back from the node so it cannot disagree with what ran.

Budgets (`bench.gd` and `bench_world.gd`; do not relax them to make a run pass):

| gate | Plan 1 | Plan 6 (`bench_world`) |
|---|---|---|
| `sim_ms_mean` | ≤ 6.0 | ≤ 6.0 |
| `stream_ms_mean` / `stream_ms_p99` | — | ≤ 1.0 / ≤ 3.0 |
| `frame_ms_p99` | — | ≤ 16.6 |
| `cells_updated_mean` | — | ≥ 54192 |
| `view_missing_frames` | — | 0 |
| `particle_ms_mean` | — | ≤ 1.0 |
| `particles_material_mean` | — | ≥ 200 |
| `body_ms_mean` | — | ≤ 2.0 |
| `bodies_mean` | — | ≥ 40 |
| `creature_ms_mean` | — | ≤ 1.0 |
| `creatures_mean` | — | ≥ 30 |
| `light_cpu_ms_mean` | — | ≤ 1.0 |
| `light_gpu_ms_mean` | — | ≤ 3.0 (light + bloom) |
| `light_gpu_ms_mean > 0` | — | lighting really ran (see the Metal note) |
| `light_spots_mean` | — | ≥ 1 |

**macOS/Metal cannot report GPU time.** Godot 4.7.2's Metal driver stubs RenderingDevice
timestamp queries (`timestamp_query_pool_get_results` just zeroes the buffer), so
`light_gpu_ms_*` is always 0 there. The bench keys that one exemption to the OS, not to the
samples: on macOS `OS.get_name() == "macOS"` with not a single sample above zero records
`light_gpu_ms_supported: false` **and** `light_gpu_ms_stubbed: true` and skips **only** the
`light_gpu_ms_mean > 0` clause. Everywhere else the clause always applies — an all-zero series on
Linux (or on macOS if it ever reports GPU time) means the light pass never ran, e.g. a
`LightRenderer` that failed to initialise, and it fails the gate instead of passing on the CPU and
spot numbers. The `≤ 3.0` budget, the CPU budget and `light_spots_mean ≥ 1` always apply. On
Metal the only GPU signal left is `frame_ms_p99`.

If `budget_pass` is `false`: re-run twice to rule out a cold or throttled machine, then record
the numbers and report back before touching the simulator or the workload.

## 5. Commit

```bash
git add bench/results/<machine>-plan1.json bench/results/<machine>-plan6.json
git commit -m "bench: <machine> results"
git push
```

Commit the result JSON files only — never edit a budget or the workload to turn a red run green.
The checkpoint is done when both files are committed with `budget_pass: true`. On the Linux
machine that is `linux-irisxe-plan1.json` **and** `linux-irisxe-plan6.json`; Plans 2 to 6 are
complete only then.

## If anything fails, report back

- **Build fails:** the last ~50 lines of the `scons` output, plus `scons --version` and
  `clang --version` (macOS: also `xcode-select -p`) or `g++ --version` (Linux).
- **Tests fail:** the full output of `build/tests/run_tests` (or
  `godot --headless --path . -s tests/godot/run_tests.gd`), including which cases failed.
- **Bench fails or misses a budget:** the JSON file, the printed `bench:` or `bench world:`
  summary line, and which library was actually loaded:
  - macOS: `DYLD_PRINT_LIBRARIES=1 godot --path . res://bench/bench.tscn -- --frames=1800 ... 2>&1 | grep noita`
  - Linux: `LD_DEBUG=libs godot --path . res://bench/bench.tscn -- --frames=1800 ... 2>&1 | grep noita`
- **`bench_world` gate diagnosis:** append the `bench world:` summary line, and say whether the failure is
  `sim_ms_mean` (generated content churning — check for liquid that never settles),
  a stream gate (check `generator_threads` and `max_inserts_per_frame`), or
  `view_missing_frames > 0` (a real streaming hole), or a particle gate (`particle_ms_mean`:
  profile `ParticlePool::step`; `particles_material_mean` under 200: say so, don't shrink it), or
  `body_ms_mean` (say which of physics / shapes / stamp dominates), or a creature gate, or a
  light gate (`light_cpu_ms_mean`: profile `PixelWorld::build_light_frame` — the dirty-chunk scan
  and `build_light_rects`, the spot discs and the restore of the last frame's spot rects;
  `frame_ms_p99`
  with `lighting_enabled = false` says whether the light pass is what pushed it over;
  `light_spots_mean` under 1 means lighting never ran — keep the `--headless` out of it).
- **Environment:** `sw_vers`, `sysctl -n machdep.cpu.brand_string`, `sysctl -n hw.ncpu` on
  macOS; `lsb_release -a`, `lscpu` (or `grep 'model name' /proc/cpuinfo`), `nproc` on Linux.

## Status on this machine

Executed on 2026-10-25 on the Apple M1 machine (Apple M1, 8 cores, macOS 26.4.1, Godot 4.7.2).
The branch was already checked out here and is not pushed, so step 1's `git fetch` /
`git pull --ff-only` was **not** run — that part is for whoever runs this doc next.
`./build.sh --release --test --verify` passed: both libraries OK,
`173 | 173 passed`, `Status: SUCCESS!`,
`OK: ALL GODOT TESTS PASSED.` (Plan 6 re-ran it; Plans 1–5 saw 152 core cases, Plan 6 161, the
half-resolution follow-up added 4 core cases and 1 Godot case, the half-resolution review fix
added 2 more, the per-dirty-chunk inputs added 4, and their review fix added 1 — 173. The
2026-10-26 frame-gate fix added 1 Godot case, `test_body_texture_change_rebuilds`.)

Plan 1 — `bench/results/macos-m1-plan1.json`: `budget_pass: true`, `sim_ms_mean` 3.53 ms
(budget 6.0), `sim_ms_p99` 5.09 ms, `frame_ms_mean` 8.35 ms, `upload_ms_mean` 1.02 ms,
`cells_updated_mean` 108384. Three runs spread 3.48–3.55 ms, so this is inside run-to-run noise.
That JSON predates the 2026-10-26 changes and was not re-committed. On the current code the arena
reads `sim_ms_mean` 3.80–3.87 and `upload_ms_mean` 0.29, against 3.69–3.72 and 0.66 for the
unchanged code re-run the same day, so sim + upload went from 4.34–4.39 to 4.09–4.17 ms. The
chunk colour conversion now runs on the sim's pool right after the tick, and part of its cost shows
up in the next tick's `sim_ms` (a serial table conversion reads 3.63–3.66 sim / 0.54 upload).

Plan 2 — `bench/results/macos-m1-plan2.json`: `budget_pass: true`, `sim_ms_mean` 4.39 ms
(budget 6.0), `sim_ms_p99` 7.22 ms, `stream_ms_mean` 0.07 ms and `stream_ms_p99` 0.36 ms
(budgets 1.0 / 3.0), `frame_ms_p99` 12.33 ms (budget 16.6), `gen_ms_mean` 0.68 ms,
`chunks_generated` 6099, `cells_updated_mean` 325127 (floor 54192), `view_missing_frames` 0.

This Plan 2 figure depends on the committed `worldgen/*.tres` values: tuning `liquid_threshold`
to 0.15 on `underground`, `caverns` and `deep` is what brings `sim_ms_mean` from 6.22 ms (a
fail) to 4.39 ms. `forest` (0.08) and `desert` (0.05) were left alone. If a later content change
raises liquid again, this bench will fail first and by a wide margin — the generated liquid is
what dominates the per-tick cost, and it never comes to rest on the cave floors.

Plan 3 — `bench/results/macos-m1-plan3.json`: `budget_pass: true`, `sim_ms_mean` 4.56 ms
(Plan 2: 4.39; the landed ejecta settle), `sim_ms_p99` 7.49 ms, `frame_ms_p99` 10.76 ms,
`stream_ms_mean` 0.08 / p99 0.40 ms, `cells_updated_mean` 328339, `view_missing_frames` 0,
`particle_ms_mean` 0.018 ms (budget 1.0), p99 0.068 ms, `particles_material_mean` 233 (floor
200), `particles_visual_mean` 41, `particles_dropped` 250 over about 75 000 thrown cells.

Plan 4 — `bench/results/macos-m1-plan4.json`: `budget_pass: true`, `sim_ms_mean` 4.40 ms,
`frame_ms_p99` 14.48 ms, `bodies_mean` 53 (awake 41), `body_ms_mean` 1.28 ms (budget 2.0) =
physics 0.10 + shapes 0.76 + stamp 0.43, `body_ms_p99` 2.41 ms, 3.3 shape builds per tick. The
bodies fall under the sand and water faucets, so their chunks change every tick: shape
rebuilds are rate-limited to one per chunk every 4 ticks, which is what brings shapes from
1.55 ms to about 0.75 ms.

Plan 5 — `bench/results/macos-m1-plan5.json`: `budget_pass: true`, `sim_ms_mean` 4.40 ms,
`frame_ms_p99` 15.13 ms (budget 16.6: the tightest margin now), `creatures_mean` 43,
`creature_ms_mean` 0.155 ms (budget 1.0), p99 0.225 ms, `body_ms_mean` 1.37 ms,
`particles_dropped` 1640 (creatures knocking sand out of piles add particles; not gated). That
run predates the lighting pass.

Plan 6 — `bench/results/macos-m1-plan6.json`: **`budget_pass: true`** since the 2026-10-26
frame-gate fix (the last section below), at frame 8.80 / 11.05 ms, sim 3.12 / 5.15 ms, upload 0.60
ms, `light_cpu_ms_mean` 0.55. The paragraphs from here up to that section are the history of the
red run and its first two fallbacks. **Their "GPU-bound" diagnosis was wrong**: that section
has the measurements.

Before the fix, Plan 6 was **`budget_pass: false`** on this M1. Every gate passed except
`frame_ms_p99`: 20.96 ms against
the 16.6 ms budget (earlier runs: 20.83, 20.75, 21.16, 20.89, 20.91, 21.14, 21.08). The lighting
gates themselves pass — `light_cpu_ms_mean` 0.52 ms (budget 1.0, p99 0.90), `light_spots_mean` 5.3
(floor 1), and `light_gpu_ms_supported: false` with `light_gpu_ms_stubbed: true` because Metal
cannot report GPU time, so the only GPU signal is the frame time. `light_resolution` 1,
`sim_ms_mean` 4.45, `stream_ms_mean` 0.08, `frame_ms_mean` 11.21, `cells_updated_mean` 345160,
`view_missing_frames` 0, `body_ms_mean` 1.38, `creature_ms_mean` 0.158.

The light pass is the whole difference: the same scene with `lighting_enabled = false` reproduces
Plan 5 (frame 9.98 / 15.01), and the pass alone — bench workload, torch and lava removed — gives
frame 11.22 / 20.95. It scales with the lit area, which is the view plus `light_margin` 240
(992×768 at the default window): `light_margin = 0` (480×256) gives 10.31 / 16.61, `light_margin =
120` gives 10.68 / 18.25. So the pass adds about 1.3 ms of frame time on average, but that delta
is unmeasured GPU time on Metal: the frame time cannot separate the light pass from everything
else the GPU does, so the plan's 3 ms light + bloom GPU budget cannot be checked there. It adds
~6 ms at p99, and Plan 5's baseline was already at 15.1 ms of the 16.6 ms budget. Plan 5's
numbers were measured before the light pass existed, so this is new GPU work, not a regression in
the simulator.

The plan's fallbacks for a GPU budget miss (half resolution, per-dirty-chunk inputs, then a
GPU-side input pass) each need the user's go-ahead; the first two have landed (the second one is
described below) and the third is not implemented. Note that half resolution cuts the upload and
cascade work but not the CPU input walk, and it is the one that touches this failure.

Plan 6 half resolution — `bench/results/macos-m1-plan6-halfres.json` (same scene, one build,
`--light-resolution=2`): **still `budget_pass: false`**, but the frame p99 drops from 21.14 to
**17.22 ms** (−19%) and the mean from 11.24 to 10.68 ms. A second run of the same settings measured
17.37 ms p99 (and a second default run 21.08 ms), so it is a real improvement, not noise — and it
is still about 0.6 ms over the 16.6 ms budget. The other gates are unchanged (`sim_ms_mean` 4.44,
`stream_ms_mean` 0.08, `view_missing` 0, `body_ms_mean` 1.39, `creature_ms_mean` 0.161);
`light_cpu_ms_mean` 0.585 (p99 0.690 — the block packing walks the same cells with more per-cell
work, while the upload copy that p99 sees drops to a quarter).
`light_spots_mean` 5.3, `light_gpu_ms_stubbed` `true`, `light_resolution` 2. What it costs in
looks: light is 2-cell blocks everywhere (softened by the overlay's linear filter), shadow edges
are chunkier, and the source disc is divided by the scale, so the torch glow is smaller and dimmer
(the capture harness's torch/far-dark ratio drops from 19.8x to 10.5x). Coarser probes also mean the cascade reach in world cells doubles per step,
so distant light travels farther than the rays' near field resolves. It
is opt-in: the committed default is `light_resolution = 1`.

Plan 6 per-dirty-chunk inputs (`src/core/light_input.*`, `Chunk::light_dirty`, `PixelWorld`): the
light pass no longer rebuilds the whole 992×768 lit area every frame. `PixelWorld` keeps a
persistent base of emission/opacity (materials and burning cells, no spots) and a persistent upload
buffer. Each lit frame repaints only the texels of the chunks that changed since the last lit frame
(the `light_dirty` flag, cleared per chunk) plus the chunks that were unloaded, coalesced into x-runs
per chunk row and built in one pool dispatch; the previous frame's spot rects are then restored
from the base instead of the whole 3 MB buffer being copied. The first frame after any change — a
new lit area, a resolution change, a chunk that loaded or unloaded — still shows the new terrain in
that same frame: an area or scale change rebuilds the whole base, and an inconsistent base (size or
scale mismatch, never built) falls back to a full rebuild too.

Measured on this M1, same scene, before → after (before = the committed JSONs at `714a31f`, after =
the re-run at `25ef088` + the review fix): default resolution `light_cpu_ms_mean` 0.589 → **0.546**
and p99 1.002 → **0.914**; half resolution 0.655 → **0.585** and p99 0.755 → **0.690**.
The win is small on this workload for two measured reasons: the bench camera flies at 10 cells per
frame, so about 43% of the frames land on a new 32-cell-snapped lit area and have to rebuild the
base in full, and on most of the remaining frames the sim's faucets, explosions, bodies and
creatures already dirty most of the ~192 chunks in view, so the repaint covers close to the whole
area anyway. A frame whose dirty chunks cover half the area or more costs about the same either way.
The upload copy is gone from the steady state, which is where the p99 gain comes from.

This fallback did not clear the failing gate on its own: 20.85 ms default, 17.22 ms at half
resolution, both over the 16.6 ms budget. The conclusion drawn at the time, that `frame_ms_p99`
was GPU-bound and needed the GPU-side input pass, was an inference from Metal's missing timestamps
and turned out to be wrong; see the next section.

Plan 6 frame-gate fix (2026-10-26). Measured first, with a temporary per-frame trace of the main
loop phases (script + sim step, `PixelWorld._process`, draw, Godot's physics step) and a GPU
microbench of the real light shaders on a local `RenderingDevice` (submit + sync around 20
repeated dispatches per stage, since Metal stubs timestamps):

- The light pass is not GPU-bound. Its GPU time is about 1.3–1.8 ms at half resolution (496×384:
  0.9–1.4 ms compute from caves to open air, plus 0.3–0.5 ms input upload) and 5–7 ms at full
  resolution (992×768), with cascade 4 the costliest stage. It runs alongside a ~10 ms CPU frame,
  and the draw phase showed no waiting for the GPU. On the CPU it costs `light_cpu_ms` plus about
  0.08 ms to record and submit.
- The p99 frames were Godot physics steps right after a body spawn (bench_world drops 10 every
  10 frames). Each new `PixelRigidBody` joins on its first physics tick, and `build_def` called
  `Texture2D::get_image()`, which on an RD texture is a GPU readback that waits for every frame in
  flight: 0.45–0.5 ms each at half resolution, 1.26 ms mean (p90 3.2) at full, so 3–6 ms per spawn
  frame, longer when the light pass is queued. Every one of the 23 frames over 16.6 ms at half
  resolution was such a frame; taking that step out of the trace gave p99 14.17 ms and none over.

Fixes, each measured on its own at half resolution, frame mean / p99 in ms:

1. `PixelRigidBody` decodes each texture to RGBA8 once (keyed by the texture instance) and again
   only after the texture emits `changed`: 10.70 / 17.2 → 10.39 / 14.1, and full resolution
   11.25 / 21.0 → 10.58 / 14.4. The first body of each texture still pays one readback.
2. Chunk uploads: about 160 chunks are dirty per frame, and the float `cell_color` conversion was
   1.34 of the 1.6 ms (`texture_2d_update` only 0.18, so batching the RenderingServer calls into
   an atlas could save at most that). The conversion is now a precomputed RGBA8 table per
   (material, burning, shade), byte-identical to `cell_color` (checked over 304 659 chunk
   uploads), run over the pool: `upload_ms_mean` 1.62 → 0.48, frame 10.41 / 14.09 → 9.63 / 13.15.
3. Sim tail: the tick's wall time tracks its total cell work (correlation 1.00), parallel
   efficiency was already 0.91–0.96 and no single chunk serialised a phase. The tail ticks simply
   visit more cells (about 550k against a 345k mean, on two stretches of the camera path), at about
   46 ns of thread time per visited cell. Profiling showed `Neighborhood::cell`/`mark` and
   `MaterialTable::get`/`reaction` as separate, non-inlined calls in that loop (about 43% of busy
   samples). Moving them into their headers took sim from 4.45 / 7.44 to 3.70 / 6.2. Raising
   `JobPool::default_workers()` from `min(4, n - 1)` to `min(8, n - 1)` (the M1's efficiency cores
   still help: 4 → 7 workers was 3.70 → 3.12 mean) gives 3.12 / 5.08. The stream gates hold
   (0.08 / 0.33 ms, `view_missing` 0); `gen_ms_mean` rises from 0.83 to about 0.95 with more
   threads sharing the cores.

Result: 8.80 / 11.05 ms at default resolution and 8.68 / 11.08 ms at half, both `budget_pass:
true`, against 11.21 / 20.96 and 10.68 / 17.22 before. The GPU-side input pass is not needed.

The Linux Iris Xe numbers are still an open hand-off: `bench/results/linux-irisxe-plan1.json`
and `linux-irisxe-plan6.json` do not exist yet. Its 20 hardware threads and its GPU are the two
things to watch — the stream gates first, then whether its Vulkan driver reports
`light_gpu_ms` at all (it should, so the `> 0` clause applies there) and what `frame_ms_p99`
does with lighting on.
