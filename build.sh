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
#                             "Could not find type" parse errors are reported
#   ./build.sh --clean        remove build artifacts before building
#   ./build.sh -j N           override job count (default: nproc)
#
# Environment:
#   GODOT=/path/to/godot      Godot binary for --verify (default: godot on PATH)
#
# Exit code 0 on success, non-zero on any failure.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
		-j) i=$((i + 1)); JOBS="${args[$i]:-}"; if [ -z "$JOBS" ]; then echo "build.sh: -j needs a number" >&2; exit 2; fi ;;
		-j*) JOBS="${arg#-j}" ;;
		-h|--help)
			sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
if ! grep -q "^$PLATFORM\.debug\.$ARCH " "$ROOT/bin/noita_engine.gdextension"; then
	echo "build.sh: warning: bin/noita_engine.gdextension has no $PLATFORM.debug.$ARCH entry;" >&2
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

# --- submodule -------------------------------------------------------------
# godot-cpp is a git submodule; fetch it if it has not been checked out. Every
# scons invocation (including `scons tests`) reads its SConstruct.
if [ ! -f "$ROOT/thirdparty/godot-cpp/SConstruct" ] || [ ! -e "$ROOT/thirdparty/godot-cpp/.git" ]; then
	echo "==> Initializing godot-cpp submodule..."
	git -C "$ROOT" submodule update --init --recursive thirdparty/godot-cpp
fi

# --- build -----------------------------------------------------------------
cd "$ROOT"
SCONS_ARGS=(platform="$PLATFORM" arch="$ARCH")

if [ "$CLEAN" -eq 1 ]; then
	echo "==> Cleaning previous build artifacts..."
	scons -c -Q "${SCONS_ARGS[@]}" target=template_debug >/dev/null 2>&1 || true
	scons -c -Q "${SCONS_ARGS[@]}" target=template_release >/dev/null 2>&1 || true
	scons -c -Q "${SCONS_ARGS[@]}" tests >/dev/null 2>&1 || true
	# bin/ also holds the tracked .gdextension file: remove only the libraries.
	rm -rf build bin/libnoita_engine.*
fi

TARGETS=(template_debug)
[ "$BUILD_RELEASE" -eq 1 ] && TARGETS+=(template_release)

built=()
for target in "${TARGETS[@]}"; do
	echo "==> Building libnoita_engine ($PLATFORM $ARCH $target) with scons (-j$JOBS)..."
	scons -j"$JOBS" -Q "${SCONS_ARGS[@]}" target="$target"
	lib="$ROOT/bin/libnoita_engine.$PLATFORM.$target.$ARCH.$LIBEXT"
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
echo "    Registered native classes: PixelWorld, PixelMaterial, PixelMaterialSet, PixelReaction, PixelBiome, PixelRoom, PixelRoomPalette, PixelWorldGenerator, PixelRigidBody, PixelCharacter, PixelRevoluteJoint, PixelWheelJoint"
echo "    The editor always loads the template_debug library. Open the project in"
echo "    Godot (or restart the editor) — 'Could not find type PixelWorld' parse"
echo "    errors disappear once the library is loaded."

# --- core test suite -------------------------------------------------------
if [ "$RUN_TESTS" -eq 1 ]; then
	echo
	echo "==> Building and running core test suite..."
	# scons keys objects on their command line, so toggling tsan rebuilds the
	# test binary on its own. tsan is read by our SConstruct, not godot-cpp, so
	# godot-cpp warns it is an unknown variable; pass it only when asked for.
	if [ "$TSAN" -eq 1 ]; then
		scons -j"$JOBS" -Q "${SCONS_ARGS[@]}" tsan=yes tests
	else
		scons -j"$JOBS" -Q "${SCONS_ARGS[@]}" tests
	fi
	"$ROOT/build/tests/run_tests"
fi

# --- headless Godot tests --------------------------------------------------
if [ "$RUN_VERIFY" -eq 1 ]; then
	echo
	echo "==> Running Godot-side tests (headless)..."
	# Redirect XDG dirs into build/ (already gitignored): sandboxes and CI-like
	# environments may deny writes to ~/.local/share/godot, which makes Godot
	# crash on startup.
	XDG_ENV=(
		XDG_DATA_HOME="$ROOT/build/xdg/data"
		XDG_CACHE_HOME="$ROOT/build/xdg/cache"
		XDG_CONFIG_HOME="$ROOT/build/xdg/config"
	)
	mkdir -p "$ROOT/build/xdg/data" "$ROOT/build/xdg/cache" "$ROOT/build/xdg/config"
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
	if [ ! -d "$ROOT/.godot" ]; then
		echo "    No .godot/ directory yet — importing the project first..."
		env "${XDG_ENV[@]}" "${TIMEOUT[@]}" "$GODOT" --headless --path "$ROOT" --editor --quit-after 30 >/dev/null 2>&1 || true
	fi

	set +e
	out="$(env "${XDG_ENV[@]}" "${TIMEOUT[@]}" "$GODOT" --headless --path "$ROOT" -s tests/godot/run_tests.gd 2>&1)"
	status=$?
	set -e
	if printf '%s\n' "$out" | grep -E 'Could not find type|Cannot infer the type' >/dev/null; then
		printf '%s\n' "$out" | grep -E 'Could not find type|Cannot infer the type' >&2
		echo "build.sh: native types still unresolved — see errors above" >&2
		exit 1
	fi
	if [ $status -ne 0 ] || ! printf '%s\n' "$out" | grep -q 'ALL GODOT TESTS PASSED'; then
		echo "$out"
		echo "build.sh: Godot tests failed (godot exited with status $status)" >&2
		exit 1
	fi
	echo "    OK: ALL GODOT TESTS PASSED."
fi

echo
echo "==> Done."
