# noita_engine (Godot 4.7 GDExtension)

Pixel-world simulation: a native library (`PixelWorld`, `PixelMaterial`,
`PixelReaction`, `PixelRigidBody`, `PixelCharacter`, ...) plus GDScript demos and
example data.

## Use it in your project

Copy this folder to `res://addons/noita_engine/` (its `.gdextension` file refers to
`res://addons/noita_engine/bin/`), then build the native library inside the copy:

```sh
cd addons/noita_engine
./build.sh
```

Restart the editor (or open the project again) so Godot loads
`bin/libnoita_engine.*`. Build artifacts stay inside the addon (`bin/`, `build/`,
and `thirdparty/` when fetched), so the copied folder remains self-contained.

## Prerequisites

- `scons`
- a C++17 compiler (`clang++` or `g++`)
- `git` — only the first build needs it, to fetch godot-cpp and box2d

Noise is not a build dependency: FastNoiseLite (single MIT header, v1.1.1) is
vendored at `engine/FastNoiseLite/FastNoiseLite.h` and compiles in-tree.

## Building

`build.sh` resolves its two dependencies (godot-cpp and box2d) in this order:
`--thirdparty DIR`, `$NOITA_THIRDPARTY`, the development repo's `thirdparty/`, then
`thirdparty/` next to the script. A directory counts only if it contains
`godot-cpp/SConstruct` and `box2d/include/box2d/box2d.h`. When none does, both are
cloned into `addons/noita_engine/thirdparty/` at pinned commits — this is the
network-access case.

```sh
./build.sh                        # template_debug library (what the editor loads)
./build.sh --release              # also template_release
./build.sh --thirdparty ../deps   # use an existing godot-cpp + box2d checkout
./build.sh --clean                # remove build/ and bin/libnoita_engine.*
./build.sh -j 4                   # job count
./build.sh --help
```

`--test` (core C++ doctest suite) and `--verify` (headless Godot tests) only work in
the noita-engine development repo: `tests/` is not part of the addon, so a copied
addon prints a note and stops after the library build.

## Rules

Gameplay rules live in `rules/`; where to add a material or reaction is documented
in [`rules/README.md`](rules/README.md). Most rules are `.tres` data, not code.

## Example data

`demo/` holds example scenes, scripts and `.tres` material/worldgen data. Copy what
you need into your own project — do not reference `res://addons/noita_engine/demo/`
from shipped game code, as it is example data that may move or change.

## Global class names

The addon registers the global `class_name`s `PixelCharacter`, `CharacterSystem` and
`CarController`. Godot class names are global across a project, so if your project
already uses any of them the addon's script will not load. Rename yours (or the
addon's, plus its references) before copying the folder in.

The native classes above are registered by the GDExtension under their `Pixel*`
names and cannot be renamed.
