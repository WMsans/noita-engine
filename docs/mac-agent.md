# Mac agent: build, test and bench this branch

Everything below runs from the repository root on the Mac (Apple silicon, macOS arm64) with
`scons` and Godot 4.7.x on `PATH`. Commands are copy-pasteable.

## 1. Get the branch

```bash
git fetch origin
git checkout plan1-sim-core
git pull --ff-only
git submodule update --init --recursive
```

## 2. Build the extension

```bash
scons platform=macos arch=arm64 target=template_debug
scons platform=macos arch=arm64 target=template_release
```

Both write `bin/libnoita_engine.macos.*.arm64.dylib`. Note: the Godot **editor** binary always
loads the `template_debug` library (Godot resolves `macos.debug.arm64` first; it does not fall
back to the release library). Both builds carry the same simulator, and on this machine they
measure within noise of each other (see the status note), so run the benchmark with whatever
the editor loads — do not delete the debug dylib to try to force the release one, the extension
just fails to load.

## 3. Core unit tests

```bash
scons tests && build/tests/run_tests
```

Expected tail: `test cases: 40 | 40 passed | 0 failed`, then `Status: SUCCESS!`.

## 4. Godot-side tests

```bash
godot --headless --path . -s tests/godot/run_tests.gd
```

Expected: `ALL GODOT TESTS PASSED`. On a cold checkout (no `.godot/` directory yet) run
`godot --headless --path . --editor --quit-after 30` first so the project imports; a cold
`--import` can exit 134 from an upstream Godot bug even after importing successfully.

## 5. Benchmark

Needs a real window — do **not** pass `--headless`, the point is frame time with rendering.

```bash
godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/macos-m1-plan1.json
```

The scene paints a fixed layout (water lake with an oil layer, three sand piles under
re-painted faucets, lava against water, a burning wood lattice, an acid pocket on rock), pans
the camera across the 1024×512 arena and back, ticks the simulator once per frame with vsync
off, discards the first 120 frames as warm-up and writes the JSON. Defaults: `--frames=1800`,
`--out=user://bench.json`; a relative `--out` is resolved against the current directory and
missing parent directories are created.

Expected: exit code 0, the JSON file exists and `budget_pass` is `true` (budget:
`sim_ms_mean <= 6.0`). One summary line is printed, e.g.

```
bench: 1680 frames (+120 warm-up) | sim 3.53 ms mean / 5.09 p99 | frame 8.35 / 10.53 ms | ...
```

`frames` in the JSON is the number of sampled frames, i.e. `--frames` minus the 120 warm-up
frames. `frame`/`sim`/`upload` `*_ms_mean` and `*_p99` are over those samples; p99 is the
nearest-rank 99th percentile, not the maximum. `worker_threads` is the node's configured value
and `-1` means auto (`JobPool::default_workers()`: `min(4, hardware_concurrency - 1)`, so 4 on
this machine).

If `budget_pass` is `false`: re-run twice to rule out a cold machine, then record the numbers
and report back before touching the simulator.

## 6. Commit and push

```bash
git add bench/results/macos-m1-plan1.json
git commit -m "bench: macOS M1 benchmark results"
git push
```

The result file is the point of the checkpoint: Plan 1 is done when
`bench/results/macos-m1-plan1.json` is committed with `budget_pass: true`.

## If anything fails, report back

- **Build fails:** the last ~50 lines of the `scons` output, plus `scons --version` and
  `xcode-select -p` / `clang --version`.
- **Tests fail:** the full output of `build/tests/run_tests` (or
  `godot --headless --path . -s tests/godot/run_tests.gd`), including which cases failed.
- **Bench fails or misses the budget:** the JSON file, the printed `bench:` summary line, and
  which library was actually loaded:
  `DYLD_PRINT_LIBRARIES=1 godot --path . res://bench/bench.tscn -- --frames=1800 ... 2>&1 | grep noita`.
- **Environment:** `sw_vers`, `sysctl -n machdep.cpu.brand_string`, and `sysctl -n hw.ncpu`.

## Status on this machine

Executed on 2026-10-25 on this machine (Apple M1, 8 cores, macOS 26.4.1, Godot 4.7.2), steps
1–5 all passed:

- `scons platform=macos arch=arm64 target=template_debug` and `target=template_release`: OK.
- `scons tests && build/tests/run_tests`: `40 | 40 passed`, `Status: SUCCESS!`.
- `godot --headless --path . -s tests/godot/run_tests.gd`: `ALL GODOT TESTS PASSED`.
- `godot --path . res://bench/bench.tscn -- --frames=1800 --out=bench/results/macos-m1-plan1.json`:
  `budget_pass: true`, `sim_ms_mean` 3.53 ms (budget 6.0), `sim_ms_p99` 5.09 ms,
  `frame_ms_mean` 8.35 ms, `sim_ms_mean` over three runs 3.48–3.55 ms. The release library
  measured 3.48–3.49 ms over two runs, so the debug/release difference is inside the run-to-run
  spread.
