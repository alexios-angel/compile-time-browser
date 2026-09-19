[Back to ctcompile.md](../ctcompile.md)

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
in `browser/scripts.cpp` appends a `\n` to every classic script's source — so a
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

## Phase 9: bytecode into CTJS MLIR

The gate is "real bytecode functions translate into equivalent CTJS MLIR".

| corpus | imported | refused | module |
|---|---|---|---|
| p5.js | **3,200** | 1,554 | verifies |
| phaser.js | **6,069** | 1,656 | verifies |

### The register file is the block argument vector

Every non-entry block takes `frame_size` arguments and every branch passes the
whole file. That is **not** the SSA construction the plan forbids here — no
dominance frontiers, no phi minimisation, no backpatching — because blocks are
created with their full argument list before anything is emitted, so a back
edge's operands are known when it is written. Pruning it to what is live is the
plan's own "later transition toward SSA/block arguments".

The dialect turned out to be built for it already: `ctjs.push_handler` carries
`$bodyOperands` **and** `$handlerOperands` and implements `BranchOpInterface`,
which only makes sense if a block carries a register vector.

### Three bugs of the shape this project keeps meeting

IR that verifies, prints plausibly, and is wrong:

* **No fall-through edge.** Bytecode runs off the end of one instruction into
  the next; an MLIR block does not. The entry block of every loop simply ended,
  the terminator pass gave it `return undefined`, and the loop header was
  reachable only from its own back edge.
* **A block one past the end.** `ret` marks its successor a leader, and for the
  last instruction that successor does not exist — so every function carried an
  unreachable stub with an invented terminator.
* **A duplicate symbol.** `ctjs.func` is a `Symbol` and p5.js has dozens of
  functions called `constructor`. The symptom was `ctjs-translate` producing
  **no output at all** for a 4,000-function file while every individual function
  verified. The index is part of the name now.

### The measurement is the work list

The first cut imported **none** of p5.js — every function hit an unmapped
opcode, which is the "never emit partially correct AOT code" invariant working
rather than failing. But the refusals are *counted*, in a `ctjs.skipped`
attribute and as warnings, so widening had a priority order rather than a guess:
`get_upvalue` 1565, `call_method` 1084, `new_cell` 467, `new_array` 196. Doing
exactly that list took it from 0 to 3,200.

What still refuses is the same kind of list: `closure` 556 — which needs a
producer for `!ctjs.program`, a design question rather than a mapping —
`gather_rest` 173, `iterable` 117, `make_arguments` 111.

### Four things MLIR 22 required

Each a comment where it bit: dialects must be **loaded**, not merely registered
(a registry says a dialect *may* be used; `Type::get` needs it loaded, and
`mlir-translate` only registers); the translation registration takes the
registry as its third argument; `builder.create<Op>` is deprecated for
`Op::create`; and `add_mlir_library` compiles through an `obj.` target that
`target_compile_options` cannot reach — so the importer is an ordinary library,
which is right anyway since it is hand-written C++ and generates nothing.

## Phase 10: one pattern, and what the arity check found

The phase's claim is that `CTJS_RuntimeCallOpInterface` pays for itself — every
operation implementing it lowers identically, so there is **one** conversion
pattern rather than fifty near-identical files and fifty chances to get the
operand order wrong.

```
%1 = ctjs.cell_get %arg0   ->  func.call @ct_aot_cell_get(%arg0)
ctjs.frame_exit %0         ->  func.call @ct_aot_leave(%0)
```

**The pass names no operation.** That is the plan's acceptance criterion rather
than a nicety: *"a pass that switches on operation names must be revisited every
time an operation is added. A pass that queries a trait never is."*

### The arity check fired on its first run and was right

The plan's sketch assumes the call is "context, then the operation's operands in
ODS order". Most of the table is not shaped like that:

```
ct_aot_binary_op(fr, op_kind, lhs, rhs, out)           ODS has 2 operands
ct_aot_enter(ctx, site, reg_count, receiver, storage)  ODS has 0
```

`op_kind` is an **attribute**, `out` is an **out-parameter** carrying the
result, `site` is the baked diagnostic and `storage` is caller-allocated frame
space. None is an operand, and materialising each is its own small decision.

So a mismatch is **"not yet", not "wrong"**: the pattern declines the match and
leaves the operation for a later one, rather than failing the module or — far
worse — emitting a call with a garbage argument. What it declines is the work
list for the rest of the phase, exactly as the importer's refusals were.

The check itself counts each helper's parameters by parsing the stringified list
from `aot_helpers.def`, rather than a number written beside each row that could
disagree with the row above it. That closes the ABI-drift hole from the side the
name check cannot see: a helper that does not exist is already a compile error;
an operation whose operands have drifted from its helper's parameters reads fine
in both files.

## The first PDLL pattern, and where the declarative boundary actually is

Part 4 of the master plan sends "structural rewrites" to PDLL. Until now nothing
in this tree had one, so the question of whether that instruction is the right
one here had never been asked with a build behind it. One rule was converted and
kept green; the interesting output is the list of things that could not follow
it.

### What was converted

`--ctnative-prune-dead-stores` had two rules. The first — *an `emitc.call` or
`emitc.call_opaque` whose single result nothing reads gets `ctnative.statement`,
so the emitter prints `f(x);` rather than `double v = f(x);`* — is a match on
operation structure and an attribute on the same operation, with no type
conversion, no block surgery and no analysis. It is now
`ctcompile/lib/CTNative/Lowering/PruneDeadStores.pdll`, compiled by `mlir-pdll`
through `add_mlir_pdll_library` into the build tree.
`test/CTNative/Lowering/Emission/unused-call.mlir` — which already pinned the marked
call, the unmarked one, the emitted C++ and the count — passes **unchanged**.

The second rule stayed C++. Erasing a write-only `emitc.variable` needs every
*use* of a value classified, its users erased with it, and the whole run to a
fixpoint; PDL matches structure, not use lists.

The runners-up were weighed and rejected, each for a different reason, and the
reasons are the map:

* **`CTJSToRuntime`'s `RuntimeCallLowering`** is an `OpInterfaceRewritePattern`
  that reads `getHelperID()` and edits the module's symbol table. PDL dispatches
  on an operation *name*; it cannot match an interface and cannot call an
  interface method. Converting it would replace one pattern with fifty.
* **`CTJSToEmitC`** (the boxed tier) is **not** disqualified by a
  `TypeConverter` — it does not use one, and neither does the native lowering.
  It is disqualified by refusal diagnostics and by rebuilding a function body
  through an `IRMapping`.
* **`LowerToEmitC::replace()`** is the most pattern-shaped code in the project
  and is the one that cannot be written at all. See below.

### Six things PDLL could not express, with what happened

**1. It cannot read a lattice.** PDLL's type vocabulary is `Attr`, `Op`, `Type`,
`TypeRange`, `Value`, `ValueRange` and ODS constraints — nothing else:

```
error: unknown reference to constraint `DataFlowSolver`
Constraint IsProvedNumber(v: Value, solver: DataFlowSolver) [{
                                            ^
```

**2. It cannot reach pass-local state either.** `mlir-pdll` registers each
native body as a plain function pointer —
`registerConstraintFunction("HasExactlyOneResult", HasExactlyOneResultPDLFn)` —
whose only parameters are the rewriter and the matched entities. Nothing
captures. `PDLPatternConfig`, the one hook that travels with a pattern, offers
only `notifyRewriteBegin/End(PatternRewriter &)` and never reaches a native
function. So `shapes`, `accessKey`, `keyConstants` and `names` can only be
reached from a global or a `thread_local`. Every predicate in `admission` is
therefore native, and once every predicate is native the `.pdll` contributes the
operation name and nothing else.

**3. A non-match is silent, and there is no `otherwise`.**

```
error: undefined reference to `otherwise`
```

A native constraint *can* emit a diagnostic — it holds the rewriter and the
operation — but constraints run only after the structural predicates have
already matched, so the case that matters (this operation is not one we handle)
never reaches C++. Worse, it is not even observable here: the release LLVM has
no `--debug-only`, so `greedy-rewriter` tracing is unavailable
(`ctjs-opt: Unknown command line argument '--debug-only=greedy-rewriter'`).

**4. It cannot retype a value in place.** A matched `Type` is immutable; there
is no assignment in a rewrite body:

```
d-retype.pdll:8:7: error: expected `;` after statement
    r = type<"f64">;
      ^
```

`LowerToEmitC::retype()` sets carriers on existing values *before* any operation
is replaced, and the whole of the native lowering depends on that ordering. No
part of it can be PDL.

**5. `mlir-pdll` cannot parse this project's own attributes, and says so with
exit status 0.** The policy's own example discriminates on a dialect enum
(`attr<"#ctjs.convert_kind<to_boolean>">`). Try it:

```
error: #"ctjs"<"binary_kind<add>"> : 'none' attribute created with
unregistered dialect...
exit=0
```

`mlir-pdll` has no way to load an out-of-tree dialect, so the attribute becomes
an opaque one — and the generated pattern **drops the constraint entirely**:

```mlir
%2 = attribute
%4 = operation "ctjs.binary"(%0, %1) {"kind" = %2} -> (%3)
```

That matches `ctjs.binary sub` as readily as `add` and rewrites it to
`emitc.add`. A **silent miscompile from a build that succeeds**, with one line
on stderr that Ninja does not treat as a failure.

> **Corrected 2026-09-02 — see "PDLL over our own dialect" below.** The
> attribute *literal* is indeed unusable, and that half stands. What does not
> stand is the conclusion drawn from it. A **native constraint** discriminates
> on the same enum, in C++ that the compiler checks, and it works today; and
> the build no longer succeeds when the literal is used, because
> `utils/pdll-strict.sh` refuses any run that printed a diagnostic. The reason
> `replace()`'s switch cannot move is *the lattice*, not the attribute.

**6. ODS arity does not check native signatures.** `emitc.call` declares
`Variadic` results, so `op<emitc.call> -> (r: Type)` does not pin the count, and
`root.0` is a `!pdl.range<value>` handed to a constraint declared
`(result: Value)` — accepted, exit 0, no diagnostic, wrong only at run time.
That is why `HasExactlyOneResult` is a native constraint on the operation.
