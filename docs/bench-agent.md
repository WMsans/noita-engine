# Bench agent: build, test and bench this branch on macOS or Linux

Everything below runs from the repository root with `scons` and Godot 4.7.x on `PATH`. The
commands are the same on both platforms; `./build.sh` picks `macos`/`linux` and `arm64`/`x86_64`
from `uname` and passes them to `scons` explicitly. Copy-pasteable.

**This doc holds one result per machine: the newest one.** When you bench, replace the numbers in
*Latest results* with your run's and delete the ones they replaced — do not append a second set or
leave an older run beside the new one. Older result JSONs stay in `bench/results/`; this text
always describes the code as it is now.

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
[doctest] test cases:    214 |    214 passed | 0 failed | 0 skipped
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

Arena (`bench/bench.tscn`): fixed painted layout, camera pans across 1024×512 and back. Cheap
simulator check, no streaming, no GPU work beyond the chunk draw.

```bash
godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/<machine>.json
```

World (`bench/bench_world.tscn`): the bench that matters — generated world, camera flies the
surface/dive/cavern-run/return polyline at 10 cells per frame while chunks stream in, sleep and
get discarded; every 20 frames a radius-24 sand disc is blown up with a burst of visual sparks;
every 10 frames 10 crates and boulders and 8 wandering characters drop above the camera, and
anything more than 600 cells away is freed; every 60 frames a 40×6 lava pool is painted at camera
offset (−200, 60); every 30 frames oil and fire; and a `PixelLight` of radius 48 with flicker 0.3
is a child of the camera. About 96 bodies (43 of them characters, which are rigid bodies too) and
43 characters are alive per frame, and the frame builds its light inputs, spots and GPU cascades
every frame from the first one.

```bash
godot --path . res://bench/bench_world.tscn -- --frames=1800 --out=bench/results/<machine>.json
godot --path . res://bench/bench_world.tscn -- --frames=1800 --light-resolution=2 --out=bench/results/<machine>-halfres.json
```

`--light-resolution=N` runs the light pass at 1/N linear resolution: `PixelWorld.light_resolution`
is "world cells per light texel", any value in 1..4, where 1 (the default) is full resolution, 2 is
half and 4 is quarter. It trades light detail and shadow sharpness for cost — the input is `1/N²`
the texels, the cascade and bloom textures shrink with it, and probes are spaced in texels so the
cascade reach in world cells grows with N. The committed default stays 1 (unchanged look and cost);
a value outside 1..4 is refused rather than clamped, so a run always measures what it says in its
JSON.

**The world bench needs a real GPU**: lighting is the point of the run. Do not pass `--headless`;
there is no `RenderingDevice` then, lighting is a silent no-op with zeroed stats, and the
`light_spots_mean ≥ 1` gate is what catches that. Leave the window size alone (resizing changes
the lit area, which is the view plus `light_margin`).

Defaults: `--frames=1800`; `--out=user://bench.json` (arena) and `--out=user://bench_world.json`
(world). A relative `--out` is resolved against the current directory and missing parent
directories are created. `--frames` at or below 120 is refused without writing a file.

Expected: exit code 0, the JSON file exists, `budget_pass` is `true`, and one summary line is
printed.

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
`characters_mean` is `PixelCharacter`s alive per frame and `character_ms_*` the GDScript
`CharacterSystem` tick (`last_tick_usec`), which builds the grid queries from the characters and
runs the behaviour in GDScript. It is **report-only**: `character_budget_ms` records the 1.0 ms
figure the old C++ creature pass was gated on, but nothing in `budget_pass` compares against it.
`light_cpu_ms_*` is the light-input build for one frame — grid walk into the RGBA8 texels (the
whole area when the lit area changed, otherwise only the chunks that changed), marker and spot
collection, spot discs, and the copy of those texels into the render job's upload buffer — against
a 1 ms CPU budget (the copy is inside the timer, not a separate untimed step);
`light_gpu_ms_*` is the light + bloom compute pass from RenderingDevice timestamps;
`light_spots_mean` is how many lights the frame used (the camera torch plus room markers);
`light_gpu_ms_supported` says whether the driver reported any GPU time at all, and
`light_gpu_ms_stubbed` says the run was allowed to report none because it is the known Metal stub
(both are recorded so the exemption is auditable). `light_resolution` is the setting the run used
(1..4, i.e. 1 full, 2 half, 4 quarter), read back from the node so it cannot disagree with what ran.

Budgets (`bench.gd` and `bench_world.gd`; do not relax them to make a run pass):

| gate | arena | world |
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
| `characters_mean` | — | ≥ 30 |
| `light_cpu_ms_mean` | — | ≤ 1.0 |
| `light_gpu_ms_mean` | — | ≤ 3.0 (light + bloom) |
| `light_gpu_ms_mean > 0` | — | lighting really ran (see the Metal note) |
| `light_spots_mean` | — | ≥ 1 |

`character_ms_mean` / `character_ms_p99` are **not** gates: `character_budget_ms` (1.0) is
written to the JSON and printed next to the timings so the cost of the move from the C++ creature
pass to GDScript stays visible, but nothing reads it into `budget_pass`.

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

## 5. Latest results

Apple M1 (8 cores, macOS 26.4.1), Godot 4.7.2 — `bench/results/macos-m1-textures.json`:

```
bench world: 1680 frames (+120 warm-up) | sim 2.07 ms mean / 4.30 p99 | frame 8.38 / 10.08 ms | stream 0.11 / 0.40 ms | gen 3.35 ms | chunks 6098 | cells 92816 | view-missing 0 frames | particles 554 / visual 67, 0.043 ms mean / 0.089 p99, dropped 1922 | bodies 54 (awake 41), physics 0.10 + shapes 0.29 + stamp 0.48 = 0.87 ms mean / 1.46 p99, 3.6 builds | creatures 43, 0.160 ms mean / 0.237 p99 | light 0.547 ms cpu mean / 1.061 p99 | light 0.000 ms gpu mean / 0.000 p99 (timestamps stubbed), spots 1.9 | budget pass
```

That run predates the GDScript character pass: its `creatures 43, 0.160 ms mean / 0.237 p99` is
the C++ creature grid pass, and it is the baseline the report-only `character_ms_*` is read
against (the same machine reports 0.80 ms mean / 1.43 p99 at 43 characters once the behaviour is
GDScript).

`light_resolution` 1, `light_gpu_ms_supported: false`, `light_gpu_ms_stubbed: true`,
`cells_updated_mean` 92816. A second run agreed within noise: frame 8.38 / 10.18 ms, gen 3.34 ms,
sim 2.08 / 4.28 ms. The material textures cost generation time (`gen_ms_mean` 3.35 ms against
1.81 ms before them, the padded 84×84 region plus the crust pass) and nothing measurable in the
frame: frame p99 10.08 against 10.18 ms before them.

Arena (`bench/results/macos-m1-plan1.json`, the only arena run so far): 1680 frames, sim 3.53 ms
mean / 5.09 p99, frame 8.35 / 10.53 ms, upload 1.02 ms, chunks 53.0, cells 108384,
`budget_pass: true`.

Linux: no result yet — `bench/results/linux-irisxe.json` is still owed. Its 20 hardware threads
and its GPU are the two things to watch: the stream gates first, then whether its Vulkan driver
reports `light_gpu_ms` at all (it should, so the `> 0` clause applies there) and what
`frame_ms_p99` does with lighting on.

## 6. Commit

```bash
git add bench/results/<machine>.json
git commit -m "bench: <machine> results"
git push
```

Commit the result JSON files only — never edit a budget or the workload to turn a red run green.
The checkpoint is done when the file is committed with `budget_pass: true`.

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
  `body_ms_mean` (say which of physics / shapes / stamp dominates), or a
  light gate (`light_cpu_ms_mean`: profile `PixelWorld::build_light_frame` — the dirty-chunk scan
  and `build_light_rects`, the spot discs and the restore of the last frame's spot rects;
  `frame_ms_p99` with `lighting_enabled = false` says whether the light pass is what pushed it
  over; `light_spots_mean` under 1 means lighting never ran — keep the `--headless` out of it).
- **Environment:** `sw_vers`, `sysctl -n machdep.cpu.brand_string`, `sysctl -n hw.ncpu` on
  macOS; `lsb_release -a`, `lscpu` (or `grep 'model name' /proc/cpuinfo`), `nproc` on Linux.
