# CMake ownership and package hygiene, 2026-09-19

Resumed clean `03b8a45f` after the file-split handoff. The user requested local
CMake ownership for significant C++ folders and removal of parent-relative CMake
paths, and explicitly allowed libraries used to organize the build. Three agents
worked on disjoint compiler, browser-library and test/tool areas in one isolated
worktree. Root reviewed, gated and atomically merged it as **099852d2**.

## Changes

- `66e3e135`: compiler libraries, public headers, TableGen and probe ownership.
- `5f8b0c8b`: browser libraries and public header ownership.
- `8775fa9f`: compiler test source ownership.
- `35f58600`: hygiene check and Windows toolchain repository fallback.
- `92f175ad`: browser test, example and tool ownership.
- `9c448c17`: complete installed headers and subsystem aliases.

There are now 224 maintained `CMakeLists.txt` files, up from 41. All 192 folders
containing maintained C++ sources or headers have one. Their parents traverse
them with `add_subdirectory`; leaf folders register their own files. The package
consumer remains an intentionally standalone CMake project. Vendor sources and
dependency checkouts are unchanged.

CTNative's Analysis and Lowering no longer list sibling sources. HostContract,
PartialEvaluation and other folders contribute locally to the existing targets;
Analysis's MLIR object target retains its source membership. Browser source
ownership no longer depends on recursive globs. Header owners maintain the
public file sets and generated table parts. A header-only test-support target
owns shared test headers, and the C++20 deduction probe has an optional executable
excluded from the default build.

The `cmake_hygiene` CTest checks folder ownership and parent-relative CMake paths.
It works without Git metadata, including on the devbox source copy. The Windows
toolchain fallback now resolves `tools/llvm-mingw` from the repository root.

The package check exposed existing missing public inputs: `aot/aot.hpp`,
`aot/aot_entry.h`, `script/dispatch.hpp` and `script/bytecode_opcodes.def`. They
now install with their owning public header sets. The 128 previous public files
remain, for 132 total. The package also supplies the same short subsystem aliases
as the build tree, retaining its existing exported target names. This fixes
standalone compiler configuration against the installed package.

## Validation

All builds and remote commands held `/tmp/ctbrowser-devbox-build.lock`. No local
C++ build ran. Evidence and exact orchestration scripts are under
`/tmp/ctbrowser-cmake-hygiene-20260919-evidence/`.

The generated CMake model preserves all 857 existing translation-unit
registrations, compile flags, include precedence, link commands and executable
paths. The model has 406 targets instead of 405 because of the optional probe,
and 858 compiled-source registrations instead of 857. All 322 original test
commands/properties remain; only `cmake_hygiene` is added. Comparison normalizes
equivalent absolute paths and the shared PCH's move into `unittests/unit`.
New private header registrations do not compile as translation units.

Explicit affected build targets, selected across the focused runs:

```text
ctjs-opt ctjs-translate ctcompile-tool ctcompile-deduction-probe
ctbrowser-tool-ctdrive ctbrowser-tool-ctrun
ctbrowser-test-core_basics ctbrowser-test-dom_element
ctbrowser-test-style_closest ctbrowser-test-vm_functions
ctbrowser-test-api_surface ctbrowser-test-vm_operators
ctbrowser-test-p5_ratchet
ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays
ctcompile-test-importer-coverage
```

Ten distinct CTests passed across focused runs:

```text
cmake_hygiene api_surface core_basics dom_element style_closest
vm_functions vm_operators ctcompile_escape_analysis_arrays
ctcompile_host_contract ctcompile_importer_coverage
```

The first eight-test run took 2.62 seconds. The seven-test run after include-order
repairs took 0.69 seconds; the final two-test run took 0.08 seconds. These runs
overlap. The p5 ratchet target was built but its corpus was not run.

Two selected lit cases passed in 287.07 seconds:

```text
CTNative/Lowering/Objects/class-dom.mlir
CTNative/Checks/deduction-probe.test
```

Class/DOM measured 632 Node/interpreter observations, eight combined native
executions and 4,910 refusals. The deduction probe ran on GCC, Clang and the
pinned host compiler. No other lit cases were run.

Additional focused configuration/package checks passed:

- Browser-only configuration with LLVM, MLIR and LLD package discovery disabled,
  and tests/examples disabled.
- A CMake script verified the Windows toolchain's repository fallback. No
  Windows compilation was run.
- Warm engine-library installation into a temporary prefix, followed by building
  and running the existing `ctbrowser/test/package` consumer: `package check ok`.
- Standalone ctcompile configuration against that prefix with MLIR and tests off;
  built `ctcompile-tool`, `ctcompile-html`, `ctcompile-css`, and
  `ctcompile-javascript`. Its version command passed and reported the installed
  engine version as `unknown`, as before.
- Local hygiene self-check, Python formatting and whitespace checks.

The required pinned `tools/format.sh --check` still reports 20 baseline
diagnostics in six unchanged C++ files. No C++ source content changed. The new
Python check passes Black. This is not a whole-tree formatting pass.

Initial gates caught build-directory collisions with existing tool executables
and include-order drift; both were corrected while preserving executable paths.
The package consumer initially failed on `aot_entry.h`; standalone configuration
initially failed on missing `ctbrowser::core`/`dom` aliases. Both now pass. An
isolated-worktree executable permission was needed by the existing ANGLE fetch
script; no script content or Git mode changed. A scratch toolchain probe needed
its CMake minimum-version policy before inspecting an empty list entry. Devbox
SSH later timed out; updating the allowed home IP restored access.

Full CTest/compiler lit, WPT/test262, corpus/matrix runs, sanitizers, Windows
cross-compilation and whole Bootstrap were skipped. Existing compliance counts
remain historical. No push.

## Next native boundary

B's ordinary `this._getConfig(t)` after its own `super()` still needs proof,
followed by Qi's lexical `LoadHome -> GetProto -> GetProperty -> Call`. Captured
helpers, full Bootstrap behavior, broader ownership/control flow and the
application driver remain. This CMake change adds no native admission rule.
