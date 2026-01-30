#!/usr/bin/env python
import os

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

Default(library)
