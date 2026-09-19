# ctcompile: an application directory in, a native executable out

**Where it is.** This is the ladder actually climbed, measured at each rung,
in the shape `docs/plans/bootstrap.md` uses: Phase -1 through Phase 10, the
program image (Phase 15) and the two PDLL rungs, each with what it cost and
what it falsified. It is
history plus the reasoning behind every decision, not the status board -
**current status, the newest gate numbers and what lands next are in
`docs/HANDOFF.md`**, whose top entry is the latest. Since these rungs were
written the native EmitC backend has run whole applications with no
interpreter (`docs/plans/launcher.md`), and the pin is LLVM 23.1.0
(`ctcompile/CMakeLists.txt`, inlined 2026-09-15 from the `cmake/LLVMVersion.cmake`
the sections below name); dated figures below are as measured on the day.

The master plan is `../ctcompile-plan/` (outside the repository), and
`01-objective-and-ground-truth.md` overrides the rest of it.

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
`navigation` — name the font path again. That is stated in each of them,
because the failure it prevents is silent.

**Test properties are directory-scoped.** `set_tests_properties` resolves a test
name in the directory that created it, so splitting the suite three ways meant
each property had to move to the file that registers its test. The ANGLE
suppression loop stayed with the corpus in `test/`; `gpu_basics` and
`bindings_basics` (now `navigation`) went with the unit tests. A property set from the wrong scope
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

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="phase-0-four-tables-because-a-document-cannot-be-checked"></a>
- [Phase 0: four tables, because a document cannot be checked](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#phase-0-four-tables-because-a-document-cannot-be-checked)
<a id="the-opcode-table-and-the-column-that-costs-money"></a>
- [The opcode table, and the column that costs money](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-opcode-table-and-the-column-that-costs-money)
<a id="one-list-and-the-build-that-proves-it"></a>
- [One list, and the build that proves it](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#one-list-and-the-build-that-proves-it)
<a id="what-phase-4-needs-to-know-before-it-starts"></a>
- [What Phase 4 needs to know before it starts](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#what-phase-4-needs-to-know-before-it-starts)
<a id="the-html-comparator-which-exists-before-the-thing-it-accepts"></a>
- [The HTML comparator, which exists before the thing it accepts](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-html-comparator-which-exists-before-the-thing-it-accepts)
<a id="css-the-parse-is-readable-the-compile-is-not"></a>
- [CSS: the parse is readable, the compile is not](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#css-the-parse-is-readable-the-compile-is-not)
<a id="a-live-engine-gap-found-by-writing-the-test"></a>
- [A live engine gap, found by writing the test](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#a-live-engine-gap-found-by-writing-the-test)
<a id="the-baseline-and-what-it-deliberately-leaves-out"></a>
- [The baseline, and what it deliberately leaves out](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-baseline-and-what-it-deliberately-leaves-out)
<a id="three-inventories-that-reduce-to-standing-decisions"></a>
- [Three inventories that reduce to standing decisions](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#three-inventories-that-reduce-to-standing-decisions)
<a id="s0-where-the-time-actually-is-and-what-that-means-for-this-project"></a>
- [S0: where the time actually is, and what that means for this project](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#s0-where-the-time-actually-is-and-what-that-means-for-this-project)
<a id="the-interpreter-is-not-where-the-time-goes"></a>
- [The interpreter is not where the time goes](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-interpreter-is-not-where-the-time-goes)
<a id="the-compile-time-half-measured"></a>
- [The compile-time half, measured](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-compile-time-half-measured)
<a id="phase-15-the-program-image-which-is-where-the-time-actually-is"></a>
- [Phase 15: the program image, which is where the time actually is](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#phase-15-the-program-image-which-is-where-the-time-actually-is)
<a id="programsource-is-kept-unless-optimisation-is-asked-for"></a>
- [`program::source` is kept unless optimisation is asked for](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#programsource-is-kept-unless-optimisation-is-asked-for)
<a id="the-hash-on-the-page-path-and-a-hash-that-was-fast-and-wrong"></a>
- [The hash on the page path, and a hash that was fast and wrong](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-hash-on-the-page-path-and-a-hash-that-was-fast-and-wrong)
<a id="what-caught-it-and-what-a-whole-page-number-cost-to-believe"></a>
- [What caught it, and what a whole-page number cost to believe](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#what-caught-it-and-what-a-whole-page-number-cost-to-believe)
<a id="validation-is-15-of-an-image-load-and-a-table-beat-a-fast-path"></a>
- [Validation is 15% of an image load, and a table beat a fast path](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#validation-is-15-of-an-image-load-and-a-table-beat-a-fast-path)
<a id="one-script-one-program--and-the-three-defects-that-bought"></a>
- [One `<script>`, one program — and the three defects that bought](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#one-script-one-program--and-the-three-defects-that-bought)
<a id="the-review-found-three-things-wrong-with-it-and-one-older-thing"></a>
- [The review found three things wrong with it, and one older thing](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#the-review-found-three-things-wrong-with-it-and-one-older-thing)
<a id="a-dead-scripts-try-caught-the-next-scripts-throw"></a>
- [A dead script's `try` caught the next script's `throw`](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#a-dead-scripts-try-caught-the-next-scripts-throw)
<a id="and-the-css-parser-had-been-reading-freed-memory-the-whole-time"></a>
- [And the CSS parser had been reading freed memory the whole time](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#and-the-css-parser-had-been-reading-freed-memory-the-whole-time)
<a id="finally-ran-on-two-ways-out-of-a-try-block-and-javascript-has-five"></a>
- [`finally` ran on two ways out of a try block, and JavaScript has five](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#finally-ran-on-two-ways-out-of-a-try-block-and-javascript-has-five)
<a id="and-the-fingerprint-could-not-see-any-of-it"></a>
- [And the fingerprint could not see any of it](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#and-the-fingerprint-could-not-see-any-of-it)
<a id="a-32-bit-operand-that-three-sites-made-16-bits-wide"></a>
- [A 32-bit operand that three sites made 16 bits wide](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#a-32-bit-operand-that-three-sites-made-16-bits-wide)
<a id="phase-2s-table-is-done-and-phase-2s-gate-was-never-met"></a>
- [Phase 2's table is done and Phase 2's gate was never met](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#phase-2s-table-is-done-and-phase-2s-gate-was-never-met)
<a id="what-is-left-of-a-page-load-and-why-16a-and-16b-are-not-next"></a>
- [What is left of a page load, and why 16A and 16B are not next](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#what-is-left-of-a-page-load-and-why-16a-and-16b-are-not-next)
<a id="two-images-of-one-script-and-the-order-they-arrived-in"></a>
- [Two images of one script, and the order they arrived in](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#two-images-of-one-script-and-the-order-they-arrived-in)
<a id="phase-2s-gate-met--and-the-row-it-falsified"></a>
- [Phase 2's gate, met — and the row it falsified](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#phase-2s-gate-met--and-the-row-it-falsified)
<a id="an-application-directory-in-an-executable-out"></a>
- [An application directory in, an executable out](ctcompile/01-phase-0-four-tables-because-a-document-cannot-be-checked.md#an-application-directory-in-an-executable-out)
<a id="eight-silent-defects-in-it-and-the-shape-they-share"></a>
- [Eight silent defects in it, and the shape they share](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#eight-silent-defects-in-it-and-the-shape-they-share)
<a id="the-newline-nobody-would-think-to-look-for"></a>
- [The newline nobody would think to look for](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-newline-nobody-would-think-to-look-for)
<a id="phase-1s-gate-closed"></a>
- [Phase 1's gate, closed](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-1s-gate-closed)
<a id="what-the-manifest-found-in-its-first-five-minutes"></a>
- [What the manifest found in its first five minutes](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#what-the-manifest-found-in-its-first-five-minutes)
<a id="phase-3-the-one-line-that-read-aot_entry"></a>
- [Phase 3: the one line that read `aot_entry`](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-3-the-one-line-that-read-aot_entry)
<a id="one-decision-five-askers"></a>
- [One decision, five askers](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#one-decision-five-askers)
<a id="the-counters-are-the-test"></a>
- [The counters are the test](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-counters-are-the-test)
<a id="and-it-costs-the-interpreter-nothing-measured"></a>
- [And it costs the interpreter nothing, measured](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#and-it-costs-the-interpreter-nothing-measured)
<a id="where-it-stops-said-rather-than-left-to-be-found"></a>
- [Where it stops, said rather than left to be found](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#where-it-stops-said-rather-than-left-to-be-found)
<a id="phase-4-the-collector-that-never-ran-and-what-happened-when-it-did"></a>
- [Phase 4: the collector that never ran, and what happened when it did](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-4-the-collector-that-never-ran-and-what-happened-when-it-did)
<a id="it-found-two-use-after-frees-on-new-immediately"></a>
- [It found two use-after-frees on `new`, immediately](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#it-found-two-use-after-frees-on-new-immediately)
<a id="two-mechanisms-and-why-each-is-the-general-one"></a>
- [Two mechanisms, and why each is the general one](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#two-mechanisms-and-why-each-is-the-general-one)
<a id="falsified-under-asan-because-that-is-where-the-bug-is-visible"></a>
- [Falsified under asan, because that is where the bug is visible](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#falsified-under-asan-because-that-is-where-the-bug-is-visible)
<a id="phase-5-26-of-69-rows-and-the-table-that-had-stopped-being-true"></a>
- [Phase 5: 26 of 69 rows, and the table that had stopped being true](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-5-26-of-69-rows-and-the-table-that-had-stopped-being-true)
<a id="the-flags-test-found-three-things-and-the-tables-had-answered-two"></a>
- [The flags test found three things and the tables had answered two](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-flags-test-found-three-things-and-the-tables-had-answered-two)
<a id="two-extractions-and-the-differential-that-guards-them"></a>
- [Two extractions, and the differential that guards them](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#two-extractions-and-the-differential-that-guards-them)
<a id="sixteen-rows-needed-no-extraction-at-all"></a>
- [Sixteen rows needed no extraction at all](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#sixteen-rows-needed-no-extraction-at-all)
<a id="and-the-table-had-stopped-pointing-at-the-code"></a>
- [And the table had stopped pointing at the code](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#and-the-table-had-stopped-pointing-at-the-code)
<a id="what-blocks-a-minimal-compiled-function"></a>
- [What blocks a minimal compiled function](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#what-blocks-a-minimal-compiled-function)
<a id="a-whole-function-hand-compiled-agreeing-with-the-interpreter"></a>
- [A whole function, hand-compiled, agreeing with the interpreter](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#a-whole-function-hand-compiled-agreeing-with-the-interpreter)
<a id="it-found-the-safepoint-in-the-wrong-place"></a>
- [It found the safepoint in the wrong place](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#it-found-the-safepoint-in-the-wrong-place)
<a id="three-things-the-test-got-wrong-about-itself"></a>
- [Three things the test got wrong about itself](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#three-things-the-test-got-wrong-about-itself)
<a id="phase-6-the-row-that-said-it-could-not-be-written"></a>
- [Phase 6: the row that said it could not be written](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-6-the-row-that-said-it-could-not-be-written)
<a id="completions-not-unwinding"></a>
- [Completions, not unwinding](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#completions-not-unwinding)
<a id="phase-7-mlir-stood-up-before-a-single-operation-exists"></a>
- [Phase 7: MLIR, stood up before a single operation exists](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-7-mlir-stood-up-before-a-single-operation-exists)
<a id="what-phase--1-had-already-built-and-i-nearly-rebuilt"></a>
- [What Phase -1 had already built, and I nearly rebuilt](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#what-phase--1-had-already-built-and-i-nearly-rebuilt)
<a id="three-policy-rules-that-only-mean-something-once-you-hit-them"></a>
- [Three policy rules that only mean something once you hit them](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#three-policy-rules-that-only-mean-something-once-you-hit-them)
<a id="one-deviation-with-its-reason-in-the-file"></a>
- [One deviation, with its reason in the file](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#one-deviation-with-its-reason-in-the-file)
<a id="the-action-item-answered-rather-than-deferred"></a>
- [The action item, answered rather than deferred](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-action-item-answered-rather-than-deferred)
<a id="and-the-test-is-two-passes-not-one"></a>
- [And the test is two passes, not one](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#and-the-test-is-two-passes-not-one)
<a id="phase-8-the-dialect-and-what-ties-it-to-the-abi"></a>
- [Phase 8: the dialect, and what ties it to the ABI](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-8-the-dialect-and-what-ties-it-to-the-abi)
<a id="four-things-mlir-22-wanted-that-the-policys-snippets-do-not-show"></a>
- [Four things MLIR 22 wanted that the policy's snippets do not show](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#four-things-mlir-22-wanted-that-the-policys-snippets-do-not-show)
<a id="one-deviation-with-its-reason-in-the-file-1"></a>
- [One deviation, with its reason in the file](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#one-deviation-with-its-reason-in-the-file)
<a id="the-verifier-worth-reading"></a>
- [The verifier worth reading](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-verifier-worth-reading)
<a id="phase-9-bytecode-into-ctjs-mlir"></a>
- [Phase 9: bytecode into CTJS MLIR](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-9-bytecode-into-ctjs-mlir)
<a id="the-register-file-is-the-block-argument-vector"></a>
- [The register file is the block argument vector](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-register-file-is-the-block-argument-vector)
<a id="three-bugs-of-the-shape-this-project-keeps-meeting"></a>
- [Three bugs of the shape this project keeps meeting](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#three-bugs-of-the-shape-this-project-keeps-meeting)
<a id="the-measurement-is-the-work-list"></a>
- [The measurement is the work list](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-measurement-is-the-work-list)
<a id="four-things-mlir-22-required"></a>
- [Four things MLIR 22 required](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#four-things-mlir-22-required)
<a id="phase-10-one-pattern-and-what-the-arity-check-found"></a>
- [Phase 10: one pattern, and what the arity check found](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#phase-10-one-pattern-and-what-the-arity-check-found)
<a id="the-arity-check-fired-on-its-first-run-and-was-right"></a>
- [The arity check fired on its first run and was right](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-arity-check-fired-on-its-first-run-and-was-right)
<a id="the-first-pdll-pattern-and-where-the-declarative-boundary-actually-is"></a>
- [The first PDLL pattern, and where the declarative boundary actually is](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#the-first-pdll-pattern-and-where-the-declarative-boundary-actually-is)
<a id="what-was-converted"></a>
- [What was converted](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#what-was-converted)
<a id="six-things-pdll-could-not-express-with-what-happened"></a>
- [Six things PDLL could not express, with what happened](ctcompile/02-eight-silent-defects-in-it-and-the-shape-they-share.md#six-things-pdll-could-not-express-with-what-happened)
<a id="and-two-things-about-the-driver-which-is-not-optional"></a>
- [And two things about the driver, which is not optional](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#and-two-things-about-the-driver-which-is-not-optional)
<a id="what-it-cost"></a>
- [What it cost](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#what-it-cost)
<a id="the-boundary"></a>
- [The boundary](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#the-boundary)
<a id="pdll-over-our-own-dialect-and-the-guard-that-makes-it-safe"></a>
- [PDLL over our own dialect, and the guard that makes it safe](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#pdll-over-our-own-dialect-and-the-guard-that-makes-it-safe)
<a id="1-matching-our-operations-already-worked"></a>
- [1. Matching our operations already worked](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#1-matching-our-operations-already-worked)
<a id="2-two-things-it-accepts-at-exit-0--one-a-trap-one-a-documented-feature"></a>
- [2. Two things it accepts at exit 0 — one a trap, one a documented feature](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#2-two-things-it-accepts-at-exit-0--one-a-trap-one-a-documented-feature)
<a id="3-a-native-constraint-is-the-substitute-and-it-is-better"></a>
- [3. A native constraint is the substitute, and it is better](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#3-a-native-constraint-is-the-substitute-and-it-is-better)
<a id="why-that-arm-and-no-other-in-replace"></a>
- [Why that arm and no other in `replace()`](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#why-that-arm-and-no-other-in-replace)
<a id="precedence-is-expressible-the-first-failing-thing-diagnostic-is-not"></a>
- [Precedence is expressible; the first-failing-thing diagnostic is not](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#precedence-is-expressible-the-first-failing-thing-diagnostic-is-not)
<a id="is-a-forked-ctjs-pdll-worth-it-no"></a>
- [Is a forked `ctjs-pdll` worth it? No.](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#is-a-forked-ctjs-pdll-worth-it-no)
<a id="the-ladder-ahead"></a>
- [The ladder ahead](ctcompile/03-and-two-things-about-the-driver-which-is-not-optional.md#the-ladder-ahead)
