# ctcompile: an application directory in, a native executable out

**Where it is. The repository is a monorepo and `ctcompile` is a project in it that
builds, links against the engine's narrow compiler-facing libraries and reports
what it was built against. It compiles nothing yet. The whole engine suite is
green from the new layout — the number that matters for a migration is that it
did not move.**

**Done:** Phase -1, the repository restructure — `ctbrowser/` and `ctcompile/`
as sibling projects, `ctbrowser/` as the configure root, the test suite split
three ways, `third-party/` at repository scope, the presets that make a
runtime-only build an *enforced* configuration rather than a claim, and a stub
executable behind a real command line.

**Next:** Phase 0, the inventories — and the one that sizes everything after it
is the bytecode table: every opcode with its operands, allocation, GC, throwing,
re-entrancy and suspension behaviour, as an X-macro `.def` that the VM's own
decoder then consumes, so the compiler's table and the interpreter's cannot
drift apart into a miscompile.

The master plan is 21 files under `ctcompile-plan/`, and
`01-objective-and-ground-truth.md` overrides the rest of it. This document is
the ladder actually climbed, measured at each rung, in the shape
`docs/plans/bootstrap.md` uses.

---

## Phase -1: a monorepo is a claim about dependency direction

The migration is mechanical and the reason for it is not. `ctcompile` depends on
`ctbrowser`, on LLVM and on MLIR; `ctbrowser` depends on none of them and has to
keep building on a machine with 7.5 GiB of RAM and no LLVM installed at all. A
`tools/ctcompile/` inside the engine would have made that a matter of everyone's
good intentions. Two sibling projects with one arrow between them make it a
matter of what CMake can see.

So the arrow is checked rather than stated. `ctbrowser/CMakeLists.txt` adds its
siblings **last**, in their own subdirectory scope, and then fails the configure
if `LLVMCore`, `MLIRIR` or `MLIRSupport` is a target in its own scope. And the
`browser-no-llvm` preset goes further: it sets
`CMAKE_DISABLE_FIND_PACKAGE_{LLVM,MLIR,LLD}`, so a `find_package` that drifts
into the engine does not quietly succeed on the one machine that has them
installed. Principle 8 is now two lines of CMake and a preset instead of a
paragraph.

### What moved, and the three places the plan was not followed

Everything went with `git mv`, so the rename history survives:

| from | to |
|---|---|
| `CMakeLists.txt`, `CMakePresets.json` | `ctbrowser/` — **the configure root** |
| `include/` | `ctbrowser/include/` — include *paths* unchanged |
| `src/core` … `src/gpu` | `ctbrowser/lib/Core` … `ctbrowser/lib/GPU` |
| `examples/cli/{ctbrowse,ctdrive}.cpp` | `ctbrowser/tools/{ctbrowse,ctdrive}/` |
| `tests/{unit,js}` | `ctbrowser/unittests/` |
| `tests/{corpus,golden,lint,package,stress,baseline,support}` | `ctbrowser/test/` |
| `tests/bench/` | `ctbrowser/benchmarks/` |
| `fonts/` | `ctbrowser/resources/fonts/` |
| `vendor/` | `ctbrowser/vendor/` |
| `external/` (submodules), `third_party/angle` | `third-party/` |
| `cmake/toolchain-windows-x86_64.cmake` | `cmake/toolchains/windows-x86_64.cmake` |

`tools/` did **not** move. The plan proposes a `utils/` split — `build/`,
`release/`, `formatting/`, `ci/` — and `tools/` already holds all of that,
organised and documented in `docs/tools.md`. A second directory meaning the same
thing is not a structure; and `utils/ci/` describes automation this repository
decided in 2026-08-08 not to have.

`ctbrowser/lib/<Subsystem>` is CamelCase and `include/ctbrowser/<subsystem>/` is
not, which looks like an inconsistency and is a distinction: **an include path is
a public name** and every `#include` in the tree, in every consumer and in
`ctbrowser/test/lint/api_surface`'s allow-list, spells it lowercase. The implementation
directory is private to the build, so it can follow LLVM's spelling for free.
Not one `#include` line changed in the migration.

`ctbrowser/vendor/` rather than the plan's `test/corpus/`: the six Bootstrap
fixtures reach their stylesheet as `../../vendor/bootstrap/bootstrap.css`, which
resolves against the *page*. Keeping `vendor/` one level above `examples/`
preserves every one of those paths, and the Chrome parity harness — which is the
project's sharpest gate — reads them exactly as it did before.

### The two things that only look like directory naming

**`resources/fonts/` versus `fonts/`.** The runtime's `font_path` defaults to
`fonts`, resolved against the current directory — which is the ctest working
directory in the source tree and the directory beside the executable in a
shipped application. Renaming the source directory made those two disagree, and
the failure mode is every text golden moving because real faces were not found
and the 8×8 bitmap font stood in. Rather than have the runtime guess between two
layouts, it learned one environment variable: `CTBROWSER_FONT_PATH`, set by
`ctbrowser/cmake/modules/CTTest.cmake` for the suite and by `ctbrowser/examples/CMakeLists.txt` for
the examples. A shipped exe still ships `fonts/` beside itself and needs nothing.

`ENVIRONMENT` is one CMake property, not a list that accumulates, so the three
places that set it for other reasons — the ANGLE suppressions, `gpu_basics`,
`bindings_basics` — name the font path again. That is stated in each of them,
because the failure it prevents is silent.

**Test properties are directory-scoped.** `set_tests_properties` resolves a test
name in the directory that created it, so splitting the suite three ways meant
each property had to move to the file that registers its test. The ANGLE
suppression loop stayed with the corpus in `test/`; `gpu_basics` and
`bindings_basics` went with the unit tests. A property set from the wrong scope
does not warn.

### The gate

`tools/remote-build.sh` is the whole gate — there is no CI and none is planned —
so the migration is not finished until that script is. It now configures from
`ctbrowser/`, and its `default` preset sets `CTBROWSER_ENABLE_PROJECTS=ctcompile`
so **one command builds and tests both projects**. A sibling nobody builds is a
sibling nobody notices breaking.

### The stub, and why it says what it says

`ctcompile --version` reports its own version and the engine's, plus the number
of bytecode operations that engine defines. The opcode count is not decoration:
it is the size of the AOT coverage problem, and Phase 0 has to account for every
one of them. It is counted today as `op::halt + 1` — the simplest thing that is
true — and Phase 0 replaces that with the generated table and a `static_assert`
that the table and the VM's decoder agree.

The command line is Boost.Program_options rather than `llvm::cl`, which the
dependency order would otherwise prefer. LLVM is behind `CTCOMPILE_ENABLE_MLIR`
and off until Phase 7, and a compiler that needed a 2 GB dependency to parse
`--version` would make every developer's first build the slowest one. Compiled
Boost libraries have been allowed here since 2026-07-31.

### LLVM is pinned at 22, not the plan's 20

Neither the build box nor anything else here can install 20: apt ships MLIR 18
for Ubuntu 24.04 and brew ships 22.1.8, and the package policy has been
brew-first since 2026-08-01. `cmake/LLVMVersion.cmake` names 22.1.8 and
`ct_require_llvm_version()` refuses to configure outside it, with a message
naming both versions and the install it found — because without it the failure
lands inside `mlir-tblgen` and names a TableGen template instead of a version.

The cost is real and is written down in `docs/LLVMUpgrade.md`: every ODS, PDLL
and pass-generation construct the master plan spells was written against 20-era
syntax and has to be **verified against 22 before it is relied on**. PDLL is the
youngest of the three and moves fastest.

---

## The ladder ahead

| phase | what | where |
|---|---|---|
| **0** | Inventories: bytecode, program representation, call paths, GC roots, HTML/CSS startup, and the performance baseline | the bytecode `.def` is the one that sizes the rest |
| 1–6 | Runtime preparation: the AOT ABI, mixed-mode dispatch, GC shadow frames, shared helpers | `ctbrowser/lib/AOT/` — runtime side, no MLIR yet |
| 7–9 | MLIR stood up, the CTJS dialect in ODS, the bytecode importer | register-machine SSA over frame slots, **never over a cell** |
| 10 | Generic CTJS runtime lowering | feeds both backends |
| **10A–10C** | **EmitC — the primary backend**: CTJS to EmitC, C++ emission, translation-unit partitioning over `function_proto`s | every corpus is one UMD bundle, so there is no module split to make |
| 11–12A | The LLVM dialect backend, second | kept as the S0 fallback and the differential oracle |
| 13–14 | Opcode coverage, then async and suspension | only the top frame can suspend today |
| 15–17A | Program metadata, modules, the HTML blueprint, the compiled style program, assets | hold 16B until the CSS engine stops moving |
| 18–20 | The generated launcher, strict AOT-only mode, the validation matrix | the launcher is a fixed library, not generated C++ |

Phases 21–43 are optimization and infrastructure, all optional, and none of them
blocks the core ladder.
