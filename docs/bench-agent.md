# Bench agent: build, test and bench this branch on macOS or Linux

Everything below runs from the repository root with `scons` and Godot 4.7.x on `PATH`. The
commands are the same on both platforms; `./build.sh` picks `macos`/`linux` and `arm64`/`x86_64`
from `uname` and passes them to `scons` explicitly. Copy-pasteable.

Checkpoints: `bench/results/macos-m1-plan1.json` through `macos-m1-plan6.json` are done on
the Apple M1 machine (Plan 6 is red on the frame gate; see the status section). The Linux Iris Xe
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
godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/linux-irisxe-plan6.json
```

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

Plan 6 on this M1 (the committed `bench/results/macos-m1-plan6.json`; **red** on the frame gate,
see the status section — do not copy it as a target):

```
bench world: 1680 frames (+120 warm-up) | sim 4.46 ms mean / 7.44 p99 | frame 11.25 / 20.89 ms | stream 0.08 / 0.39 ms | gen 0.66 ms | chunks 6099 | cells 345357 | view-missing 0 frames | particles 312 / visual 43, 0.033 ms mean / 0.075 p99, dropped 1883 | bodies 53 (awake 42), physics 0.15 + shapes 0.82 + stamp 0.41 = 1.38 ms mean / 2.60 p99, 3.8 builds | creatures 43, 0.159 ms mean / 0.230 p99 | light 0.62 ms cpu mean / 1.00 p99 | light 0.000 ms gpu mean / 0.000 p99 (timestamps stubbed), spots 5.3 | budget FAIL | bench/results/...
```

For reference, the same scene with `lighting_enabled = false` reproduces Plan 5's committed run
(frame 9.98 / 15.01), so the lighting pass is the whole difference: about +1.3 ms of frame time on
average and +6 ms at p99 on this machine.

`frames` in the JSON is the number of sampled frames, i.e. `--frames` minus the 120 warm-up
frames. `frame`/`sim`/`upload`/`stream` `*_ms_mean` and `*_p99` are over those samples; p99 is
the nearest-rank 99th percentile, not the maximum. `gen_ms_mean` is the mean over the frames
that generated anything, `chunks_generated` is cumulative, `chunks_simulated_mean` is how many
chunks were awake per frame, and `view_missing_frames` counts frames with a hole in the view.
`worker_threads` is the node's configured value; `-1` means auto (`JobPool::default_workers()`:
`min(4, hardware_concurrency - 1)`). `particle_ms_*` is the particle step per tick,
`particles_material_mean` / `particles_visual_mean` the particles in flight per frame, and
`particles_dropped` the cumulative material particles that found nowhere to land.
`bodies_mean` / `bodies_awake_mean` are rigid bodies alive / awake per frame;
`physics_ms_*` (Box2D step), `shape_ms_*` (terrain shape builds), `stamp_ms_*` (stamp +
unstamp) and their sum `body_ms_*`; `shape_builds_mean` is chunk shape rebuilds per tick.
`creatures_mean` is creatures alive per frame and `creature_ms_*` the creature grid pass.
`light_cpu_ms_*` is the light-input build for one frame — grid walk into the RGBA8 texels, marker
and spot collection, spot discs, and the copy of those texels into the render job's upload buffer
— the plan's 1 ms CPU budget (the copy is inside the timer, not a separate untimed step);
`light_gpu_ms_*` is the light + bloom compute pass from RenderingDevice timestamps;
`light_spots_mean` is how many lights the frame used (the camera torch plus room markers);
`light_gpu_ms_supported` says whether the driver reported any GPU time at all, and
`light_gpu_ms_stubbed` says the run was allowed to report none because it is the known Metal stub
(both are recorded so the exemption is auditable).

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
  light gate (`light_cpu_ms_mean`: profile `build_light_inputs`, the spot discs and the texel
  copy into the upload buffer; `frame_ms_p99`
  with `lighting_enabled = false` says whether the light pass is what pushed it over;
  `light_spots_mean` under 1 means lighting never ran — keep the `--headless` out of it).
- **Environment:** `sw_vers`, `sysctl -n machdep.cpu.brand_string`, `sysctl -n hw.ncpu` on
  macOS; `lsb_release -a`, `lscpu` (or `grep 'model name' /proc/cpuinfo`), `nproc` on Linux.

## Status on this machine

Executed on 2026-10-25 on the Apple M1 machine (Apple M1, 8 cores, macOS 26.4.1, Godot 4.7.2).
The branch was already checked out here and is not pushed, so step 1's `git fetch` /
`git pull --ff-only` was **not** run — that part is for whoever runs this doc next.
`./build.sh --release --test --verify` passed: both libraries OK,
`161 | 161 passed`, `Status: SUCCESS!`,
`OK: ALL GODOT TESTS PASSED.` (Plan 6 re-ran it; Plans 1–5 saw 152 core cases.)

Plan 1 — `bench/results/macos-m1-plan1.json`: `budget_pass: true`, `sim_ms_mean` 3.53 ms
(budget 6.0), `sim_ms_p99` 5.09 ms, `frame_ms_mean` 8.35 ms, `upload_ms_mean` 1.02 ms,
`cells_updated_mean` 108384. Three runs spread 3.48–3.55 ms, so this is inside run-to-run noise.

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

Plan 6 — `bench/results/macos-m1-plan6.json`: **`budget_pass: false`** on this M1, and it is the
one guardrail this plan did not clear. Every gate passes except `frame_ms_p99`: 20.89 ms against
the 16.6 ms budget (three runs: 20.75, 21.16, 20.89). The lighting gates themselves pass —
`light_cpu_ms_mean` 0.62 ms (budget 1.0, p99 1.00), `light_spots_mean` 5.3 (floor 1), and
`light_gpu_ms_supported: false` with `light_gpu_ms_stubbed: true` because Metal cannot report GPU
time, so the only GPU signal is the frame time. `sim_ms_mean` 4.46, `stream_ms_mean` 0.08,
`frame_ms_mean` 11.25, `cells_updated_mean` 345357, `view_missing_frames` 0, `body_ms_mean` 1.38,
`creature_ms_mean` 0.159.

The light pass is the whole difference: the same scene with `lighting_enabled = false` reproduces
Plan 5 (frame 9.98 / 15.01), and the pass alone — bench workload, torch and lava removed — gives
frame 11.22 / 20.95. It scales with the lit area, which is the view plus `light_margin` 240
(992×768 at the default window): `light_margin = 0` (480×256) gives 10.31 / 16.61, `light_margin =
120` gives 10.68 / 18.25. So the pass costs about 1.3 ms of frame time on average (inside the
plan's 3 ms light + bloom GPU budget) but adds ~6 ms at p99, and Plan 5's baseline was already at
15.1 ms of the 16.6 ms budget. Plan 5's numbers were measured before the light pass existed, so
this is new GPU work, not a regression in the simulator.

The plan's fallbacks for a GPU budget miss (half resolution, per-dirty-chunk inputs, then a
GPU-side input pass) each need the user's go-ahead and none is implemented. Note that the first
two fallbacks cut the CPU input cost (already 0.62 ms of the 1 ms budget, upload copy included)
and half resolution is the one that would touch this failure: at a quarter of the texels
(`light_margin = 0`) the p99 lands at 16.6 ms, right on the budget.

The Linux Iris Xe numbers are still an open hand-off: `bench/results/linux-irisxe-plan1.json`
and `linux-irisxe-plan6.json` do not exist yet. Its 20 hardware threads and its GPU are the two
things to watch — the stream gates first, then whether its Vulkan driver reports
`light_gpu_ms` at all (it should, so the `> 0` clause applies there) and what `frame_ms_p99`
does with lighting on.
