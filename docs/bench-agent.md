# Bench agent: build, test and bench this branch on macOS or Linux

Everything below runs from the repository root with `scons` and Godot 4.7.x on `PATH`. The
commands are the same on both platforms; `./build.sh` picks `macos`/`linux` and `arm64`/`x86_64`
from `uname` and passes them to `scons` explicitly. Copy-pasteable.

Checkpoints: `bench/results/macos-m1-plan1.json` through `macos-m1-plan5.json` are done on
the Apple M1 machine. The Linux Iris Xe machine owes `linux-irisxe-plan1.json` and
`linux-irisxe-plan5.json`. Each plan's `bench_world` run replaces the one before: same scene,
same camera path, the earlier workload plus the new one (Plan 3 explosions, Plan 4 bodies,
Plan 5 creatures), and the earlier gates plus the new ones.

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
[doctest] test cases:    147 |    147 passed | 0 failed | 0 skipped
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

Plan 5 (generated world: camera flies the surface/dive/cavern-run/return polyline at 10 cells
per frame while chunks stream in, sleep and get discarded; every 20 frames a radius-24 sand
disc is blown up with a burst of visual sparks; every 10 frames 10 crates and boulders and 8
wandering creatures drop above the camera, and anything more than 600 cells away is freed,
about 53 bodies and 43 creatures alive):

```bash
godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/macos-m1-plan5.json
godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/linux-irisxe-plan5.json
```

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

Plan 5 (Plan 4's committed line, without creatures, was sim 4.40 / 7.31, frame 9.70 / 14.48):

```
bench world: 1680 frames (+120 warm-up) | sim 4.40 ms mean / 7.38 p99 | frame 9.94 / 15.27 ms | stream 0.08 / 0.31 ms | gen 0.68 ms | chunks 6099 | cells 343296 | view-missing 0 frames | particles 298 / visual 43, 0.028 ms mean / 0.068 p99, dropped 615 | bodies 53 (awake 42), physics 0.16 + shapes 0.80 + stamp 0.42 = 1.38 ms mean / 2.58 p99, 3.8 builds | creatures 43, 0.097 ms mean / 0.179 p99 | budget pass | bench/results/...
```

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

Budgets (`bench.gd` and `bench_world.gd`; do not relax them to make a run pass):

| gate | Plan 1 | Plan 5 (`bench_world`) |
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

If `budget_pass` is `false`: re-run twice to rule out a cold or throttled machine, then record
the numbers and report back before touching the simulator or the workload.

## 5. Commit

```bash
git add bench/results/<machine>-plan1.json bench/results/<machine>-plan5.json
git commit -m "bench: <machine> results"
git push
```

Commit the result JSON files only — never edit a budget or the workload to turn a red run green.
The checkpoint is done when both files are committed with `budget_pass: true`. On the Linux
machine that is `linux-irisxe-plan1.json` **and** `linux-irisxe-plan5.json`; Plans 2 to 5 are
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
  `body_ms_mean` (say which of physics / shapes / stamp dominates), or a creature gate.
- **Environment:** `sw_vers`, `sysctl -n machdep.cpu.brand_string`, `sysctl -n hw.ncpu` on
  macOS; `lsb_release -a`, `lscpu` (or `grep 'model name' /proc/cpuinfo`), `nproc` on Linux.

## Status on this machine

Executed on 2026-10-25 on the Apple M1 machine (Apple M1, 8 cores, macOS 26.4.1, Godot 4.7.2).
The branch was already checked out here and is not pushed, so step 1's `git fetch` /
`git pull --ff-only` was **not** run — that part is for whoever runs this doc next.
`./build.sh --release --test --verify` passed: both libraries OK,
`147 | 147 passed`, `Status: SUCCESS!`,
`OK: ALL GODOT TESTS PASSED.`

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
`frame_ms_p99` 15.27 ms (budget 16.6: the tightest margin now), `creatures_mean` 43,
`creature_ms_mean` 0.097 ms (budget 1.0), p99 0.179 ms, `body_ms_mean` 1.38 ms,
`particles_dropped` 615 (creatures knocking sand out of piles add particles).

The Linux Iris Xe numbers are still an open hand-off: `bench/results/linux-irisxe-plan1.json`
and `linux-irisxe-plan5.json` do not exist yet. Its 20 hardware threads and its GPU are the two
things to watch — the stream gates first, then `sim_ms_mean`.
