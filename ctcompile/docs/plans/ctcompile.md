# ctcompile: an application directory in, a native executable out

**Where it is. The repository is a monorepo, `ctcompile` builds beside the
engine, and PHASE 0 IS COMPLETE: six inventories the build checks, two
differential comparators written before the things they will accept, and a
recorded startup baseline. PHASE 2'S GATE IS MET - a hand-authored
compiled function runs through the real ABI, and doing it falsified a row. PHASE
15 IS WORKING: a page is handed its scripts
already compiled, one per `<script>`, which is **71% of a p5 page load** and
**3.5x on the edit-one-script case that used to cost a full recompile**. It compiles nothing
of its own yet. 97 of 97 tests pass.**

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

### The hash on the page path, and a hash that was fast and wrong

`image_source_hash` is the field that decides whether a cached image is *this
page's* — an image built from other source is not a slow path, it is different
JavaScript running at full speed. It was FNV-1a a byte at a time, and it was
**4.16 ms of a 65 ms p5 page load**, which nobody had noticed because it is
called from `browser::run_scripts` rather than from anything named like a hash.
It is now `boost::hash2::xxhash_64` and costs 0.181 ms.

**The obvious fix was measured, committed to in a working tree, and is wrong.**
FNV-1a widened to four lanes of 64-bit words is 0.127 ms — faster than what
shipped — and it collides on **50,678 of 262,145 single-byte edits of real
p5.js**. One edit in five. The reason is structural rather than a bug: FNV's
round is `h = (h ^ w) * prime`, and a multiply carries a difference only
*upward*, so a change to a word's most significant bits leaves the lane
differing in its top three bits and nowhere else — and it is the *same* three
bits wherever in the lane the change happened. Two different edits reach an
identical accumulator. A byte at a time does not have the problem, because a
byte enters at the bottom where the multiply can spread it. **Widening it is
what broke it**, and every round-trip test passed the whole time, because the
hash is not what is round-tripped.

Rotating inside the round removes the collisions and replaces them with a magic
number. Swept across all 63 rotate amounts on the same corpus, **51 collide with
FNV's prime and 49 with a dense one**, and the amounts that work do so by
dodging a property of ASCII — bit 7 of a byte is never set. A constant that is
good because of the corpus is a coincidence with a test suite in front of it.

So the algorithm is somebody else's and **so is the code**. XXH64 leaves no
constant to choose and was tested against SMHasher rather than against p5.js. It
was first written out here by hand, forty lines, and that version was deleted in
favour of Boost.Hash2's: a hand transcription of a published algorithm is
exactly the code most likely to be quietly wrong, and this is the entry that
stopped that argument being hypothetical. `ctbrowser-script` already links
`Boost::headers`, the header costs 0.02 s in that translation unit, and it
measured 0.181 ms against the hand-written 0.183. The floor moves 1.80 → 1.88,
where Hash2 arrived; 1.80 had been nominal for some time, since Boost.URL —
required as a COMPONENT on the same line — did not exist until 1.81.

**The known answers are not ours.** The test pins seven values from
`xxhsum -H64`, Yann Collet's own tool, two of which are the vectors XXH64's
specification publishes. Nothing in this repository computed them, so they check
three things a self-generated table checks none of: that the hash really is
XXH64 at seed zero, that a big-endian host would fail rather than quietly
disagree, and that the algorithm cannot change without `source_hash_algorithm`
changing with it.

That tag goes in `image_fingerprint()` and not in `format_version`, because the
byte layout did not change — the *meaning* of one field did — and the version
check runs first, so a version bump would refuse an old image with a message
about a format that is in fact identical. **It is a message rather than a safety
net**, and it is not covered by a test: producing an image carrying the old hash
needs the old build. The test pins the precondition instead.

### What caught it, and what a whole-page number cost to believe

The case that caught the four-lane FNV is one paragraph long: flip every byte of
a 200-byte source in turn and require 201 distinct hashes. It is in the file
beside three deliberately blinded hashes — four interchangeable lanes, a lane
reading its neighbour's word, and the four-lane FNV itself — and each negative
case asserts that its blinded control *does* collide, so a case that stops
proving anything says so instead of passing quietly.

`docs/baseline/page-load.json` is re-recorded: **66.80 ms to 18.97 ms, 72% of a
p5 page load**, against the 53% it held at `e4aed22`. Two things about that
number are worth more than the number:

* **The saving is attributed by an A/B of two binaries, not by subtraction.**
  The from-source arm also moved — 72.59 ms to 65.38 — and *no commit explains
  it*; nothing on that path changed. Subtracting the old file's numbers from the
  new ones would have credited this work with variance on a shared VM. Two
  builds of this tree differing only in `image_source_hash`, run alternately,
  say 23.7 ms against 19.8, which agrees within 0.1 ms with the hash measured
  alone.
* **A measurement was taken from a stale binary and nearly recorded, and the
  first explanation for it was wrong.** The first reading after a full remote
  build reported 23.9 ms for a tree that hashes in 0.181. That was blamed on
  `rsync -az` preserving mtimes. **It is not that.** `ctpageload` was
  `EXCLUDE_FROM_ALL`, so `cmake --build` never built it at all, and the binary
  in the build tree was the *previous session's* — which is exactly why its
  number matched the A/B's old arm to the hundredth of a millisecond. Proven by
  touching one engine `.cpp`, running a full default build and watching the
  executable's mtime not move.

  **This tree had been caught by the same mechanism twice before.**
  `docs/history/computed-goto.md` withdrew a computed-goto result because the
  benchmark "compared two things neither of which was computed goto";
  `docs/performance.md` records the same run as invalid. Both write-ups end by
  telling the next person to check the binary, and that instruction then failed
  a third time. So it is a build edge now rather than a fourth paragraph:
  `ctcompile/tools/CMakeLists.txt` grows a `ctcompile-tools ALL` aggregate —
  the idiom `ctbrowser-tests` already used to keep EXCLUDE_FROM_ALL test
  executables current — and both tools are verified to relink on a plain
  `cmake --build`. It costs 3.1 s of a 4.1 s incremental build. The engine's own
  benchmarks still have the defect and are left alone here.

### Validation is 15% of an image load, and a table beat a fast path

The operand pass — every index in every instruction checked against the pool it
addresses — is **3.0 ms of a 19.9 ms image load**, measured by building the
loader without it and interleaving the two binaries. That is the third-largest
number left on this path and the reason it is worth touching at all.

Two things were tried. **The first was wrong and is recorded because it was
wrong**: the bounds (`fn.constants.size()` and friends) looked loop-invariant
and reloaded per instruction, since `in.fail` takes a `std::string` and is
opaque to the optimiser. Hoisting them into locals changed nothing — 19.79 to
20.17 against 19.62 to 20.47, ranges fully overlapping. The compiler was already
doing it, and the guess about where the time went was simply wrong.

**What worked was deleting the branch, not the check.** `check_slot` switched on
the operand's kind three times per instruction, and an opcode's kinds arrive in
whatever order a program was written, so that is three indirect branches the
predictor cannot learn, half a million times for p5. One bound per kind in a
nine-entry array turns the whole thing into a load and a compare: **19.18 ms
against 19.74, faster in 15 of 15 paired runs**, recovering about a fifth of the
validation cost. The kind-specific message is still built by a switch, on the
failure path, where a branch costs nothing.

**Nothing about what is checked changed**, and the point is that this is
demonstrated rather than asserted. The kinds the switch ignored — `count`,
`jump`, `bx_hi`, `unused` — get a bound of `UINT32_MAX`, so their compare is
false by construction rather than by omission; and a wide operand gets its own
table, because the switch it replaced had no `reg` case and fell through its
default, so sharing one table would have made the validator quietly *stricter*
than the code it replaced. That is the kind of change that passes every test and
starts refusing valid images.

The evidence is differential: 60,000 random one-to-three-byte mutations of the
real 7.3 MB p5 image, through both builds, digesting **every verdict and every
error string**. Same digest, `b86379232d75ecb1`, on 36,653 acceptances and
23,347 refusals. A suite that goes green proves the cases in it still pass; this
proves the two implementations are the same function.

**The fast path in the handoff was not implemented, and should not be.** The
proposal was to skip the per-operand switch when a function's tables make every
index trivially in range — which requires the minimum of five bounds, and that
minimum is *zero* for any function with an empty constant pool. p5 has 23
functions with no pools at all. It would have bought nothing on exactly the
functions it was cheapest to check, and it weakens a validator to do it.

### One `<script>`, one program — and the three defects that bought

The image was keyed to a whole page's concatenated classic scripts, which made
the compiled form of a page a single artefact: **editing a 473-byte sketch threw
away the image for the 4.5 MB library beside it.** Each `<script>` is now its own
program, run in document order on the one shared context — the shape modules
already had — and each consults the image cache on its own bytes.

| p5-basic.html, three classic scripts | ms |
|---|---|
| from source | 69.65 |
| from images | 19.93 |
| **edit the sketch only** | **19.77 — 3.5×, 1 of 3 scripts recompiled** |

That last row is the one that could not exist before. **Splitting itself costs
nothing**: three separate compiles are 50.61 ms against 51.44 for the
concatenation, and the images total +0.005%. There was no whole-page pooling to
lose, because a classic script's top level compiles to `set_global`/`get_global`
by name and `predeclare_locals` is deliberately not called for it.

**It is a conformance fix first and a caching change second**, and that half is
worth more. Measured against the old engine: a parse error in script A used to
stop script B, and no longer does; an uncaught throw likewise; a promise
resolved in script A used to have its handler run after the *last* script and now
runs before script B, because each script is its own microtask checkpoint. All
three are what the specification asks for. What was lost is a call in an earlier
script to a function declared in a later one — 42 before, a throw now, and a
`ReferenceError` in Chrome, so concatenation was the outlier.

### The review found three things wrong with it, and one older thing

A five-lens adversarial fan-out over the uncommitted change returned thirteen
confirmed findings. Three were the split's own, none visible from a rendered
page, each now with a test watched failing without its fix: `script_sources()`
listed contributions the loop then skipped, so a packager would have built images
nothing could look up; a missing `<script src>` **masked every later error**,
because the walk writes its complaint into the field the new first-failure-wins
rule reads; and the compile counter never reset, so a second load reported the
first load's misses against a per-load denominator.

**The fourth was a use-after-free older than the split.** A script can navigate
synchronously — `element.click()` reaches the embedder's navigate hook, and
ctbrowse's hook calls `load_html` — which reset the context and freed the
programs *while the interpreter was still executing one of them*. With one
program there was nothing left to run afterwards, so it survived; with N it also
let the abandoned page's remaining scripts read and overwrite the document that
replaced theirs. `load_html` now queues a re-entrant navigation and performs it
once the scripts stop, which is what a browser does anyway. Removing the queue
makes the test SIGSEGV.

### A dead script's `try` caught the next script's `throw`

The sharpest finding, and the split is what made it reachable rather than what
introduced it. `context::execute` cleared `frames_` and not `handlers_`, and a
VM-level `raise` does not unwind — the loop condition is
`frames_.size() > stop_depth && !failed_` and it simply stops. Every `try` live
at that moment stayed on the handler stack recording frame 0. **A second
top-level program has a frame 0 too**, so `unwind_to_handler` accepted the dead
program's handler, wrote the thrown value into the wrong frame's register, and
set `ip` to an address out of the *other program's bytecode*.

Measured on a page whose first script exhausts the stack inside a `try` and whose
second alerts 1–8 then throws: `12345678345678` — the throw swallowed, six
statements run twice. Padding the first script's `try` body walked the
resumption point through the second script, which is what proves where the
address came from. Not memory-unsafe — `ip` is bounds-checked and the register
file resized — just silently wrong, which is worse to find.

### And the CSS parser had been reading freed memory the whole time

The lens told to find a use-after-free in the new code found none, and reported
this instead. `emit_declarations` iterates a `std::span` **into** `sheet_.values`
while every declaration it emits appends to that same vector; the first
reallocation frees the buffer the span points at and the next iteration reads it.
It has always worked, because the freed bytes still hold the old values.

**The measure of it is that this repository's own `asan` preset could not run:
29 of 52 tests failed, all of them this, aborting at browser construction —
which means every page this engine has ever loaded did it.** It is 52 of 52 now.
The fix is one copy, and it costs 4% of a bootstrap.css parse (2.44 → 2.55 ms,
interleaved, losing all five pairs). The first version of that comment said it
was free; it is not, and the number is in the file.

### `finally` ran on two ways out of a try block, and JavaScript has five

Scoping Phases 1 and 3–6 with a fan-out produced three orderings that disagreed,
and the one that argued for **doing the least** was right: it pointed at a live
defect in the reference implementation rather than at scaffolding for a backend
that does not exist. `compile_try` emitted the finally block twice and let every
other exit leave without it. Measured against what JavaScript specifies, **six of
nine cases were wrong**, and the worst discarded an exception outright —
`try { throw x } finally { }` sent the throw to a catch path with no catch,
ran the finally and fell through, with no error anywhere.

It is now a completion record and one copy of the block. Nesting falls out for
free because the dispatch re-emits the *same* lowering the statement would have
emitted, so a `return` crossing three finallys is handed outward one at a time
and no finally knows how deep it is.

**Two defects in the fix were mine and neither was found by reading.** A
`return` inside a nested function was routed into the *enclosing* function's
open finally — pushing a jump onto its arrival list to be patched into another
proto's code array — which `compile_function_body` already guards against for
`optional_exits_`, with a comment saying why. And a `break` to a loop opened
*inside* the try was routed through a finally it never crosses, so the dispatch
re-emitted a jump to a loop already popped: `loop_index 1 of 0 loops`. **Both
were found by the asan preset**, the first as a leak in the compiler — which is
what an arrival list that never gets patched looks like from outside.

**The corpus moved, which is what a ratchet is for.** p5_api went 172 → 175:
three advances recorded deliberately, because p5's
`try { … } finally { this._inUserDraw = false }` now actually resets. The fourth
change was a probe that had been passing *because* the engine swallowed
exceptions — it called `curveDetail()` on a 2D canvas, where p5 throws on
purpose. It now requires that throw, which turns an accidental pass into a
deliberate test of the thing that was broken.

### And the fingerprint could not see any of it

That rewrite changed the bytecode of every `try/finally` in every program and
changed **not one opcode**, so `image_fingerprint()` did not move: an image
written that morning, carrying the broken `finally`, would have loaded into the
fixed build and run at full speed. `opcode_set_identity` says what the opcodes
*are* and nothing said what the compiler *emits*.

So the fingerprint now **compiles a canary and hashes the result** — thirty
lines covering closures, classes, generators, async, destructuring, optional
chaining, labelled break, switch, for-in, a BigInt and three shapes of
try/finally — folded over every instruction's opcode and operands and each
function's frame shape. A version constant would have worked, and this
function's own comment already argued against one: *a version says what someone
remembered to bump*. Demonstrated on the change that motivated it: with the fix
in place `1b0fb1310f6b5265`, with it bypassed `6b239364b59b87b2`, and an image
written by one refused by the other.

### A 32-bit operand that three sites made 16 bits wide

`op::closure` names its target with `with_bx`, which takes a `uint32_t`. Three
of the four sites emitting it cast the index to `uint16_t` first — not a bound,
a **wrap**. Measured: a program of 70,001 functions called function 69,999 and
ran function **4,463**, with no error from the compiler, the VM or the image
validator. Babylon, vendored here, is 31,905 functions — **49% of a ceiling the
encoding never had.** Deleting the three casts is the whole fix; 140,001
functions now compile in 34 ms and call what they name. The image's own 65,535
refusal went with it, replaced not by a bigger constant but by the arithmetic
the pools already use: a count checked against the bytes remaining.

### Phase 2's table is done and Phase 2's gate was never met

Scoping the next phases turned up a claim worth checking: the record says Phase
2 is complete, and the plan's gate for it is *"VM code calls a hand-authored AOT
closure through the real runtime ABI."* Nothing has ever done that. There is no
helper body, no native entry on a `function_proto`, no `lib/AOT/`. That is a
legitimate position — `aot.hpp` says so itself, "NO DEFINITIONS YET … a contract
is useful before its implementation exists" — but "done" was the wrong word for
it, and the handoff now distinguishes the two.

**Worse, the runtime had never compiled its own contract.** `aot.hpp` expands
the table into an enum and 68 prototypes, and the only file in the repository
that included it was `ctcompile/test/Inventories.cpp`. The presets that most
need the check are precisely the ones that skip it: `browser`,
`browser-no-llvm`, `asan`, `tsan` and `windows` all configure with
`CTBROWSER_ENABLE_PROJECTS` empty, so ctcompile is not built and **neither the
ABI nor `EngineContract.hpp` is parsed at all** — one configuration in six
checked any of it. Verified in the asan build tree, which has no `ctcompile/`
directory.

`ctbrowser/lib/Script/aot_contract.cpp` is a translation unit of nothing but
`static_assert`s, inside `ctbrowser-script`, so all six now compile it. Each
assertion was checked to **bite** by mutating the table on the devbox: changing
`ct_aot_check`'s return column fails the classifier pin, and making
`ct_aot_truthy` return `int32_t` fails the return-type rule with the error
anchored at `aot_helpers.def:662`, naming the row.

The rule it enforces is one the table stated only in prose: **a signed result is
a status and a status carries a frame handle**, over all 68 rows, with the one
documented exception — `ct_aot_to_int32`, *"a signed int32 return that is DATA,
not a status … it takes no frame handle, which is the mechanical tell."* Twenty-
five rows return `int32_t`; twenty-four take a frame.

And the completion vocabulary exists. `CT_AOT_OK`, `CT_AOT_FAILED`,
`CT_AOT_UNWOUND` and `CT_AOT_CAUGHT` were cited 35 times and defined nowhere, so
24 prototypes returned a bare `int32_t` whose meaning lived only in prose while
the table already assumed ctcompile would `switch` on it. `ct_aot_status` now
derives its underlying type *from* `ct_aot_check`'s prototype. **The precedence
is the contract; the numbers are not** — the table fixes the order a classifier
tests in and fixes no integer, so none is invented here. `CT_AOT_PAD_BIT` and
`CT_AOT_FRAME_BYTES` stay undeclared on purpose: both are Phase 4 layout
decisions, and freezing either on no evidence would put a guess in a header two
backends read.

A fan-out of 28 agents proposed the assertions and then tried to kill them; the
verify pass earned its keep by **rejecting several as tautologies**, one of them
proved unfalsifiable by mutating the table and watching the assertion pass
anyway. What survived is above. One correction fell out of it: the coverage is
83 of 93 opcodes, not 84 — there are 84 `CT_AOT_COVERS` rows because `type_of`
is served by two helpers deliberately, which is the case the union rule exists
for.

### What is left of a page load, and why 16A and 16B are not next

`page-load.json` said the residual after an image was *"HTML parsing, CSS,
style, layout and the image load itself."* Profiled, in instructions, on the
image-loaded path only:

| | share |
|---|---|
| **image loader** | **26.0%** |
| unattributed | 18.1% |
| interpreter (`run_loop`) | 17.4% |
| property and object access | 12.0% |
| libc memcpy/memset | 9.6% |
| allocator | 8.2% |
| regex | 6.4% |
| source hash | 1.7% |
| **CSS and style** | **0.5%** |
| **DOM and HTML parsing** | **0.0%** |
| layout, paint, raster | absent |

**HTML parsing, CSS, style and layout are under one percent of the work between
them.** What remains is the *loader*, and p5's own top level running — the
interpreter, property access, the allocator and the regex engine come to about
44%, which is a 4.5 MB library building its API surface.

That answers a question the master plan's ordering assumes. **Phase 16A compiles
a DOM blueprint and 16B a style program; on this page they target the 0.5%.**
Worth knowing before building either, and it needed nothing built to find out.
It says nothing about a page that is mostly markup — and everything about the
corpus this project's headline number comes from.

It also moves the target. **The loader is now the largest single thing on the
path**, which it was not while the compile dwarfed it, and its operand pass
alone is 7.49% — fifteen times what the whole CSS engine costs.

**Measured without measuring the compile**, which is the trap this project fell
into once already: `ctpageload` builds the images it hands over, so the image
arm is a named `noinline` function and callgrind collects only inside it. The
metric is instructions, not time, and that is why the interpreter's 17.4% here
does not contradict the 1.4% quoted from `docs/performance.md` — that figure
measures a page load which *includes* compiling 4.5 MB of JavaScript. Neither
licenses a claim about what an AOT backend would save in milliseconds.

### Two images of one script, and the order they arrived in

An image keeps `program::source` or drops it, and both are valid images of the
same text with the same hash and kind — so they collide in the cache, and they
are not interchangeable: one makes `f.toString()` return the function's text and
the other `"[native code]"`. `read_image_header` reported the hash and the kind
and deliberately not the option, so the browser could not tell them apart and
took the last one. Measured both ways: **keep-then-drop degraded `toString`,
drop-then-keep did not.** Same inputs, different answer, decided by call order.

The one that keeps the source now wins whichever arrives first — not because
last-writer-wins is unreasonable but because it is *order-dependent*, and
because dropping the source is an optimisation that removes behaviour.
`clear_script_images` is how a caller says it means the lean one.

### Phase 2's gate, met — and the row it falsified

The AOT ABI had been specified since Phase 2 and **not one line of it had ever
run**. The gate is *"VM code calls a hand-authored AOT closure through the real
runtime ABI"*, and a table nobody executes is prose however good — with 68
helper bodies and two code generators still to be written against it.

Four rows now have bodies, chosen because a non-throwing call needs exactly
those four: `ct_aot_enter`, `ct_aot_leave`, `ct_aot_check`,
`ct_aot_return_value`. A hand-written body for `function addup(a, b)` is stamped
onto the proto the engine's own compiler produced, and `addup(41, 1)` from
ordinary interpreted JavaScript reaches it through one branch in `op::call`.

**The answer is not the evidence.** Both arms return 42, so a dispatch that
never fired would look identical. The body counts its own calls and the test
asserts that counter — disabling the branch leaves every *value* assertion
passing and fails only the counters, which is how it was checked.

**And it falsified a row, which is the whole reason to do this first.**
`ct_aot_catch_land(fr, out_thrown)` is specified to read back
`registers_[call_frame::base + handler::slot]` — but `unwind_to_handler` **pops
the handler before it writes**, and `call_frame`'s twelve fields do not include
the slot. By the time a compiled body could ask, the register the thrown value
went into is unknowable, and the signature has nowhere to learn it from. Two
fixes exist, both ABI changes, and neither was taken: choosing between them
without a compiled `try` to test against would be inventing on no evidence. It
is marked in the table.

So the throwing tier is **not** here. Three rows executed and the fourth
reported beats four bodies one of which quietly does something other than its
row says — and `ct_aot_check` therefore never returns `CAUGHT`, and says so
where the test would be.

Three decisions the rows left open are now taken with evidence: the **entry
signature** (the table specifies what compiled code may *call* and never what
the runtime calls back — the first ABI here that is not a row, obeying the rows'
own status-in-a-register convention); **`CT_AOT_FRAME_BYTES`**, left undeclared
two days ago because nothing had measured it and now 64, `static_assert`ed
against the 32-byte handle it sizes; and **where the entry hangs** — on
`function_proto`, because an entry belongs to the code, so one stamp is
inherited by every closure built from it.

Measured, because the interpreter is on every page: the dispatch is a
predictable not-taken branch on a field in the same eight-byte word as
`param_count`, `frame_size` and `is_generator`, all three already loaded by that
handler. **−0.07% instructions on `bench_script`** — inside the noise, in the
faster direction. 98 of 98, and 53 of 53 on asan, which matters more than usual
for a change that pushes a real `call_frame` and resizes the register vector.

## An application directory in, an executable out

```
ctcompile app/ -o myapp        then ./myapp
```

Phases 18–20 put "the generated launcher" near the end of the ladder, and the
note beside them says the launcher is a fixed library rather than generated C++.
Taking that seriously makes the whole of it available NOW, because if the
launcher is fixed then packaging is not compilation: it is a file copy.

`ctrun` is an ordinary tool in `ctbrowser/tools/`. A packaged application is a
byte-for-byte copy of it with an application bundle appended and a 24-byte
trailer saying where that bundle starts — a linked ELF does not care what
follows its last section, verified by appending 4.5 MB to `ctbrowse` and finding
it ran with a byte-identical screenshot. So the machine that RUNS the result
needs no toolchain, and the machine that BUILDS it needs no linker.

The bundle is `ctbrowser/{include,lib}/…/app_bundle.*`: magic, format version,
**the engine fingerprint**, a table of `(kind, name, offset, length)` and a
payload. The fingerprint is the same one the program image carries, and it is
checked before anything else is read — a bundle whose images were compiled by
another build of this engine describes different instructions with the same
bytes, and refusing it here says so, rather than leaving one layer down to
complain about opcode numbering.

Measured on the devbox, p5-basic.html, seven runs each, whole-process wall clock
including startup and rendering a frame:

| | ms |
|---|---|
| `ctbrowse p5-basic.html`, reading the JavaScript | **78.0** |
| the packaged executable, run from `/tmp` | **47.3** |

The packaged binary is 15 MB against ctbrowse's 3 MB, because the launcher reads
its own file back at every start to find its trailer. Reading 12 MB more still
wins by 30 ms, which is worth stating because "the copy costs more than the
parse it removes" is a reasonable objection and it is wrong here.

**IT ASKS THE ENGINE WHAT THE APPLICATION IS.** Which scripts a page compiles
and which resources it reaches for are decided by rules that live in the
browser. A packager holding a second copy of them is a packager free to drift,
and the drift presents as an application that quietly compiles from source and
is merely slow. So the page is loaded, headless, by the same browser that will
run it, and then asked: `script_sources()`, `module_sources()`,
`assets().requested()`.

### Eight silent defects in it, and the shape they share

An adversarial review of the packaging path found eight. Every one of them left
an application that RAN and produced the right document — which is the class of
failure this project has repeatedly found to be the expensive one.

* **Module scripts were invisible in both directions.** `script_sources()` is
  classic scripts only, and there is no image path into `load_module`. A page of
  modules packaged as "0 scripts compiled" and the run-time guard that asks
  whether packaging worked read a truthful zero, because zero classic scripts
  compiled from source is trivially true when there are no classic scripts.
  Mixed pages were worse: one classic `<script>` made the image list non-empty,
  the count was zero, and the module half still parsed at every start.
* **The guard was gated on `!script_images.empty()`.** `require_script_images`
  was only ever consulted INSIDE that test, so the one case where completeness
  is most obviously violated — a bundle with no images at all — was the case the
  outer `if` deleted.
* **The probe never ticked the page.** `fetch` and `img.src` QUEUE their
  requests and are drained from `tick`; p5 loads in `preload` and Phaser in the
  first game step, both inside callbacks. Asking the moment `load_html` returned
  saw the markup's resources and nothing a script wanted — every sprite, atlas
  and level in the applications this is for, packaged as a warning-free success.
  It now runs the page until it stops asking, ceiling 60 frames; p5-basic
  settles after one.
* **The packager built a second, base-less `asset_registry`** whose probe order
  (`.`, then two levels up from ctcompile's OWN working directory) differed from
  the one that had just answered the page. It could resolve a name from the
  build tree that the page resolved from the application and ship those bytes
  under the right name, silently — and it could miss a name the engine found,
  warn, and exit 0. `assets.hpp` spends a paragraph forbidding exactly this.
* **A packaged application fell back to the filesystem.** `run_bundle` never set
  `asset_path`, and an empty base still probes `.` and `../..` — so a packaged
  application missing a resource read whatever sat next to the USER, under the
  name its own document asked for, and worked on the machine that built it.
  Registries can be SEALED now: the registry and `data:` URLs, never the disk.
* `read_bundle` bounded each blob against the payload and never the total, so
  400,000 rows each claiming the whole 10 MB payload asked for four terabytes
  before any single check failed. `least_bytes_per_entry` was 20 where the
  minimum row is 24 — safe, because every read is bounds-checked anyway, but 20%
  looser than its own comment claimed. And `bundle_write_error()` was a channel
  nothing ever wrote to, behind a header promising a check that was never
  implemented and a branch in ctcompile that printed an empty string.

Six new guards, each removed and watched going red for its own message:

| removed | what went red |
|---|---|
| the seal on the asset registry | a sealed registry answered from the working directory |
| the running total in `read_bundle` | entries claiming more than the payload were accepted |
| the probe's tick loop | `late.json` was not packaged |
| `require_script_images` with no images | the launcher ran `no-images.ctapp` |
| the module refusal in the launcher | it ran `module-page.ctapp` |
| the module refusal in ctcompile | it packaged a page of modules |

The seventh, `write_bundle`'s refusal of >4G entries or a >4G name, could not be
falsified: reaching it needs a bundle no machine here can hold. It is written
and untested and that is said rather than implied.

### The newline nobody would think to look for

`ctcompile_app_bundle` pins one thing that is not about bundles at all. The walk
in `browser.cpp` appends a `\n` to every classic script's source — so a
`<script src>` and the inline text after it are two lines, and so a trailing
`//` comment terminates. It follows that a script's source is NOT the bytes
between its tags, and an image built from the text an author typed hashes
differently, matches nothing, and leaves the page working exactly as it did.

That is the whole failure mode of this feature in one character, and the test
that catches it was itself written wrong the first time — the typed-by-hand arm
went red, which is how the rule was found. Both arms are in the file now: the
one that matches, and the one that differs by that newline and matches nothing.

## Phase 1's gate, closed

"Documented CLI, manifest, identities, format versions, and functional compiler
stub." The stub stopped being a stub above; the CLI is documented in
`ctcompile/docs/ctcompile.md`; the identities and format versions already
existed and were unexposed. What was missing was the manifest.

`--manifest FILE` writes it and a copy travels in every bundle, so a packaged
application can say what it is without the directory it was built from — and
`myapp --info` prints it, which is also what stopped `myapp --help` from
silently starting the application.

`program_id` is the identity the runtime actually matches on: the hash of the
source **the engine reported**. Writing the hash of the file instead would have
produced a manifest that looks like it explains a cache miss and does not, and
the two differ by the newline the script walk appends.

Not `llvm::json`, which the master plan asks for. LLVM is behind
`CTCOMPILE_ENABLE_MLIR`, OFF until Phase 7, for the plan's own reason: a
compiler that needs a 2 GB dependency to write a JSON file is one a runtime-only
machine cannot build. Forty lines with tested escaping instead, and switching is
one file when Phase 7 turns MLIR on.

`--mode` declares Phase 1's three modes and REFUSES two of them. There is no
native code to prefer in `hybrid` and none to require in `aot-only`, so
accepting either would be accepting a flag that changes nothing — and the
refusal names the phase that implements them rather than reporting an unknown
option.

### What the manifest found in its first five minutes

The packed faces were carrying the build machine's absolute paths as their
registry names: `/home/ubuntu/projects/…/fonts/Tinos-Regular.ttf`. It worked,
because the launcher is handed the same directory out of the bundle, and it
baked a checkout path into every application it produced. They are renamed to a
fixed `fonts/` now — a prefix substitution on names the ENGINE produced, not a
second implementation of how a face is named.

That is the argument for a manifest in one paragraph. Nothing was broken, no
test could have failed, and the defect was visible the moment the artifact could
describe itself.

## Phase 3: the one line that read `aot_entry`

The gate is "arbitrary nested mixed-mode invocation works", and the plan makes
this a phase of its own for a reason it states and this rung confirmed: a call
path that cannot reach a compiled body **does not fail**. It interprets, returns
the right answer, and presents as a performance cliff under one particular
browser callback long after the code that caused it ran.

Before this, `function_proto::aot_entry` was read at exactly one line -
`run_loop.cpp`'s `op::call`. So a compiled body was reachable from interpreted
JavaScript and from nowhere else. Mapping every entry into JavaScript in the
engine found four bypasses, and they are not obscure:

| bypass | what could not reach a compiled body |
|---|---|
| `context::call` | every C++ entry: DOM events, timers, promise jobs, animation frames, `Function.prototype.apply`, getters, class field initialisers, `super()` — and one compiled body calling another, which goes through here too |
| `op::construct` | `new C()`, which also would not have passed `constructing` |
| `execute` | a program's TOP LEVEL |
| `run_reentrant` | a module's top level, when evaluated inside its importer |

The last two are the ones that matter most for this project, and they are easy
to miss because they look like plumbing. **A page's `<script>` IS a top level.**
A backend that compiles anything compiles that, so a whole compiled script would
have been interpreted while the packager reported success — the same silent
failure the packaging work spent a rung learning to refuse.

### One decision, five askers

`script/dispatch.hpp` is now the only read of `aot_entry` in the engine.
Deliberately one DECISION rather than one FUNCTION: `op::call` reuses the
caller's register window with no copying, `context::call` sizes its own, and the
three top-level entries have no caller at all — so routing them through a single
call function would have put an argument copy on the interpreter's hot path to
buy a tidier diagram.

`executing_kind` is the half of a transition that cannot be read off the call
site. `enter_compiled` is reached from the interpreter and from C++ alike, and
only the context knows whether the code doing the calling was itself compiled.
One byte, saved and restored by an RAII guard on the stack of whatever entered a
body.

`ct_aot_call` is the ABI's fifth implemented row, and it had to be: without it a
compiled body cannot call anything, so three of the six transitions could only
have been demonstrated by a test reaching around the ABI it is meant to test.

### The counters are the test

Every arm of `aot_dispatch` returns the same number whether it dispatched or
not, so the answer is not evidence and the counter is. Not behind `NDEBUG`
either: each counter sits at a boundary that already costs a vector resize or an
indirect call through the ABI, none is in the interpreter's inner loop, and a
counter that only exists in a build the suite does not run is a counter that
proves nothing.

Eight removals, each watched going red:

| removed | what went red |
|---|---|
| `op::call` reaching a compiled body | the script's own call |
| `op::construct` reaching a compiled body | `new Point(3, 4)` |
| passing `constructing` to a compiled constructor | `new` evaluating to a number |
| `context::call` reaching a compiled body | C++ → AOT, and AOT → AOT with it |
| `execute` reaching a compiled top level | a whole compiled script, interpreted |
| `run_reentrant` reaching a compiled top level | a module's compiled body |
| `ct_aot_call` | every transition with an AOT source |
| marking the interpreter as what is running | VM → AOT counted as C++ → AOT |

**Two of those arms did not exist until the removal left the suite green** —
`new` on a compiled constructor, and a module evaluating inside its importer.
That is the whole argument for removing a guard to watch its test fail: not to
confirm the guard, but to find out which case nobody wrote.

### And it costs the interpreter nothing, measured

Centralising a decision that sits on the interpreter's call path is exactly the
kind of change that quietly buys a diagram with a percent. The first version did
- `enter_compiled` was entirely out of line, so `op::call` made a real function
call per JavaScript call to ask a question whose answer is almost always no.

Split so the header holds the `aot_entry == nullptr` test inline and the
translation unit holds everything that happens when there IS a body. Interleaved
A/B of two binaries, nine pairs, `bench_script` on the devbox (no hardware
counters there, and both binaries checksummed so this is not the
`EXCLUDE_FROM_ALL` trap again):

| | ms |
|---|---|
| before Phase 3 | 1949.3 |
| after Phase 3 | 1943.6 |
| | **−0.29%, 6 of 9 pairs faster** |

Inside the noise, in the faster direction — the same result the original inline
branch measured, which is what it should be, because it is the same branch.

### Where it stops, said rather than left to be found

A generator is not dispatched. Calling one runs nothing — it builds a coroutine
— and resuming one restores a saved REGISTER WINDOW copied out of the flat
register file, which a compiled frame does not have. That is what the master
plan lists `generator_resume` and `resume` under Phase 14 for. The test asserts
the current boundary, so the phase that changes it fails here and has to say so.

## Phase 4: the collector that never ran, and what happened when it did

The gate is "forced-GC mixed-mode tests pass under sanitizers", and the master
plan calls a forced-GC mode the highest-value test in the phase. Finding out why
took one run.

**Nothing collects while script is running.** The only production trigger is
`collect_if_due`, once per tick, from the browser's frame loop, under a comment
that says "collect between callbacks, never inside one". `allocate` never
collects. So every `is_safepoint` flag in `aot_helpers.def` — thirty-three rows
— has been an obligation on callers that nothing has ever enforced, and any
value kept where the precise collector cannot see it has been safe by accident.

`context::set_gc_stress` collects the whole heap at every safepoint. Entering a
function is one.

### It found two use-after-frees on `new`, immediately

Neither is in code this phase wrote:

* **`context::construct`** allocates the instance, runs field initialisers, then
  calls the constructor body. The instance is in a **C++ local** across both, and
  both run user JavaScript.
* **`op::construct`** does the same in the interpreter with its own local, and
  `reg(in.a)` still holds the *callee* at that point, so nothing else refers to
  the instance either.

Both are genuine: an object freed while `new` is still building it. Neither is
reachable today, because a collection cannot happen there — which is exactly the
kind of latent defect the plan says this mode finds and nothing else will.

A third came out of reviewing the bridge before the mode existed: **a compiled
body's receiver was in no root at all.** The interpreted path stores it in
`call_frame::receiver` and the collector marks that for every live frame; the
bridge never set the field. Survivable by accident for an ordinary call — the
receiver is usually still in the caller's register — and not survivable for
`new`. `ct_aot_enter` takes the receiver now.

### Two mechanisms, and why each is the general one

`context::rooted` is a stack of temporaries the collector marks. The alternative
was a third special-cased field beside `current_this_` and
`pending_new_target_`, which are this same problem solved twice already, each
with a comment explaining that the value "lives only in this slot, which is
exactly the window a collection can fall in".

`ct_aot_slots` is the ABI's sixth implemented row. A compiled body has had a
register span reserved for it since Phase 2 — traced in full, initialised to
undefined — and **no way to address it**, so the only place it could keep a live
value was a C++ local. Its guard tests `base + count`, not `base` alone: an
unwound frame truncates the register file to exactly `base`, so a `>` test
passes at the moment the span ceases to exist and hands back a one-past-the-end
pointer.

### Falsified under asan, because that is where the bug is visible

A rooting bug is a use-after-free, and reading freed memory usually returns the
right bytes. Under the default preset a blinded guard mostly still passes.

| removed | under asan |
|---|---|
| the receiver rooted by `ct_aot_enter` | heap-use-after-free |
| the instance rooted by `context::construct` | heap-use-after-free |
| the instance rooted by `op::construct` | heap-use-after-free |
| the body parking its string in a frame slot | wrong string returned |
| marking the temporaries in `collect()` | heap-use-after-free |

**Two came back green, and both were acted on rather than explained away.**
`op::construct`'s root was untested because the fixture's constructor was a
plain function, so its field-initialiser run had nothing to run and could not
collect — **a class field is the only shape that opens that window**, and there
is an arm for it now. And the safepoint written into `ct_aot_call` was
*redundant*: it delegates to `context::call`, whose first act is a safepoint. It
is deleted, with the reason in its place.

The frame-chain validation the plan asks for is in `collect()` under stress and
**no test exercises it** — reaching it needs a corrupted frame stack that
nothing outside the class can produce, and a test that reached in to corrupt one
would be testing the corruption. Said in the code rather than implied.

## Phase 5: 26 of 69 rows, and the table that had stopped being true

The gate is "existing VM tests pass with shared runtime helper semantics", and
the discipline is one helper per change with the full suite green in between —
because extraction is a refactor whose whole invariant is that nothing
observable changes, and doing several at once destroys the diagnostic value of
a failure.

### The flags test found three things and the tables had answered two

The plan asks for a test "asserting that every helper's flags match its
opcode's flags". Writing it found three mismatches, and two were mine:

* `type_of` is served by TWO helpers deliberately — one classifies without
  allocating, the other materialises the string only when the result escapes —
  and its row says so: *"(0,0,0,0) here OR ct_aot_new_string's (1,1,0,1) is
  exactly type_of's inventory row"*. The rule is the **union** over the helpers
  serving an opcode.
* `await_value` is a real documented exception: the opcode's `is_safepoint` is
  the mechanical `allocates||may_reenter` derivation, and its helper covers only
  the allocation-free read half. It is **named** in the test so a second one
  cannot appear quietly, and the exemption is itself asserted so it fails when
  it goes stale.

"Match" is at-least, not equal. Overstating is conservative; understating tells
a code generator it may keep a value across a collection or skip a status test.

### Two extractions, and the differential that guards them

`context::binary_op_static` (add, and the six bitwise) and `context::binary_op`
(sub, mul, div, mod, pow, add_generic, concat). The interpreter and the ABI
helper now call the same function rather than two that agree today.

The test is a **reference implementation transcribed from the pre-extraction
handlers**, run over 10,976 operand pairs. It exists because of what a bad
extraction of fourteen byte-identical handlers looks like: they differ by one
operator each, so the plausible mistake is a transposed line, and a program that
never ORs passes a whole suite over it. Five blindings, all red — including
`concat` routed through the BigInt arm, which is the trap the row warns about:
`${1n}` would reach a switch with no case for it and throw "BigInts have no
unsigned right shift".

### Sixteen rows needed no extraction at all

The runtime already had the function; what was missing was the shim. Nineteen
opcodes bought for no risk — those changes touch no handler, so the
one-per-commit rule (whose invariant is *the VM does not change*) has nothing to
protect.

`ct_aot_ordering` **did not exist**. `ct_aot_compare`'s row names
`CT_AOT_ORD_LESS`/`EQUIVALENT`/`GREATER`/`UNORDERED` and derives all four
relational opcodes from constant comparisons against them, and nothing anywhere
defined them — the same position the status vocabulary was in before Phase 2.
Here the NUMBERS are contract, not just the precedence.

### And the table had stopped pointing at the code

A line number in a comment is a fact with an expiry date. Phases 3, 4 and 5
moved several hundred lines, and the `DELEGATES TO` citations went stale
wholesale: **six pointed past the end of a file that had shrunk** —
`run_loop.cpp` is 1,502 lines and rows cited 1,505 through 1,577 — and one named
a range that is now the middle of a different function entirely.

Repaired **as names**, not as fresh numbers: `run_loop.cpp's VM_CASE(cell_get)`
cannot drift. `ctcompile_def_citations` checks the half a machine can see and
is falsified by adding a bad citation; the half it cannot see is now stated in
the .def's header, because many citations still land inside their file while
naming a handler that has moved. Two rows also still read as open work that
Phase 5 had already done, which is how a table stops being believed.

### What blocks a minimal compiled function

`ct_aot_intern_name`. Every property helper's key is a `const ct_aot_name *`,
the row asks for an owning immortal pool that does not exist, and
`lookup_property` takes a `const std::string &`. Until that row has a body,
`o.x` cannot be emitted at all — which is why `ct_aot_get_index`, which needs no
name, is implemented and `ct_aot_get_prop` is not.

## A whole function, hand-compiled, agreeing with the interpreter

`ct_aot_intern_name` had no runtime target, and every property helper's key is a
`const ct_aot_name *` — so `o.x` was not expressible and the entire property
family was blocked. The record **owns its text**, which the row is emphatic
about: `prehashed_name` is a `{string_view, hash}`, and a pool built out of one
dangles the moment the characters go away. A compiled body's names come from an
image that may be mapped or from a literal in generated C++.

With that in place the minimal set a backend needs is complete, so the obvious
thing was to use it:

```js
function total(items, scale) {
  var sum = 0;
  for (var i = 0; i < items.length; i = i + 1) {
    sum = sum + scale(items[i].width);
  }
  return sum;
}
```

Hand-written as a backend would emit it and checked against the interpreter on
the same source — **which is the shape Phase 12A's oracle will have**. It runs
under forced GC as well, so every live value crosses a safepoint in a frame slot
and the pointer is reloaded afterwards.

### It found the safepoint in the wrong place

Phase 4 put the collection at the TOP of `context::invoke`, before the arguments
are copied into the register window. So `ctx.call(fn, args, this)` collected
while `args` was still a span the EMBEDDER owned, and any heap argument not
separately rooted was freed. The first test written against it was a
heap-use-after-free on its own array, and **the test was right**: the safepoint
belongs after the copy, where the collector traces every argument, which is also
where a real collector would run — at the point the frame it is about to enter
is describable.

### Three things the test got wrong about itself

Worth keeping, because each is a way a differential can look like it is working:

* **Two arms agreeing on NaN agree perfectly.** `is_number()` accepts NaN, and
  NaN is exactly what this fixture produces if the getter never runs. The answer
  is pinned at 396 now rather than merely compared against the other arm.
* **A blinding is worthless if it goes red for the wrong reason.** Deepening the
  getter until the slot pointer went stale also pushed the widest case past the
  512-frame guard, so the control arm failed too. Reverted.
* **One reload is not enforced, and the test says so where it happens.** A probe
  showed the register file does not reallocate across the property read: it
  grows geometrically, settles at a high-water mark, and nothing reachable
  inside the frame guard pushes past it. The reload after the nested CALL is
  enforced — blinding it is a clean asan use-after-free.

## Phase 6: the row that said it could not be written

`aot_helpers.def` records, dated and by name, that `ct_aot_catch_land` **cannot
be implemented as written, found by trying**. The problem was real: the row says
to read back `registers_[call_frame::base + handler::slot]`, and
`unwind_to_handler` **pops the handler before it writes**, so by the time a
compiled body could ask, which register the thrown value went into was
unknowable. `result_reg` is where the caller wants the return value and is a
different register.

The row then wrote down two fixes and took neither, for a reason worth quoting:
*"taking one without a compiled `try` to test it would be inventing on no
evidence"*. That is the right instinct and it is why this was cheap to finish —
the evidence existed as soon as a body could be hand-written.

**The fix is a third option the row did not consider.** `call_frame::landed_slot`
records the slot at the moment `unwind_to_handler` writes it: two bytes on a
frame the interpreter never reads. The row's own preferred option — adding the
slot to the helper's parameters — would have changed a signature two code
generators are written against, to save those two bytes.

`CT_AOT_PAD_BIT` is defined too. `aot.hpp` left it out deliberately: *"inventing
either here would freeze a choice with no measurement behind it into a header
two backends will read"*. The measurement turned out to be arithmetic rather
than a benchmark — `ip` is a `size_t` index into bytecode, and 2^63 instructions
would be 74 exabytes, so the top bit cannot collide with a real one. With it,
**the unwinder does not change at all**: the pad id rides in `handler::address`,
and the same four steps that resume the interpreter at a catch block land a
compiled body on its pad.

### Completions, not unwinding

No C++ exception is thrown through a compiled body. A helper returns
`CT_AOT_CAUGHT` and the body branches, which is what the plan means by
"generated functions are nounwind". The test checks five shapes against the
interpreter: nothing thrown (the handler must come off, or it stays live for a
frame that has returned), a string thrown, an object whose `toString` runs
**inside** the compiled catch block, a throw passing through to a handler below
(`UNWOUND` — no epilogue, no `leave`, because the frame is already gone), and an
uncaught throw, which is the tier no `catch` may see.

Five blindings, each red. **Two were green first, and the test was at fault both
times:**

* The landing slot was **slot zero**, so a `landed_slot` left at its default was
  indistinguishable from one correctly recorded.
* Nothing exercised a **mis-balanced handler stack** — the hazard the row names:
  pop takes the globally innermost handler without consulting `handler_base`, so
  a body popping one it never pushed silently takes its CALLER's catch. There is
  a deliberately sloppy compiled body for that now.

## Phase 7: MLIR, stood up before a single operation exists

The gate is four things and they are all met: `ctjs-opt` and `ctjs-translate`
build and run; hand-authored CTJS MLIR containing only the five types parses,
verifies, prints and round-trips; `check-ctcompile` runs and passes; and a
runtime-only `ctbrowser` configure still succeeds with no LLVM or MLIR.

That last one was checked **with MLIR installed**, which is the case that
actually matters. A box without it was never the danger — the danger is a box
with it that quietly starts requiring it, which is why `CTCOMPILE_ENABLE_MLIR`
exists and stays OFF.

### What Phase -1 had already built, and I nearly rebuilt

`ct_require_llvm_version()` and `ct_add_tablegen_component()` have been sitting
in `cmake/modules/` since the monorepo split, with the note: *"unused until
Phase 7 stands MLIR up — it is here now so that phase adds dialects rather than
build plumbing"*. My first version hand-rolled the version check next to them.

**And that function had never run, and was broken.** It included the pin as
`"${CMAKE_CURRENT_LIST_DIR}/../LLVMVersion.cmake"` — and inside a function body
that variable is the **caller's** directory, not the module's. The include
failed, the three variables it sets were empty everywhere, and the comparison
that used one ran against an empty string, which CMake's `LESS`/`GREATER` treat
as 0. It accepted every version. The module captures its own directory now.

### Three policy rules that only mean something once you hit them

* **The five types are generated from `CTJSOps.td`**, by `add_mlir_dialect` —
  there is no `-gen-typedef-decls` call anywhere in the policy. So `CTJSOps.td`
  must include `CTJSTypes.td` or the types are never generated. The file layout
  implies otherwise; the CMake decides.
* **"EXTRA_INCLUDES must reach both the project's own .td files and MLIR's" is
  about a directory property.** LLVM's `tablegen()` does
  `get_directory_property(tblgen_includes INCLUDE_DIRECTORIES)` and turns each
  entry into a `-I`; `LLVM_TABLEGEN_FLAGS` is passed through untouched and never
  becomes an include path. That cost a build to find out.
* **The generated header is `CTJSOpsTypes.h.inc`** — named after the `.td` given
  to `add_mlir_dialect`, not after `CTJSTypes.td`.

### One deviation, with its reason in the file

`useDefaultAttributePrinterParser` is 0 where the mandated `CTJSBase.td` says 1.
Setting it makes the dialect **declare** `parseAttribute` and `printAttribute`,
whose definitions come from `-gen-attrdef-defs` — and with zero `AttrDef`s that
backend emits an empty file, so the library does not link. Nothing is
hand-written to paper over it, because hand-writing those two is on the policy's
never list. It returns to 1 in Phase 8 with the first attribute.

### The action item, answered rather than deferred

The policy ends its dialect section with: *"Verify `TypeDef` usability as a
direct type constraint against the pinned MLIR version… Do not scatter `AnyType`
as a workaround — that silently disables verification."* Generating an operation
that uses `CTJS_ValueType` in `arguments` against MLIR 22.1.8 produces

```cpp
if (!((::llvm::isa<::ctcompile::ctjs::ValueType>(type)))) {
  return op->emitOpError(valueKind) << " must be A generic, boxed ECMAScript
      value, but got " << type;
```

a real `isa<>` check with the summary in the diagnostic. **Phase 8 uses the
TypeDefs directly and needs no aliases.**

### And the test is two passes, not one

`ctjs-opt %s | ctjs-opt | FileCheck %s`. One pass proves the parser accepts the
syntax; the pair proves the **printer** emits something the parser accepts,
which is what makes every later test's expected output trustworthy. Two
blindings, both red: a mnemonic changed in the ODS, and the dialect not
registering its types at all.

## Phase 8: the dialect, and what ties it to the ABI

32 operations in ODS, round-tripped, every verifier diagnostic watched firing,
documentation building. `grep -r "public Op<" lib/` finds nothing.

**The operations name real helpers.** `CTJS_RuntimeOp` takes an enumerator of
`ctbrowser::aot::helper_id` — generated from `aot_helpers.def` — so an operation
cannot claim a helper the runtime does not declare, and a wrong name is a C++
compile error rather than a lowering that calls the wrong thing. That is the
join between this dialect and the ABI the last several phases built: the same
three obligations the helper rows carry (`may_throw`, `may_reenter`,
`is_safepoint`) are the traits the operations carry, spelled the same way on
purpose.

**Folded where the policy asks, split where the traits differ.** Twelve binary
operators are one `ctjs.binary` with a kind; six comparisons and six conversions
likewise. But `ctjs.binary_static` is a *separate operation*, not a flag,
because the static family cannot run user code — which is a difference in
traits, and the policy says to split on exactly that.

**Three operations are deliberately not `RuntimeOp`s**, each saying why in its
description. `ctjs.unary` and `ctjs.compare` because their kinds reach three and
four different helpers with different effect profiles, and one `getHelperID`
cannot answer for all of them. `ctjs.create_regexp` because **the ABI declares
no helper for a regexp literal at all** — a real gap in the table, recorded
here rather than papered over by pointing at a helper that does something else.

### Four things MLIR 22 wanted that the policy's snippets do not show

Each is now a comment where it bit, because every one cost a build:

* `genSpecializedAttr` **already generates** the `<Name>Attr` class. Adding
  `EnumAttr` records for the same five was a redefinition — and the "no type
  named BinaryKind" error that prompted it was one missing `#include`.
* `FunctionOpInterface` **declares** `getArgumentTypes`, `getResultTypes` and
  `getCallableRegion` and defines none of them. Putting them in
  `extraClassDeclaration` is "cannot be redeclared"; omitting them is an
  undefined reference from the interface's own Model. They go in the `.cpp`.
* `addTypes<>` / `addAttributes<>` need the storage classes **complete**, and
  those live in the `.cpp` that defines them — hence `registerTypes()` and
  `registerAttributes()` split across those files, which is upstream MLIR's own
  layout.
* `add_mlir_doc` writes under `${MLIR_BINARY_DIR}/docs`, which is empty out of
  tree, so the doc target tried to `mkdir("/docs")`.

### One deviation, with its reason in the file

`ctjs.number` carries the IEEE-754 **bit pattern** rather than an `APFloat`. The
policy's objection to a builtin `FloatAttr` is exactly right — it compares
`-0.0` equal to `0.0` and JavaScript does not — but MLIR 22 has **no
`FieldParser` for `APFloat`**, so that parameter generates a parser that does not
compile. The alternatives were a hand-written parser, which is on the never
list, or a printer that cannot round-trip a NaN payload. The bits are exact for
both zeroes and every NaN, and need no parser at all.

### The verifier worth reading

`ctjs.pop_handler`. The runtime's `ct_aot_handler_pop` takes the **globally**
innermost handler without consulting the frame, so a body that pops one it never
pushed silently takes its *caller's* catch — and nothing at run time reports it.
The verifier makes it a build error, and it checks what a verifier *can* check:
one block. Whole-function balance is a dataflow question and belongs to a pass.

All four verifiers falsified.

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
