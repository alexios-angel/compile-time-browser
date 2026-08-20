# ctcompile: an application directory in, a native executable out

**Where it is. The repository is a monorepo, `ctcompile` builds beside the
engine, and Phase 0's four code-facing inventories exist as tables the build
checks. It compiles nothing yet. 94 of 94 tests pass in both dispatch
configurations.**

**Done:** Phase -1, the repository restructure — sibling projects, `ctbrowser/`
as the configure root, the suite split three ways, `third-party/` at repository
scope, presets that make a runtime-only build an *enforced* configuration, and a
stub behind a real command line · **Phase 0's bytecode, program-representation,
call-path and GC-root inventories**, each an X-macro table or a wall of
`static_assert`s rather than prose.

**Next:** the rest of Phase 0 — the HTML and CSS startup inventories, which are
harder than the three above because their deliverable is a *differential
comparator* (the acceptance test for Phases 16A and 16B, written before the
thing it accepts), and the performance baseline as committed JSON with the
machine and build configuration beside it.

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

## Phase 0: four tables, because a document cannot be checked

The inventories are not documents. A prose description of a layout drifts from
the layout within weeks and nothing can tell you it has; a table can be walked
by a test, and a `static_assert` cannot be ignored at all. So:

| inventory | where | form |
|---|---|---|
| bytecode | `ctbrowser/include/ctbrowser/script/bytecode_opcodes.def` | 93 rows × 11 columns |
| program representation | `ctcompile/include/ctcompile/JavaScript/EngineContract.hpp` | `static_assert`s |
| call paths | `ctcompile/include/ctcompile/JavaScript/CallPaths.def` | 11 entries |
| GC roots | `ctcompile/include/ctcompile/JavaScript/GCRoots.def` | 17 entries |

### The opcode table, and the column that costs money

Every row was read out of its handler in `run_loop.cpp` and whatever it
delegates to, then re-derived independently by a second reader against the same
code. **`may_reenter` is true for 31 of the 93** — a third of the instruction
set can run page JavaScript in the middle of what looks like a primitive
operation. Arithmetic reaches `valueOf` through `to_primitive`; a property read
hits an accessor or a proxy trap; and `throw_value` does it on the path nobody
instruments, because describing an *uncaught* throw reads `.name` and
`.message` off the thrown object. An AOT backend that trusts the opcode's name
omits a root exactly where one is needed.

Two tiers of failure are kept apart in the table and must stay apart in the
backend: `raise()` sets the failure flag and **no `try`/`catch` can see it** —
the allocation ceiling and a missing module are raises — while `thrown_` plus
`unwind_to_handler` is the catchable path. Both set `may_throw`.

### One list, and the build that proves it

`run_loop.cpp` kept a private 93-name macro for its computed-goto label table.
The comment above it records what a second list costs: two opcodes were missing
from it for as long as modules had existed, and the only reason nothing jumped
to address zero is that computed gotos are off by default. That list is gone —
the table is built by including the `.def`, and the count assert sits in
`bytecode.hpp` beside the enum it checks.

Which means **the change only compiles in the configuration nobody builds**, so
that is the configuration it was verified in: `-DCTBROWSER_COMPUTED_GOTO=ON`,
94 of 94.

### What Phase 4 needs to know before it starts

`set_external_roots` **assigns** a `std::function` rather than appending, and
`dom_bindings::register_roots` is already its one caller. The plan tells Phase 4
to route AOT shadow frames through that hook rather than inventing a parallel
mechanism — and doing that naively unregisters every event listener, timer
callback and pending fetch the page owns, whose failure mode is those objects
being collected while the page is still using them. Either the hook grows a
list, or the AOT registrar chains what it found. Decide it before writing the
shadow frame, not after.

### Still open in Phase 0

The HTML and CSS startup inventories, whose deliverable is a **differential
comparator** — the acceptance test for Phases 16A and 16B, written before the
thing it accepts — and the performance baseline, stored as committed JSON with
the machine and build configuration recorded beside it, because a baseline
without its configuration is unusable six months later.

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
