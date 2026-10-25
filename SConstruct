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

env.Append(CPPPATH=["src/"])
sources = Glob("src/core/*.cpp") + Glob("src/godot/*.cpp")

# libnoita_engine.<platform>.<target>.<arch>.<ext> — godot-cpp's suffix without
# the .dev/.universal markers, keeping the arch.
suffix = env["suffix"].replace(".dev", "").replace(".universal", "")
library = env.SharedLibrary(
    "bin/libnoita_engine" + suffix + env["SHLIBSUFFIX"], source=sources
)
Default(library)

# Core unit tests: host toolchain only, no godot-cpp flags (src/core has no Godot dependency).
tests_env = Environment()
tests_env.Append(CPPPATH=["src", "src/core", "thirdparty"])
tests_env.Append(
    CXXFLAGS=["-O2", "-g", "-pthread", "-std=c++17"],
    LINKFLAGS=["-pthread"],
)
tests = tests_env.Program(
    "build/tests/run_tests",
    source=Glob("src/core/*.cpp") + Glob("tests/core/*.cpp"),
)
Alias("tests", tests)
