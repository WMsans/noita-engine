#!/usr/bin/env python
"""Build the noita_engine GDExtension and the host-side C++ test binary.

    scons platform=macos arch=arm64 target=template_debug   # extension -> bin/
    scons tests && build/tests/run_tests                     # core unit tests
"""

env = Environment()
customs = ["custom.py"]

SConscript(
    "thirdparty/godot-cpp/SConstruct",
    {"env": env, "customs": customs, "api_version": "4.7"},
)

# Box2D v3.2.0 (git submodule) is compiled into the extension and the test binary as C17.
# No SIMD defines: it picks SSE2 / NEON itself.
box2d_sources = Glob("thirdparty/box2d/src/*.c")

env.Append(CPPPATH=["src/", "thirdparty/", "thirdparty/box2d/include"])
env.Append(CFLAGS=["-std=c17"])
sources = Glob("src/core/*.cpp") + Glob("src/godot/*.cpp") + box2d_sources

# libnoita_engine.<platform>.<target>.<arch>.<ext> — godot-cpp's suffix without
# the .dev/.universal markers, keeping the arch.
suffix = env["suffix"].replace(".dev", "").replace(".universal", "")
library = env.SharedLibrary(
    "bin/libnoita_engine" + suffix + env["SHLIBSUFFIX"], source=sources
)
Default(library)

# Core unit tests: host toolchain only, no godot-cpp flags (src/core has no Godot dependency).
tests_env = Environment()
tests_env.Append(CPPPATH=["src", "src/core", "thirdparty", "thirdparty/box2d/include"])
tests_env.Append(CFLAGS=["-std=c17", "-O2", "-g", "-pthread"])
tests_env.Append(
    CXXFLAGS=["-O2", "-g", "-pthread", "-std=c++17"],
    LINKFLAGS=["-pthread"],
)
# scons tests tsan=yes — ThreadSanitizer for the test binary only; the extension build and
# its env are untouched, so a shipped library never carries the sanitizer runtime. -g above
# keeps the reports pointing at source lines.
if ARGUMENTS.get("tsan") == "yes":
    tests_env.Append(CXXFLAGS=["-fsanitize=thread"], LINKFLAGS=["-fsanitize=thread"])
tests = tests_env.Program(
    "build/tests/run_tests",
    source=Glob("src/core/*.cpp") + Glob("tests/core/*.cpp") + box2d_sources,
)
Alias("tests", tests)
