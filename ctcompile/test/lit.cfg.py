# The ctcompile lit configuration: test discovery, timeouts, and the tool and
# %-substitutions that RUN lines use. lit.site.cfg.py (generated from
# lit.site.cfg.py.in by Lit.cmake) sets the config.* paths read here.
import os
import shutil

import lit.formats
import lit.llvm

lit.llvm.initialize(lit_config, config)
from lit.llvm import llvm_config  # noqa: E402  (must follow initialize)
from lit.llvm.subst import ToolSubst  # noqa: E402

config.name = "ctcompile"
config.suffixes = [".mlir", ".td", ".test"]
# One hour per test, then a failure. A case that hangs (a native binary waiting
# on input that never comes, a fixed point that does not converge) would
# otherwise hold the whole run forever; this reports it as TIMEOUT, which fails
# the run. lit enforces the limit through psutil, which the lit environment
# must have (pyproject.toml's test group installs it beside lit); without it
# lit stops at once with that instruction rather than running without the
# limit.
lit_config.maxIndividualTestTime = 3600
config.excludes = ["Inputs"]
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.ctcompile_obj_root, "test")

# FileCheck, not, count: the standard LLVM tools RUN lines use.
llvm_config.use_default_substitutions()

# A list of directories, not one: each tool is built where its own CMakeLists
# puts it rather than in a shared bin/, and add_tool_substitutions accepts a
# search list.
tools = ["ctjs-opt", "ctjs-translate", "ctcompile"]
llvm_config.add_tool_substitutions(tools, config.ctcompile_tools_dirs)

# The C++ test executables, by target name, from the directory CMake builds
# them into (this suite's exec root), plus ctbrowser's launcher and browser,
# which the packaging round-trip test runs. unresolved="ignore" because
# add_tool_substitutions drops the whole group when one name is missing: a
# test executable that was not built should fail its own RUN line by name, not
# turn eleven others into "command not found".
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

# mlir-translate is LLVM's, not ours, so it is looked for where LLVM's tools are
# rather than in the directories ctcompile builds into. It turns an EmitC module
# into C++, which makes it part of the backend under test.
# `not` comes from LLVM's tool directory too: use_default_substitutions
# registers it only when it is already on PATH. Without it a RUN line using
# `not` fails with "not: command not found" and FileCheck then reports a
# missing string, which looks like a broken assertion.
# split-file is LLVM's too. ctjs-translate reads a whole file as one program,
# so a test holding a positive program and three negative ones would need four
# files; split-file cuts one file at its `//--- name` markers and drops the
# preamble where the RUN and CHECK lines live.
llvm_config.add_tool_substitutions(["mlir-translate", "not", "split-file"], [config.llvm_tools_dir])

# %cxx compiles the emitted translation unit against the real ABI headers with
# -fsyntax-only: the question is whether the emitted C++ agrees with aot.hpp
# (argument types, qualified names, status values), not whether it links. A
# backend that emits plausible C++ against a signature it invented is caught by
# the host compiler at no cost.
#
# %cxx_exe builds and links, for tests that must run the emitted code. Syntax
# alone cannot catch a miscompile: the C++ emitter can lose a copy on a
# block-argument edge and produce code that compiles and returns the wrong
# number, so such a test compiles the output, runs it, and uses the program's
# exit status as the assertion.
#
# %cxx_exe must be registered before %cxx. lit replaces the first pattern that
# matches, and "%cxx" matches the start of "%cxx_exe"; in the other order
# `%cxx_exe file.cpp` expands to `<compiler> -fsyntax-only ... _exe file.cpp`,
# which fails with a message about a file called "_exe".
#
# Both add the runtime header's directory: every native program includes
# ctcompile/CTNative/Runtime/ctnative.hpp.
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

# %gxx and %clangxx are two specific compilers, not the host one. The
# Target/Cpp tests run the emitted C++ under both GCC and Clang, because a
# check that silently skips a compiler would pass without testing it
# (test/cmake/Native.cmake). The preference order matches Target/Cpp/harness.py
# so the Python drivers and the RUN lines pick the same binaries. Optimisation
# and sanitizer flags stay on the RUN line, since they differ per test.
for name, candidates in (("%gxx", ("g++-13", "g++")), ("%clangxx", ("clang++-18", "clang++"))):
    found = next((shutil.which(c) for c in candidates if shutil.which(c)), candidates[-1])
    config.substitutions.append(
        (
            name,
            f"{found} -std=c++23 {runtime_include} -I {config.ctbrowser_include} -Wall -Wextra -Werror -Wconversion -pedantic",
        )
    )

config.substitutions.append(("%node", config.node or shutil.which("node") or "node"))
config.substitutions.append(("%native_reference", config.native_reference))

# The native-output check scripts (CTNative/Checks/*.py) as substitutions, so
# a fixture's RUN lines name only what differs per fixture: the module, the
# program, the global to break. %compilation_unit uses the host compiler
# (public headers only, no browser library), the nm CMake found, the
# interpreter reference, and ctcompile-test-type-oracle as a binary that does
# link the interpreter, to check that nm sees the difference.
# These are inserted at the front of the list: lit expands substitutions in
# order without re-scanning, so the tool names they expand to (ctjs-translate,
# ctcompile-test-type-oracle) are resolved only if the tool substitutions
# above run after them.
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
# The browser DOM drivers' shared command-line tail. Inserted at the front like
# the checks above, so the tool, %dom_clang, %build, %{monorepo}, %nm and %t
# substitutions still expand what it expands to.
config.substitutions.insert(
    0,
    (
        "%dom_driver",
        "--translate ctjs-translate --opt ctjs-opt --clang %dom_clang --build %build"
        " --include %{monorepo}/ctbrowser/include --work %t --nm %nm",
    ),
)
# %{obj}: where the build writes each native fixture's modules (Native.cmake's
# ctcompile_add_native_pipeline: <name>.pipeline{,.deduced,.mutated}.emitc.mlir).
# docs/refactor-goldens.md compares a refactor against the same files.
config.substitutions.append(("%{obj}", config.test_exec_root))
config.substitutions.append(("%{monorepo}", config.monorepo_root))
config.substitutions.append(("%host_cxx", config.host_cxx))
config.substitutions.append(("%nm", config.nm))
# pipeline.cmake is the build's own lowering script; default-optimizations.test
# runs it under each switch so the modules it checks are what the build writes.
config.substitutions.append(("%cmake", config.cmake))
# The browser drivers compile against the DOM's public API with the compiler
# Native.cmake chose for it, and read the engine's build tree.
config.substitutions.append(("%dom_clang", config.dom_clang))
config.substitutions.append(("%build", config.build_root))

# The Python drivers import each other across directories (CTNative/harness.py
# explains how), and running one by hand needs the same path.
config.environment["PYTHONPATH"] = config.test_source_root

# A driver that loops over named fixtures runs only those CTCOMPILE_FIXTURES
# selects (comma-separated globs; CTNative/harness.py selected_fixtures), so a
# change can be checked on the fixtures it touches. lit clears the environment,
# so the variable is passed through when set; unset, every fixture runs.
if "CTCOMPILE_FIXTURES" in os.environ:
    config.environment["CTCOMPILE_FIXTURES"] = os.environ["CTCOMPILE_FIXTURES"]
