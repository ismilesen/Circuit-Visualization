#!/usr/bin/env python
import os
import SCons.Errors

env = SConscript("godot-cpp/SConstruct")

# Add ngspice include path
env.Append(CPPPATH=["src/", "ngspice/include/"])

# Add ngspice library path
env.Append(LIBPATH=["ngspice/"])

# Note: We use dynamic loading (LoadLibrary) for ngspice on Windows
# so no static linking is needed

# Source files
sources = Glob("src/*.cpp")

# Build the shared library
library = env.SharedLibrary(
    "project/bin/libcircuit_sim{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)

def _verify_artifacts(target, source, env):
    required = [str(source[0])]

    # On macOS we also rely on these runtime dylibs being valid.
    if env.get("platform") == "macos":
        required.extend([
            "project/bin/libngspice.dylib",
            "ngspice/libngspice.dylib",
        ])

    bad = []
    for path in required:
        if (not os.path.exists(path)) or os.path.getsize(path) <= 0:
            bad.append(path)

    if bad:
        raise SCons.Errors.BuildError(
            errstr="Build artifact verification failed (missing/empty): {}".format(", ".join(bad))
        )

    stamp_path = str(target[0])
    with open(stamp_path, "w", encoding="utf-8") as f:
        f.write("ok\n")

verify = env.Command("project/bin/.build_verify_stamp", [library], _verify_artifacts)
AlwaysBuild(verify)

Default(verify)
