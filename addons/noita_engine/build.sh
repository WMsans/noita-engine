#!/usr/bin/env bash
#
# build.sh — build the Noita Engine GDExtension native library.
#
# GDScript errors like
#     Could not find type "PixelWorld" in the current scope.
# mean Godot could not load bin/libnoita_engine.*, so the native classes
# PixelWorld / PixelMaterial / PixelMaterialSet / PixelReaction were never
# registered. This script produces that library.
#
# Usage:
#   ./build.sh                build the shared library (template_debug, the one
#                             the editor loads)
#   ./build.sh --release      also build the template_release library
#   ./build.sh --test         also build & run the core C++ test suite (doctest)
#   ./build.sh --tsan         build the core tests with ThreadSanitizer (implies --test)
#   ./build.sh --verify       also run the Godot-side tests headless
#                             (tests/godot/run_tests.gd) and check that no
#                             "Could not find type" parse errors or unexpected
#                             error output is reported
#   ./build.sh --clean        remove build artifacts before building
#   ./build.sh -j N           override job count (default: nproc)
#   ./build.sh --thirdparty DIR
#                             use DIR for godot-cpp/ and box2d/; when no candidate
#                             directory has both, they are cloned into
#                             thirdparty/ next to this script at pinned commits
#
# Environment:
#   GODOT=/path/to/godot      Godot binary for --verify (default: godot on PATH)
#   NOITA_THIRDPARTY=DIR      same as --thirdparty
#
# Exit code 0 on success, non-zero on any failure.

set -euo pipefail

ADDON="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# REPO is the development repo, recognised by its project file and both test
# suites; a host project whose git toplevel merely contains project.godot is not it.
REPO="$(git -C "$ADDON" rev-parse --show-toplevel 2>/dev/null || true)"
if [ -z "$REPO" ] || [ ! -f "$REPO/project.godot" ] || [ ! -d "$REPO/tests/core" ] || [ ! -d "$REPO/tests/godot" ]; then
	REPO=""
fi
GODOT="${GODOT:-godot}"
# Platform-aware job count: nproc on Linux, sysctl on macOS.
if command -v nproc >/dev/null 2>&1; then
	JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
	JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
else
	JOBS=4
fi
BUILD_RELEASE=0
RUN_TESTS=0
TSAN=0
RUN_VERIFY=0
CLEAN=0
THIRDPARTY_ARG=""

i=0
args=("$@")
while [ $i -lt ${#args[@]} ]; do
	arg="${args[$i]}"
	case "$arg" in
		--release) BUILD_RELEASE=1 ;;
		--test) RUN_TESTS=1 ;;
		--tsan) RUN_TESTS=1; TSAN=1 ;;
		--verify) RUN_VERIFY=1 ;;
		--clean) CLEAN=1 ;;
		--thirdparty)
			i=$((i + 1)); THIRDPARTY_ARG="${args[$i]:-}"
			if [ -z "$THIRDPARTY_ARG" ]; then echo "build.sh: --thirdparty needs a directory" >&2; exit 2; fi
			;;
		-j) i=$((i + 1)); JOBS="${args[$i]:-}"; if [ -z "$JOBS" ]; then echo "build.sh: -j needs a number" >&2; exit 2; fi ;;
		-j*) JOBS="${arg#-j}" ;;
		-h|--help)
			sed -n '2,31p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*) echo "build.sh: unknown argument: $arg (see --help)" >&2; exit 2 ;;
	esac
	i=$((i + 1))
done

# --- platform --------------------------------------------------------------
# Pass platform and arch explicitly: on macOS godot-cpp defaults to a universal
# build, and the SConstruct strips ".universal" from the suffix, which would
# leave a library name that bin/noita_engine.gdextension does not list.
case "$(uname -s)" in
	Darwin) PLATFORM=macos; LIBEXT=dylib ;;
	Linux)  PLATFORM=linux; LIBEXT=so ;;
	*) echo "build.sh: unsupported host OS: $(uname -s)" >&2; exit 1 ;;
esac
case "$(uname -m)" in
	x86_64|amd64)  ARCH=x86_64 ;;
	arm64|aarch64) ARCH=arm64 ;;
	*) echo "build.sh: unsupported host architecture: $(uname -m)" >&2; exit 1 ;;
esac
if ! grep -q "^$PLATFORM\.debug\.$ARCH " "$ADDON/noita_engine.gdextension"; then
	echo "build.sh: warning: noita_engine.gdextension has no $PLATFORM.debug.$ARCH entry;" >&2
	echo "          Godot will not load the library this host builds." >&2
fi

# --- prerequisites ---------------------------------------------------------
missing=()
for tool in git scons; do
	command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
# any C++ compiler (godot-cpp picks its own from the environment)
if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
	missing+=("a C++ compiler (g++ or clang++)")
fi
if [ "$RUN_VERIFY" -eq 1 ] && ! command -v "$GODOT" >/dev/null 2>&1; then
	missing+=("godot (needed by --verify; set GODOT=/path/to/godot)")
fi
if [ "${#missing[@]}" -gt 0 ]; then
	echo "build.sh: missing required tools: ${missing[*]}" >&2
	exit 1
fi

# --- thirdparty ------------------------------------------------------------
# Every scons invocation reads godot-cpp/SConstruct, and both the extension and
# the test binary compile box2d/src/*.c; those two are the git submodules, so
# they are what this resolves and fetches. FastNoiseLite is vendored in the addon
# (engine/FastNoiseLite), so nothing else has to be fetched.
thirdparty_ok() {
	[ -f "$1/godot-cpp/SConstruct" ] && [ -f "$1/box2d/include/box2d/box2d.h" ]
}

# In the development repo the dependencies are git submodules; init them so
# $REPO/thirdparty qualifies below. A host project's own submodules are left alone.
if [ -n "$REPO" ] && grep -qs 'thirdparty/godot-cpp' "$REPO/.gitmodules" && ! thirdparty_ok "$REPO/thirdparty"; then
	echo "==> Fetching git submodules (godot-cpp, box2d)..."
	git -C "$REPO" submodule update --init --recursive
fi

# Resolution order: --thirdparty, $NOITA_THIRDPARTY, the development repo's
# thirdparty/, then this addon's own thirdparty/.
THIRDPARTY=""
# Explicit choices first; a bad one is reported but does not stop the search.
for candidate in "$THIRDPARTY_ARG" "${NOITA_THIRDPARTY:-}"; do
	if [ -z "$candidate" ]; then continue; fi
	if thirdparty_ok "$candidate"; then
		THIRDPARTY="$candidate"
		break
	fi
	echo "build.sh: warning: $candidate has no godot-cpp/SConstruct and box2d/include/box2d/box2d.h" >&2
done
if [ -z "$THIRDPARTY" ] && [ -n "$REPO" ] && thirdparty_ok "$REPO/thirdparty"; then
	THIRDPARTY="$REPO/thirdparty"
fi
if [ -z "$THIRDPARTY" ] && thirdparty_ok "$ADDON/thirdparty"; then
	THIRDPARTY="$ADDON/thirdparty"
fi
if [ -z "$THIRDPARTY" ]; then
	THIRDPARTY="$ADDON/thirdparty"
fi

# Nothing usable: clone both at the pinned commits so a copied addon builds on
# its own. A checkout already in place is reused and only re-checked-out.
if ! thirdparty_ok "$THIRDPARTY"; then
	GODOT_CPP_SHA=507ed9d840c01a3c5b2a39af8bb4000bfac30bf5
	BOX2D_SHA=178495ba35695aefe9c321e569d2277f06db3b90
	fetch_repo() { # <dir> <url> <sha>
		if [ ! -d "$1/.git" ]; then
			echo "==> Cloning $2..."
			git clone "$2" "$1" || { echo "build.sh: git clone $2 failed (git and network access are required on the first build)" >&2; return 1; }
		fi
		git -C "$1" checkout "$3" || { echo "build.sh: git checkout $3 in $1 failed" >&2; return 1; }
	}
	mkdir -p "$THIRDPARTY"
	fetch_repo "$THIRDPARTY/godot-cpp" https://github.com/godotengine/godot-cpp.git "$GODOT_CPP_SHA"
	git -C "$THIRDPARTY/godot-cpp" submodule update --init --recursive
	fetch_repo "$THIRDPARTY/box2d" https://github.com/erincatto/box2d.git "$BOX2D_SHA"
fi
# scons runs with -C "$ADDON", so hand it an absolute path.
THIRDPARTY="$(cd "$THIRDPARTY" && pwd)"
echo "==> thirdparty: $THIRDPARTY"

# --- build -----------------------------------------------------------------
SCONS_ARGS=(platform="$PLATFORM" arch="$ARCH" thirdparty="$THIRDPARTY")

if [ "$CLEAN" -eq 1 ]; then
	echo "==> Cleaning previous build artifacts..."
	scons -C "$ADDON" -c -Q "${SCONS_ARGS[@]}" target=template_debug >/dev/null 2>&1 || true
	scons -C "$ADDON" -c -Q "${SCONS_ARGS[@]}" target=template_release >/dev/null 2>&1 || true
	scons -C "$ADDON" -c -Q "${SCONS_ARGS[@]}" tests >/dev/null 2>&1 || true
	# bin/ also holds the tracked .gdextension file: remove only the libraries.
	rm -rf "$ADDON/build" "$ADDON"/bin/libnoita_engine.*
fi

TARGETS=(template_debug)
[ "$BUILD_RELEASE" -eq 1 ] && TARGETS+=(template_release)

built=()
for target in "${TARGETS[@]}"; do
	echo "==> Building libnoita_engine ($PLATFORM $ARCH $target) with scons (-j$JOBS)..."
	scons -C "$ADDON" -j"$JOBS" -Q "${SCONS_ARGS[@]}" target="$target"
	lib="$ADDON/bin/libnoita_engine.$PLATFORM.$target.$ARCH.$LIBEXT"
	if [ ! -f "$lib" ]; then
		echo "build.sh: build finished but $lib was not produced" >&2
		exit 1
	fi
	built+=("$lib")
done

# --- report ----------------------------------------------------------------
echo
for lib in "${built[@]}"; do
	echo "==> Build OK: $(ls -lh "$lib" | awk '{print $5}') $(basename "$lib")"
done
echo "    Registered native classes: PixelWorld, PixelLight, PixelMaterial, PixelMaterialSet, PixelReaction, PixelBiome, PixelRoom, PixelRoomPalette, PixelWorldGenerator, PixelRigidBody, PixelCharacter, PixelRevoluteJoint, PixelWheelJoint"
echo "    The editor always loads the template_debug library. Open the project in"
echo "    Godot (or restart the editor) — 'Could not find type PixelWorld' parse"
echo "    errors disappear once the library is loaded."

# --- tests need the development repo ---------------------------------------
# --test and --verify live in the development repo, not in the addon, so a
# copied addon builds the library and stops there.
if [ "$RUN_TESTS" -eq 1 ] || [ "$RUN_VERIFY" -eq 1 ]; then
	if [ -z "$REPO" ]; then
		echo "--test/--verify need the noita-engine development repo (tests/ is not part of the addon); skipping"
		echo
		exit 0
	fi
fi

# --- core test suite -------------------------------------------------------
if [ "$RUN_TESTS" -eq 1 ]; then
	echo
	echo "==> Building and running core test suite..."
	# scons keys objects on their command line, so toggling tsan rebuilds the
	# test binary on its own. tsan is read by our SConstruct, not godot-cpp, so
	# godot-cpp warns it is an unknown variable; pass it only when asked for.
	if [ "$TSAN" -eq 1 ]; then
		scons -C "$ADDON" -j"$JOBS" -Q "${SCONS_ARGS[@]}" tsan=yes tests
	else
		scons -C "$ADDON" -j"$JOBS" -Q "${SCONS_ARGS[@]}" tests
	fi
	"$ADDON/build/tests/run_tests"
fi

# --- headless Godot tests --------------------------------------------------
if [ "$RUN_VERIFY" -eq 1 ]; then
	echo
	echo "==> Running Godot-side tests (headless)..."
	# Redirect XDG dirs into build/ (already gitignored): sandboxes and CI-like
	# environments may deny writes to ~/.local/share/godot, which makes Godot
	# crash on startup.
	XDG_ENV=(
		XDG_DATA_HOME="$ADDON/build/xdg/data"
		XDG_CACHE_HOME="$ADDON/build/xdg/cache"
		XDG_CONFIG_HOME="$ADDON/build/xdg/config"
	)
	mkdir -p "$ADDON/build/xdg/data" "$ADDON/build/xdg/cache" "$ADDON/build/xdg/config"
	# GNU coreutils' timeout doesn't exist on stock macOS; degrade gracefully
	# (`env` with no time limit in front is a no-op wrapper).
	TIMEOUT=(env)
	if command -v timeout >/dev/null 2>&1; then
		TIMEOUT=(timeout 240)
	fi

	# On a cold checkout (no .godot/ yet) import the project first, or the
	# test script cannot load resources. A cold import can exit 134 from an
	# upstream Godot bug even after importing successfully, so its status is
	# ignored; the test run below is what decides.
	if [ ! -d "$REPO/.godot" ]; then
		echo "    No .godot/ directory yet — importing the project first..."
		env "${XDG_ENV[@]}" "${TIMEOUT[@]}" "$GODOT" --headless --path "$REPO" --editor --quit-after 30 >/dev/null 2>&1 || true
	fi

	set +e
	out="$(env "${XDG_ENV[@]}" "${TIMEOUT[@]}" "$GODOT" --headless --path "$REPO" -s tests/godot/run_tests.gd 2>&1)"
	status=$?
	set -e
	if printf '%s\n' "$out" | grep -E 'Could not find type|Cannot infer the type' >/dev/null; then
		printf '%s\n' "$out" | grep -E 'Could not find type|Cannot infer the type' >&2
		echo "build.sh: native types still unresolved — see errors above" >&2
		exit 1
	fi
	# A godot exit 139 (segfault) right after the PASSED line is a known exit-time crash, not a
	# test failure; any other non-zero status is.
	if { [ $status -ne 0 ] && [ $status -ne 139 ]; } || ! printf '%s\n' "$out" | grep -q 'ALL GODOT TESTS PASSED'; then
		echo "$out"
		echo "build.sh: Godot tests failed (godot exited with status $status)" >&2
		exit 1
	fi
	# The API-rejection tests below intentionally push_error; every other error line
	# (in particular one from a --headless code path that should be a silent no-op)
	# must fail the run. Keep this list in sync with run_tests.gd: the tests that
	# provoke errors are test_unknown_material, test_material_write_rejects_unknown,
	# test_particle_api_rejects_bad_input/_unbounded_input, test_body_bad_definitions,
	# test_batched_query_rejects_bad_input, and test_bodies_stepped_handler_may_free, which
	# frees the world from a bodies_stepped handler through its parent (a signal handler
	# cannot free the emitter itself). Godot names the object only as <Object#id>, so that
	# entry cannot name PixelWorld; it is pinned to the exact message and the count below
	# requires it exactly once, so a free-during-emission bug in any other test or class
	# still fails the run.
	allowed_errors='^ERROR: PixelWorld: unknown material .|^ERROR: PixelWorld\.(sweep_boxes|probe_boxes|scan_boxes|displace_cells): .*|^ERROR: PixelWorld\.explode: radius 1000 is over the limit of 256$|^ERROR: PixelWorld\.spawn(_visual)?_particles: position is not finite$|^ERROR: Object '\''<Object#[0-9]+>'\'' was freed or unreferenced while a signal is being emitted from it\.|^ERROR: PixelRigidBody .*: (has no texture|texture has no opaque pixels|pixel .* colour .* is not a material in the palette|pixel_material .* is not a material)$|^ERROR: PixelMaterial .*: (crust )?texture must be 16x16'
	unexpected="$(printf '%s\n' "$out" | grep -E '(^| )ERROR' | grep -vE "$allowed_errors" || true)"
	if [ -n "$unexpected" ]; then
		printf '%s\n' "$unexpected" >&2
		echo "build.sh: unexpected error output from the Godot tests — see errors above" >&2
		exit 1
	fi
	emit_frees="$(printf '%s\n' "$out" | grep -cE '^ERROR: Object '\''<Object#[0-9]+>'\'' was freed or unreferenced while a signal is being emitted from it\.' || true)"
	if [ "$emit_frees" -ne 1 ]; then
		printf '%s\n' "$out" | grep -E 'was freed or unreferenced while a signal' >&2
		echo "build.sh: expected exactly one free-during-emission error, got $emit_frees" >&2
		exit 1
	fi
	echo "    OK: ALL GODOT TESTS PASSED."
fi

echo
echo "==> Done."
