# The ctcompile lit configuration.
#
# WHAT THE PLAN SPECIFIES is the middle block: config.name, config.suffixes,
# config.test_format and the tool substitutions. The preamble and the two roots
# are standard LLVM out-of-tree convention, which the plan does not spell -
# noted here rather than presented as though it did.
import os
import shutil

import lit.formats
import lit.llvm

lit.llvm.initialize(lit_config, config)
from lit.llvm import llvm_config  # noqa: E402  (must follow initialize)
from lit.llvm.subst import ToolSubst  # noqa: E402

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

# THE TEST EXECUTABLES THE FORMER `cmake -P` CHECKS DROVE, by their target
# names, from the directory CMake builds them into (this suite's exec root);
# and ctbrowser's launcher and browser, which the packaging round trip runs.
# unresolved="ignore", because add_tool_substitutions drops the WHOLE group
# when one name is missing: a test executable that was not built should fail
# its own RUN line by name, not turn eleven others into "command not found".
llvm_config.add_tool_substitutions(
    [
        ToolSubst(name, unresolved="ignore")
        for name in [
            "ctcompile-test-type-oracle",
            "ctcompile-test-type-claims",
            "ctcompile-test-escape-claims",
            "ctcompile-test-escape-oracle-aot",
            "ctcompile-test-escape-oracle-aot-return",
            "ctcompile-test-launcher-vm",
            "ctcompile-test-launcher-aot",
            "ctcompile-test-launcher-page-vm",
            "ctcompile-test-launcher-page-aot",
            "ctcompile-test-native-vm-linked",
            "ctcompile-test-native-values-vm-linked",
            "ctcompile-test-app_bundle",
        ]
    ],
    [config.test_exec_root],
)
llvm_config.add_tool_substitutions(
    [ToolSubst(name, unresolved="ignore") for name in ["ctrun", "ctbrowse"]],
    [config.ctbrowser_tools_dir],
)

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
llvm_config.add_tool_substitutions(["mlir-translate", "not", "split-file"], [config.llvm_tools_dir])

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
# AND THE RUNTIME HEADER'S DIRECTORY: every native program includes
# ctcompile/CTNative/Runtime/ctnative.hpp, so every compile of one needs it.
runtime_include = f"-I {config.ctcompile_src_root}/include"
config.substitutions.append(
    ("%cxx_exe", f"{config.host_cxx} -std=c++23 {runtime_include} -I {config.ctbrowser_include}")
)

config.substitutions.append(
    (
        "%cxx",
        f"{config.host_cxx} -std=c++23 -fsyntax-only {runtime_include} -I {config.ctbrowser_include}",
    )
)

# %gxx AND %clangxx ARE BOTH COMPILERS, NOT THE HOST ONE. The Target/Cpp tests
# run the emitted C++ under GCC and Clang because a gate that silently drops a
# compiler passes vacuously (test/cmake/Native.cmake); %cxx would be one of
# them. Same preference order as Target/Cpp/harness.py so the python drivers
# and the RUN lines pick the same binaries. Optimisation and sanitizer flags
# stay on the RUN line, since they differ per test.
for name, candidates in (("%gxx", ("g++-13", "g++")), ("%clangxx", ("clang++-18", "clang++"))):
    found = next((shutil.which(c) for c in candidates if shutil.which(c)), candidates[-1])
    config.substitutions.append(
        (name, f"{found} -std=c++23 {runtime_include} -Wall -Wextra -Werror -Wconversion -pedantic")
    )

config.substitutions.append(("%node", config.node or shutil.which("node") or "node"))
config.substitutions.append(("%native_reference", config.native_reference))

# THE NATIVE GATES AS SUBSTITUTIONS, so a fixture's RUN lines name only what
# differs per fixture: the module, the program, the global to break. The
# compilation-unit gate takes the HOST compiler bare (no ctbrowser include
# path: standalone is the point), the same nm CMake found, the interpreter
# reference and the type oracle as the nm control that links the interpreter.
# INSERTED AT THE FRONT of the list: lit expands substitutions in order and
# does not re-scan, so the tool names these expand to (ctjs-translate,
# ctcompile-test-type-oracle) are resolved by the tool substitutions above
# only if those run AFTER this one.
checks = os.path.join(config.test_source_root, "CTNative", "Checks")
config.substitutions.insert(
    0,
    (
        "%compilation_unit",
        f"python3 {checks}/compilation-unit.py --translate ctjs-translate --cxx {config.host_cxx}"
        f" --nm {config.nm} --reference {config.native_reference}"
        " --vm-linked ctcompile-test-type-oracle",
    ),
)
config.substitutions.insert(
    0,
    (
        "%compile_clean",
        f"python3 {checks}/compile-clean.py --translate ctjs-translate"
        f" --compilers {config.clean_compilers}",
    ),
)
config.substitutions.insert(
    0,
    (
        "%print_deduced",
        f"python3 {checks}/print-deduced.py --translate ctjs-translate --cxx {config.host_cxx}",
    ),
)
# The modules the build writes for every native fixture (Native.cmake's
# ctcompile_add_native_pipeline: <name>.pipeline{,.deduced,.mutated}.emitc.mlir)
# - the same files docs/refactor-goldens.md measures a refactor against, so
# the gate and the instrument read one artefact.
config.substitutions.append(("%{obj}", config.test_exec_root))
config.substitutions.append(("%{monorepo}", config.monorepo_root))
config.substitutions.append(("%host_cxx", config.host_cxx))
config.substitutions.append(("%nm", config.nm))
# pipeline.cmake is the build's own lowering script; default-optimizations.test
# runs it under each switch so the modules it gates are what the build writes.
config.substitutions.append(("%cmake", config.cmake))
# The Browser drivers compile against the DOM's public API with the compiler
# Native.cmake chose for it, and read the engine's build tree.
config.substitutions.append(("%dom_clang", config.dom_clang))
config.substitutions.append(("%build", config.build_root))

# THE PYTHON DRIVERS IMPORT EACH OTHER ACROSS DIRECTORIES (CTNative/harness.py
# says how), and a hand run wants the same path.
config.environment["PYTHONPATH"] = config.test_source_root
