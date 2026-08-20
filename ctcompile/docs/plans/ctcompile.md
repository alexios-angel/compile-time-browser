# ctcompile: an application directory in, a native executable out

**Where it is. The repository is a monorepo, `ctcompile` builds beside the
engine, and PHASE 0 IS COMPLETE: six inventories the build checks, two
differential comparators written before the things they will accept, and a
recorded startup baseline. It compiles nothing yet. 96 of 96 tests pass.**

**Done:** Phase -1, the repository restructure — sibling projects, `ctbrowser/`
as the configure root, the suite split three ways, `third-party/` at repository
scope, presets that make a runtime-only build an *enforced* configuration, and a
stub behind a real command line · **Phase 0's bytecode, program-representation,
call-path and GC-root inventories**, each an X-macro table or a wall of
`static_assert`s rather than prose.

**Next:** Phases 1–6, the runtime preparation — and the one everything
downstream reads is Phase 2's AOT ABI: the shared runtime helpers, declared once
in `ctbrowser` in a dependency-free X-macro so that drift between the runtime
that defines them and the compiler that emits calls to them is a *compile
error*.

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

### The HTML comparator, which exists before the thing it accepts

`ctcompile::html::compare` walks two documents in document order and returns the
first field they disagree on, with a path rather than a handle — because a
`node_id` is a generation-tagged handle whose tag is meaningless across
processes, which is also why a blueprint serializes ordinals. Every atom is
compared as **text through its own document's table**: ids are handed out in
first-interning order at run time, so comparing ids would pass or fail on
interning order rather than on the document. `atom_table::text()` returns an
empty view for an id past the end rather than throwing, so resolving one
document's atom through the other's table would fail *open* — both sides come
back `""` and compare equal.

It does not compare anything the viewport decides. `node` stores no layout rect,
which is what makes Principle 6 checkable rather than aspirational.

**The positive case is one line and the negative cases are the file**, because a
comparator that is too lenient does not fail to catch a bad blueprint — it
certifies one. Eight mutations must each be noticed, and all eight were re-run
against a deliberately blinded comparator to prove they fire. Two are worth
naming: an element in the wrong namespace, because an SVG `<title>` and an HTML
`<title>` intern to the *same atom*; and one element attached in two places with
every child count still matching, which is the only case the visited-set guard
can catch and is the shape a loader with a shared-subtree optimisation actually
produces.

### CSS: the parse is readable, the compile is not

The style inventory came back with a **blocker for 16B rather than a
comparator**. Everything `parse_stylesheet` produces can be read and compared
with total fidelity — the token stream, the component-value tree, rules,
selector lists in compiled form, specificity, the declaration triple plus its
sheet-wide `order`, the media-condition tree and `@font-face`.

What `engine::add_sheet` then *files* cannot be read at all. The only windows on
it are `selector_count()`, `rule_count()` and `page_fonts()`, so **origin, the
sheet-local to engine-local condition remap, and bucket assignment are all
unobservable**. A comparator built today verifies the parse completely and
almost nothing about the compile, and the end-to-end alternative — `resolve()`
on a pinned environment — cannot distinguish a rule filed in the wrong bucket
from one that simply never matched, while dragging viewport-dependent state into
an acceptance test that must not compare viewport-decided things.

**So the enumerator came first.** `engine::for_each_rule` now yields every filed
rule with its bucket, bucket key, specificity, property, value bytes, order,
condition ordinal, origin and importance — ordered by (source order, selector)
rather than by bucket, because the buckets are unordered maps keyed by atom id
and an atom id is handed out in first-interning order at run time. Source order
is a property of the stylesheet and of nothing else.

`ctcompile::css::compare` reads it, and its seven negative cases all share one
property: **every one leaves `selector_count()` and `rule_count()` untouched**
while changing what the page looks like. A sheet filed under the wrong origin, a
changed value, a lost `!important`, a rule whose rightmost compound moves it to
another bucket, a changed specificity, a rule that lost its `@media` gate, and a
changed `@font-face` source. The test asserts the counts still agree *before* it
asserts anything else, so a case that drifted into being caught by a count
reports itself as no longer testing what it claims.

### A live engine gap, found by writing the test

The `@font-face` case failed at first, and the cause was the fixture rather than
the comparator:

```css
@font-face { font-family: Fira; src: url(f.ttf) }        /* records NOTHING */
@font-face { font-family: "Fira"; src: url("f.ttf") }    /* records one font */
@font-face { font-family: "Fira"; src: url("f.ttf") format("truetype") }  /* NOTHING */
```

**Fixed.** The engine searched a declaration's reassembled *text* for a literal
`url(` — but `url(` with an unquoted body is its own token and a quoted one is a
function plus a string, so the two spellings share nothing by the time a
declaration is text again: `url("f.ttf")` keeps its `url(` and `url(f.ttf)`
comes back as bare `f.ttf`. Every unquoted src therefore produced an empty
source and the face was dropped, silently, for the spelling most stylesheets
use. It now reads the tokens instead, which makes the two forms one case, and
`style_basics` pins all seven spellings including `format()`, a `local()` first
in the list, and whitespace inside the parens.

The immediate lesson is smaller and sharper: the case had been comparing *no
fonts against no fonts* and passing while proving nothing, which is the exact
failure the negative-case discipline exists to prevent.

Three parse-side traps the inventory turned up, all of which a blueprint can get
wrong silently: the parser appends selectors and condition entries **before** the
rule is accepted and never rolls back, so orphans exist that a rule-keyed walk
cannot see; `source_length` is the length of the **preprocessed** buffer, so a
blueprint slicing original bytes disagrees on every CRLF-authored sheet; and
`declaration.order` is dense across the whole sheet, which is the only trace of
what the parser dropped.

**Sequencing, per the ground truth's warning that serializing a moving
representation guarantees churn:** settled and safe to serialize now are the
tokenizer, the value tree, the rule/selector/declaration structure and the
condition tree. Still moving, and excluded: the value representation (S3b is
open — serialize value *bytes*, never the container), cascade output (`revert` is
folded to `unset`, shorthand expansion is incomplete, `::before`/`::after` are
S6), and at-rule coverage — S5 still owes `@supports`, `@layer`, `@charset` and
`@import`, and when that lands **condition ordinals shift with no field changing
shape to warn you**.

### The baseline, and what it deliberately leaves out

`tools/check/baseline.py` records `ctcompile/docs/baseline/startup.json`: what a
packaged application pays before it draws anything, per stage, with the CPU,
cores, RAM, OS, compiler, build flags and commit beside every timing — because a
baseline without its configuration is unusable six months later.

| corpus | stage | ms |
|---|---|---|
| babylon 11.6 MB | parse + compile to bytecode | **264** |
| phaser 8.8 MB | parse + compile to bytecode | **70** |
| p5 4.6 MB | parse + compile to bytecode | **53** |
| bootstrap.css 298 KB | parse | 2.6 |
| bootstrap.css 298 KB | parse **and file into the engine** | 2.9 |
| kitchen fixture 10.7 KB | HTML parse + DOM build | 0.07 |

The shape of the problem is in that table: **JavaScript is where startup goes**,
by two orders of magnitude over HTML, and filing a stylesheet costs about 13% on
top of parsing it.

It says what it does not measure, in the file. Style resolution, layout, paint,
raster and the first frame are absent on purpose — they stay runtime work by
Principle 6 and the engine already benchmarks them. And the one stage that does
not mean what it says is marked as such: running p5's top level "in 0.037 ms" is
a bundle that raised on its first statement, because a bare `script::context` has
no `Symbol`. Each stage records whether it *completed* and why not, so the file
carries the `TypeError` rather than a timing that flatters the engine.

### Three inventories that reduce to standing decisions

The plan also asks for JSON, text-stack and layout/SDL3 inventories. Read
closely, each is a *constraint* rather than a table, and recording them as
decisions is more honest than a document nobody will check:

* **JSON** — the existing implementation stays. Do not replace it with simdjson
  as part of this project; change it only for correctness or a measured need.
* **Text** — SDL3_ttf is the shaping and measurement stack. Do not add HarfBuzz
  or ICU without a concrete unsupported requirement to point at.
* **Layout/SDL3** — viewport → style → layout → paint → raster → present stays
  runtime, entire. This is Principle 6 restated as a pipeline, and both
  comparators already refuse to compare anything downstream of `style`.

---

---

## S0: where the time actually is, and what that means for this project

Phase 10A opens with a measurement that decides the backend ordering. Half of it
is done, and on the way it turned up the single most important fact about this
project's premise — which is not about backends at all.

### The interpreter is not where the time goes

This repository's own profiling, in `ctbrowser/docs/performance.md`:

| whole p5 page load | share |
|---|---|
| `ctjs::vp::lex` | 17.5% |
| `compiler_impl::declare_local` | 15.1% |
| `compiler_impl::collect_captured_names` | 7.6% |
| **`context::run_loop`** — *the entire interpreter* | **1.4%** |

| `phaser_invaders`, a running game | share |
|---|---|
| `context::run_loop` | ~22% |
| `context::lookup_property` + `memcmp` | ~14% |
| `ctjs::vp::lex` | ~10% |
| `canvas_context::blend_span` | ~7% |

**Roughly 40% of a page load is READING JavaScript and 1.4% is executing it.**
That cuts both ways and it should shape the whole project:

* It is a powerful argument FOR the compiler, and for the half of it nobody
  argues about. Parsing and compiling is exactly what an image removes
  completely — 264 ms for Babylon, 70 for Phaser, 53 for p5, every single start.
* It is a hard CAP on the other half. Compiled code still calls
  `lookup_property`; on a running game the interpreter's own dispatch is ~22%,
  so even an impossible zero-cost dispatch buys ~22% of a frame, and ~1.4% of a
  page load.

So the throughput gate must not be `bench_script`, which is ~100% `run_loop` by
construction: a 1.5× there is an Amdahl-bounded sub-12% on a real Phaser frame.
**The value of this compiler is overwhelmingly in what it deletes from startup,
not in what it speeds up at run time** — which is what the plan's own framing
("runtime-ready structures", "startup representations") already said, and now
there are numbers behind it.

### The compile-time half, measured

EmitC carries a build-time risk the LLVM backend does not, because every corpus
is one bundle and Babylon is 31,905 functions in one program. Measured on
EmitC-shaped C++ (`ctcompile/utils/s0-emit.py`), recorded in
`ctcompile/docs/baseline/s0-compile-time.json`:

* **~0.72 ms marginal cost per function**, stable from 4,000 to 16,000, so
  Babylon extrapolates linearly to **~23 s at 8-way**. Affordable.
* **Partition coarsely.** 4,000 functions cost 9.09 s in 32 translation units
  and 2.88 s in 8 — every extra TU re-pays the header. Enough TUs to fill the
  cores, and no more.
* **`value.hpp` costs 1.175 s per TU, and 0.940 s of that is Boost**, reached
  only because `bigint_object` holds a `cpp_int` **by value**.

**DECIDED 2026-08-20: `value.hpp` is not being de-Boosted.** Moving that member
out of line would have removed 0.940 s from every consumer, the engine's own
translation units included, and it is not being done — so Phase 10A pays the
header and partitions coarsely instead. That is affordable and the numbers above
are why: at 2,000 functions per TU the header is about 10% of the build, and
Babylon lands near 23 s at 8-way. The generated code therefore INCLUDES THE
RUNTIME HEADERS rather than a slim prelude, which also keeps the 31 leaf helpers
— the ones with no throw, no re-entry and no safepoint — inlinable at `-O2`.
That inlining is the entire irreducible advantage EmitC has over a frozen C ABI,
and a slim prelude would have traded it away to save a term that coarse
partitioning already makes small.

What this does **not** decide is the ordering: there is no LLVM arm, and both
backends run the same optimiser over the same functions. The only term EmitC
pays alone is the frontend, and `clang -ftime-trace` is what separates it.

---

## Phase 15: the program image, which is where the time actually is

The profiling above says it plainly — parsing and compiling JavaScript is ~40% of
a page load and executing it is 1.4% — so the largest number this project can
delete is the one in the baseline: **264 ms for Babylon, 70 for Phaser, 53 for
p5, every single start.** A serialized `script::program` removes it, and needs no
MLIR to do it.

Measured before designing the format, not after
(`ctcompile/docs/baseline/program-size.json`):

| corpus | source | image | |
|---|---|---|---|
| p5 | 4.5 MB | 3.0 MB | 66% |
| phaser | 8.6 MB | 3.8 MB | 44% |
| **babylon** | **11.3 MB** | **15.7 MB** | **139%** |

**An image is not automatically smaller than the source it replaces.** Babylon's
31,905 protos carry 9.98 MB of eight-byte instructions on their own.

**Pool the names.** Babylon's 166,396 name entries are 24,172 unique — 2686 KB
collapses to 495 KB, Phaser 553→96, p5 333→68. That is a v1 decision rather than
a later optimisation, because it changes the format. String literals do *not*
duplicate that way (2050→1840 KB), so the effort belongs on names.

### `program::source` is kept unless optimisation is asked for

Retaining the source doubles the image — Babylon goes to 27 MB, 2.4× its own
source — and dropping it breaks `f.toString()`, which is not academic: p5's error
system reads its own source, and an engine with no answer there cannot run it.

**So the default keeps it, and dropping it is something an explicit optimisation
request buys.** A plain `ctcompile <app>` produces an image that behaves exactly
like the interpreted page. Asking for optimisation permits the compiler to drop
the source, and the cost is stated where it is chosen rather than discovered
later: `f.toString()` degrades, and a library that reads itself may stop working.
The image records which of the two it is, so a loader never has to guess.

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
