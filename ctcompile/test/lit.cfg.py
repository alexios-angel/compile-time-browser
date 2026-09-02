# The ctcompile lit configuration.
#
# WHAT THE PLAN SPECIFIES is the middle block: config.name, config.suffixes,
# config.test_format and the tool substitutions. The preamble and the two roots
# are standard LLVM out-of-tree convention, which the plan does not spell -
# noted here rather than presented as though it did.
import os

import lit.formats
import lit.llvm

lit.llvm.initialize(lit_config, config)
from lit.llvm import llvm_config  # noqa: E402  (must follow initialize)

config.name = "ctcompile"
config.suffixes = [".mlir", ".td", ".test"]
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.ctcompile_obj_root, "test")

# FileCheck, not, count - everything a RUN line reaches for that is not ours.
llvm_config.use_default_substitutions()

# A LIST OF DIRECTORIES, not one. The plan's snippet passes a single
# `config.ctcompile_tools_dir`, which assumes every tool lands in a shared bin/
# - and these do not: each is built where its own CMakeLists puts it, and moving
# them would change paths that documentation and other tests already name.
# add_tool_substitutions accepts a search list, so nothing has to move.
tools = ["ctjs-opt", "ctjs-translate", "ctcompile"]
llvm_config.add_tool_substitutions(tools, config.ctcompile_tools_dirs)

# mlir-translate IS LLVM'S, not ours, so it is looked for where LLVM's tools
# are rather than in the three directories ctcompile builds into. It is what
# turns an EmitC module into C++, which makes it part of the backend under test
# rather than a convenience.
# `not` COMES FROM THE SAME PLACE and is not optional: use_default_substitutions
# registers it only if it is already on PATH, and it is not here - so a RUN line
# using it failed with "not: command not found" and FileCheck then reported the
# missing string, which reads like a broken assertion rather than a missing
# tool.
# split-file IS LLVM'S TOO. A lit test that starts from JavaScript is one
# program per file - ctjs-translate reads the whole file as source - so a test
# holding a positive program and three negative ones needs to be four files;
# split-file cuts one file at its `//--- name` markers and drops the preamble
# where the RUN and CHECK lines live.
llvm_config.add_tool_substitutions(["mlir-translate", "not", "split-file"],
                                   [config.llvm_tools_dir])

# %cxx COMPILES THE EMITTED TRANSLATION UNIT AGAINST THE REAL ABI HEADERS.
#
# -fsyntax-only because the question is whether the C++ the backend emitted
# agrees with aot.hpp - the argument types, the qualified names, the status
# vocabulary - not whether it links. A backend that emits plausible C++ against
# a signature it invented is exactly the failure this project keeps meeting,
# and the host compiler catches it for nothing.
# %cxx_exe BUILDS AND LINKS, for the tests that have to RUN the emitted code.
#
# Syntax alone cannot settle a miscompile. The C++ emitter loses a copy on a
# block-argument edge - it produces code that compiles perfectly and returns the
# wrong number - so the test for the pass that fixes it compiles the output,
# runs it, and lets the program's own exit status be the assertion.
#
# IT IS REGISTERED BEFORE %cxx AND THAT ORDER IS LOAD-BEARING. lit walks this
# list and replaces the first pattern that matches, and "%cxx" matches the start
# of "%cxx_exe" - registered the other way round, `%cxx_exe file.cpp` expands to
# `<compiler> -fsyntax-only ... _exe file.cpp`, which fails with a message about
# a file called "_exe" rather than about the order of this list.
config.substitutions.append(
    ("%cxx_exe", f"{config.host_cxx} -std=c++23 -I {config.ctbrowser_include}"))

config.substitutions.append(
    ("%cxx",
     f"{config.host_cxx} -std=c++23 -fsyntax-only -I {config.ctbrowser_include}"))
