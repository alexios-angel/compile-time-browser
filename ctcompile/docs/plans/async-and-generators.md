# Async functions, `await`, generators and suspension

Phase 14 is three opcodes — `op::wrap_promise`, `op::await_value`,
`op::yield_value` — and **one of them is not a suspension point.** That split
is the whole shape of this document:

**`op::wrap_promise` LANDED WITH IT.** An async function containing no `await`
carries `wrap_promise` on every return path and no `await_value` at all, so it
is fully AOT-eligible and needed no new mechanism. It is implemented, lowered,
and covered by two differential arms.

**The other two are a design decision, not a missing case**, and the honest
answer for now is to refuse them precisely rather than to guess. A compiled
body is a C++ stack frame; `coroutine_object` saves a suspended frame by
copying its REGISTER WINDOW out of the flat register file; a compiled frame has
no register window. That is not a gap somebody forgot to fill.

Everything below was read out of the tree rather than recalled. Where a fact
asserted by an existing comment, ABI row or plan file turned out to be false,
it is recorded here as a finding rather than quietly worked around.

## What was measured

* **The importer refused all three opcodes through `default:`**, with the
  reason `"no CTJS operation for this opcode yet"` — the same sentence
  `op::dyn_import` gets. Two of the three will never get a case, so the
  sentence was wrong about them in a way that reads as a work item.
* **`function_proto` does not carry `is_async`.** `compiler_impl::is_async`
  exists and gates every `wrap_promise` emission (`statements.cpp:227, 713,
  792, 983`), and `compile_function_decl` copies only
  `proto().is_generator = fn().is_generator`. `EngineContract.hpp`'s
  `proto_header` does not carry it either. **So nothing at dispatch time can
  tell an async function from an ordinary one**, and an async function reaches
  `enter_compiled` exactly like any other. The importer's refusal is the only
  thing standing between a compiled body and an `await` it cannot execute.
* **A generator, by contrast, is guarded twice and never dispatched.** Both
  `context::call` (`call.cpp:80`) and `VM_CASE(call)` test
  `target.is_generator` and build a coroutine BEFORE asking `enter_compiled`,
  and `unittests/unit/aot_dispatch` asserts that boundary by name.
* **`ct_aot_wrap_promise` had no body anywhere.** Neither did
  `ct_aot_await_settled` nor `ct_aot_suspend_unsupported` — searched across
  `ctbrowser/lib`, `ctbrowser/unittests` and `ctcompile`. The only mentions
  outside `aot_helpers.def` were one `static_assert` and one comment.
* **`ct_aot_status` has four enumerators and `CT_AOT_SUSPENDED` is not one of
  them**, despite `ct_aot_await_settled`'s row naming it as an outcome. The row
  says so itself: it is "produced only to be refused by the caller" until Phase
  14, and adding it to the shared enum "makes every ctcompile status switch
  non-exhaustive at once."
* **`ct_aot_suspend_unsupported` covers `yield_value` and nothing covers
  `await_value`'s suspend half.** `ctcompile/test/Inventories.cpp` carries two
  named exemptions for exactly this and says whoever implements suspension is
  the one who deletes them.
* **The `default:` refusal is not enough on its own, and was never the only
  net.** `body_is_supported` in `CTJSToEmitC.cpp` is an allow-list, so a
  hypothetical importer case for `ctjs.suspend` would still be refused at
  lowering. Two nets, and neither of them said WHY.
* **`ctcompile_importer_coverage`'s matcher cannot tolerate a `case
  op::await_value:` label.** It carries a sanity check that fails if
  `op::await_value` is ever reported as dispatched, on the grounds that the
  matcher would then be matching prose. A refusal is not a dispatch, so the
  refusal had to be written as a pre-walk scan rather than a switch case —
  which is the right shape anyway: the reason belongs to the FUNCTION, not to
  one instruction.

### The corpora, measured

`ctjs-translate --ctbrowser-js-to-ctjs` over each vendored bundle, counting
`ctjs.func` operations in the output and `is not compiled` diagnostics by
opcode. Run with this work applied, so `wrap_promise` is imported and the two
suspending opcodes are refused with the new reason.

| corpus | imported | contain `wrap_promise` | refused: `yield_value` | refused: `await_value` | refused: `make_arguments` | refused: `gather_rest` | refused: `push_handler` |
|---|---:|---:|---:|---:|---:|---:|---:|
| bootstrap | 570 | 0 | 0 | 0 | 1 | 3 | 0 |
| p5 | 4,424 | **14** | 0 | **39** | 108 | 171 | 12 |
| phaser | 7,702 | 0 | 0 | 0 | 21 | 0 | 2 |
| babylon | 31,056 | 0 | **598** | 0 | 142 | 78 | 31 |

Four things in that table were not expected and matter:

* **`yield_value` is the single largest refusal reason in the largest corpus**,
  by a factor of four over the next one. 598 of Babylon's functions are
  generators. Any reading of Phase 14 that treats generators as the tail end of
  async has it backwards.
* **Babylon has 598 generators and ZERO `await_value` and ZERO
  `wrap_promise`.** It is transpiled: TypeScript's `__awaiter`/`__generator`
  turns every `async`/`await` into a generator driven by a helper, so the
  suspension shows up as `yield` and native async never appears. **A state
  machine that handled `await` and not `yield` would buy nothing on this
  corpus.**
* **p5 is the mirror image** — 39 `await_value`, 14 `wrap_promise`, zero
  generators — because it ships untranspiled.
* **The 14 are the population this work made compilable.** Small in absolute
  terms, and they were previously refused with a message that said an operation
  was missing.

**And one measurement that is not about async at all**, found while trying to
run the suite against this change:

* **`tools/remote-build.sh` does not build the MLIR pipeline.** The `default`
  preset never sets `CTCOMPILE_ENABLE_MLIR`, which defaults to `OFF`, so the
  108-test devbox run contains **no lit tests, no `ctcompile_differential`, no
  `ctcompile_linkable` and no `ctcompile_gc_roots`** — it prints
  `ctcompile: gc-roots test unavailable` and moves on. The dialect, the
  importer and the EmitC backend are not compiled by the command `CLAUDE.md`
  calls "the whole gate". See *What could not be verified*.

## The three ABI rows, verbatim

From `ctbrowser/include/ctbrowser/aot/aot_helpers.def`. The `wrap_promise` row
is quoted as it stands after this work — its `DELEGATES TO` paragraph was
repaired, and what it used to say is inside it.

```
/* IMPLEMENTED. It had REAL tier-1 callers before it had a body and now it
 * has both: an async function with no await contains wrap_promise and no
 * await_value, so it is fully AOT-eligible, and the emission is
 * unconditional on every async return path (statements.cpp:227, 713, 792,
 * 983). The test must live inside the helper because it reads an own
 * '__value' off an object_object (constraint 1). THREE facts the lowering
 * must keep: the test is is_object() EXACTLY, so an array, function or
 * proxy returned from an async function is ALWAYS re-wrapped; with no
 * promise_factory_ installed context::make_promise is the IDENTITY, so
 * AOT must not fold this to 'the result is always an object'; and a PENDING
 * promise is passed through UNWRAPPED, because the pending factory builds
 * it with detail::make_promise and only then flips __settled to false
 * (async.cpp:49-53), so it carries an own __value. Two adjacent handlers
 * therefore use two DIFFERENT shape tests for 'is a promise' - wrap_promise
 * reads __value, context::is_pending_promise reads __settled - and a
 * backend that unified them would change what `return somePendingPromise`
 * does inside an async function.
 * DELEGATES TO: context::wrap_in_promise (vm.hpp), which is the WHOLE
 *   handler [...]
 * FAILURE: RAISE TIER ONLY (the ceiling), uncatchable, unwinds nothing.
 *   may_reenter=0 verified against the INSTALLED factory rather than the VM
 *   stub: detail::make_promise (internal.hpp:339-347) is new_table,
 *   promise_prototype, four sets and a make_array, with no call() anywhere.
 *   allocate() raises and STILL returns a valid object, so the operation
 *   completes and the result is written; FAILED means 'stop', not 'nothing
 *   happened'. */
CT_AOT_HELPER(/* name */ ct_aot_wrap_promise, /* ret */ uint64_t,
              /* params */ (struct ct_aot_frame *fr, uint64_t v), /* may_throw */ 1,
              /* may_reenter */ 0, /* is_safepoint */ 1)
CT_AOT_COVERS(/* helper */ ct_aot_wrap_promise, /* opcode */ wrap_promise)
```

```
/* IT DISAGREES WITH THE INVENTORY ROW ON PURPOSE, and the disagreement is
 * verified rather than asserted: await_value's row is (1,1,1,0,1,1,1) but
 * the READ half contains no allocate<> and no call(); raise() only
 * concatenates a std::string built by current_stack (const, allocates no GC
 * object) and unwind_to_handler only resizes vectors. So the row is true of
 * the SUSPEND half only. The split is legitimate ONLY because the DECISION
 * is allocation-free - the interpreter creates vm_frame->async_promise and
 * the coroutine_object strictly AFTER the three-way test at
 * run_loop.cpp:1177-1182 - and that ordering is now an ABI contract, not an
 * observation: if Phase 14 hoists promise creation above the test, this
 * silently becomes a safepoint and every caller's flush disappears. It also
 * preserves two facts: await NEVER re-enters user JS (no then() is called;
 * state is read with own-property find), and is_pending_promise uses
 * is_object() exactly, so a promise behind a Proxy is never seen as
 * pending. Kept even though it has zero tier-1 callers today, because
 * dropping it leaves await_value with no row and lets CT_AOT_SUSPENDED
 * enter the shared enum in Phase 14, making every ctcompile status switch
 * non-exhaustive at once.
 * DELEGATES TO: context::is_pending_promise (vm.hpp:822) plus the read path
 *   at run_loop.cpp:1207-1219 (__rejected -> thrown_ + unwind_to_handler,
 *   else __value). It does NOT contain the suspend path.
 * FAILURE: CT_AOT_OK with *out = the settled __value, or the awaited value
 *   itself when it is not an object, or UNDEFINED when the input is a
 *   pending promise in a context with no
 *   pending_promise_factory_/promise_settler_ - such a promise still
 *   carries __value = undefined (async.cpp:50-52) and run_loop.cpp:1218
 *   takes it, which a headless AOT target must not be surprised by.
 *   CAUGHT/UNWOUND for a settled REJECTED promise. CT_AOT_FAILED when
 *   nothing catches it ('uncaught rejection'). CT_AOT_SUSPENDED when
 *   is_pending_promise(awaited) AND pending_promise_factory_ AND
 *   promise_settler_ are ALL present, exactly as run_loop.cpp:1177 tests
 *   them - and until Phase 14 that status is produced only to be refused by
 *   the caller. */
CT_AOT_HELPER(/* name */ ct_aot_await_settled, /* ret */ int32_t,
              /* params */ (struct ct_aot_frame *fr, uint64_t awaited, uint64_t *out),
              /* may_throw */ 1, /* may_reenter */ 0, /* is_safepoint */ 0)
CT_AOT_COVERS(/* helper */ ct_aot_await_settled, /* opcode */ await_value)
```

```
/* COVERAGE, NOT CAPABILITY. yield_value sits between await_value and
 * wrap_promise in the inventory and 'excluded by the tier-1 rule' is not
 * coverage - Phase 13 checks 93 opcodes against the helper table, so an
 * opcode with no row is a hole whichever way it was reasoned about. This
 * particular hole is where the eligibility predicate sits: function_proto
 * carries is_generator (statements.cpp:751) but the compiler's is_async
 * (compiler_impl.hpp:119) is never copied to the proto and is absent from
 * engine_contract::proto_header (EngineContract.hpp:86-93), so Phase 3's
 * dispatch and ctcompile must each derive the predicate and CANNOT derive
 * it from the same data. Today a disagreement means native code runs off
 * the end of a yield with no diagnostic. This makes it a named engine fault
 * with a stack, costs one call at a site that must be unreachable, and
 * commits to NO suspend ABI - constraint 4 says Phase 14 owns that. Delete
 * it in Phase 14 when a real suspend helper replaces it; the .def edit is
 * one line either way. It covers yield_value ONLY: await_value is owned by
 * ct_aot_await_settled, whose CT_AOT_SUSPENDED outcome is the decision
 * point.
 * DELEGATES TO: context::raise (vm.hpp:932), with the opcode's name from
 *   bytecode_opcodes.def and the JS stack from current_stack()
 *   (vm.hpp:426).
 * FAILURE: Always CT_AOT_FAILED. It produces no value and takes no out-
 *   parameter; the row exists so that 'this opcode has no tier-1 lowering'
 *   is a DECLARED outcome with a diagnosable message rather than an absent
 *   row. */
CT_AOT_HELPER(/* name */ ct_aot_suspend_unsupported, /* ret */ int32_t,
              /* params */ (struct ct_aot_frame *fr, uint32_t opcode), /* may_throw */ 1,
              /* may_reenter */ 0, /* is_safepoint */ 0)
CT_AOT_COVERS(/* helper */ ct_aot_suspend_unsupported, /* opcode */ yield_value)
```

### The three rows disagree with each other about what to build, and that is a finding

`ct_aot_await_settled` says the suspend decision is `CT_AOT_SUSPENDED`, a
status a compiled caller tests. `ct_aot_suspend_unsupported` says a suspension
point should be **a named engine fault with a stack** and commits to no ABI.
Both were written by the same phase. They are not contradictory — one is
`await`'s design sketch and the other is `yield`'s placeholder — but a reader
who takes either as settled will build the wrong thing. `ct_aot_await_settled`
is a sketch too: it has zero callers and no body, and the recommendation below
does not adopt it as-is.

## The VM handlers, verbatim

All three from `ctbrowser/lib/Script/vm/run_loop.cpp`. In scope there, `base`
is `vm_frame->base` and `reg(r)` is `registers_[base + r]`.

`wrap_promise` is quoted as it was BEFORE this work, because the body moved:

```cpp
        VM_CASE(wrap_promise) do {
            // Already a promise (`return somePromise` inside an async function)
            // stays as it is rather than nesting.
            if (!(reg(in.a).is_object() &&
                  static_cast<object_object *>(reg(in.a).as_heap())->find("__value") != nullptr)) {
                reg(in.a) = make_promise(reg(in.a), false);
            }
            break;
        }
        while (0);
        VM_NEXT;
```

```cpp
        VM_CASE(await_value) do {
            {
                const value awaited = reg(in.b);
                if (is_pending_promise(awaited) && pending_promise_factory_ && promise_settler_) {
                    if (vm_frame->async_promise.is_undefined()) {
                        vm_frame->async_promise = pending_promise_factory_(*this);
                    }
                    const value promise = vm_frame->async_promise;
                    auto * saved = allocate<coroutine_object>();
                    saved->proto = vm_frame->proto;
                    saved->ip = vm_frame->ip;
                    saved->await_reg = in.a;
                    saved->argc = vm_frame->argc;
                    saved->closure = vm_frame->closure;
                    saved->receiver = vm_frame->receiver;
                    saved->constructing = vm_frame->constructing;
                    saved->promise = promise;
                    saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base),
                                         registers_.end());
                    for (std::size_t i = vm_frame->handler_base; i < handlers_.size(); ++i) {
                        handler moved = handlers_[i];
                        moved.reg_top -= base;
                        saved->handlers.push_back(moved);
                    }
                    handlers_.resize(vm_frame->handler_base);
                    const std::uint16_t slot = vm_frame->result_reg;
                    registers_.resize(base);
                    frames_.pop_back();
                    attach_resume(awaited, value::object(saved));
                    suspended_ = true;
                    if (frames_.size() <= stop_depth) { return promise; }
                    registers_[frames_.back().base + slot] = promise;
                    break;
                }
                reg(in.a) = awaited;
                if (awaited.is_object()) {
                    auto * obj = static_cast<object_object *>(awaited.as_heap());
                    if (value * state = obj->find("__rejected");
                        state != nullptr && truthy(*state)) {
                        thrown_ = obj->find("__value") != nullptr ? *obj->find("__value")
                                                                  : value::undefined();
                        if (!unwind_to_handler()) { raise("uncaught rejection"); }
                        break;
                    }
                    if (value * settled = obj->find("__value")) { reg(in.a) = *settled; }
                }
                break;
            }
        }
        while (0);
        VM_NEXT;
```

```cpp
        VM_CASE(yield_value) do {
            {
                coroutine_object * saved = vm_frame->generator;
                if (saved == nullptr) {
                    raise("`yield` outside a generator");
                    break;
                }
                const value produced = reg(in.b);
                saved->ip = vm_frame->ip;
                saved->await_reg = in.a;
                saved->receiver = vm_frame->receiver;
                saved->window.assign(registers_.begin() + static_cast<std::ptrdiff_t>(base),
                                     registers_.end());
                saved->handlers.clear();
                for (std::size_t i = vm_frame->handler_base; i < handlers_.size(); ++i) {
                    handler moved = handlers_[i];
                    moved.reg_top -= base;
                    saved->handlers.push_back(moved);
                }
                handlers_.resize(vm_frame->handler_base);
                registers_.resize(base);
                frames_.pop_back();
                yielded_ = true;
                if (frames_.size() <= stop_depth) { return produced; }
                raise("a generator yielded outside its own resume");
                break;
            }
        }
        while (0);
        VM_NEXT;
```

**The two suspending handlers are the same eleven steps in a different order.**
Save `ip`, save the destination register, copy the window, rebase the handlers,
truncate `registers_`, pop the frame, set a flag, return out of `run_loop`. What
differs is only who puts the frame back and where the produced value goes.

## What the runtime's coroutine representation actually is

`ctbrowser::script::context::coroutine_object`, in `vm.hpp`, is a
`heap_object` — so the collector already walks it — with these fields:

| field | what it is |
|---|---|
| `proto`, `closure`, `receiver`, `constructing`, `argc` | the frame's identity, re-installed on resume |
| `ip` | a bytecode index, captured AFTER the dispatch increment |
| `await_reg` | where the resumed value lands: the `await`'s or `yield`'s destination register |
| `window` | **`std::vector<value>` — the whole register window, COPIED** |
| `handlers` | this frame's handlers with `reg_top` made RELATIVE to `base` |
| `promise` | the promise the caller was handed. One per suspended function however many times it awaits |
| `generator`, `started`, `done`, `running` | the generator protocol's four bits |

Three properties matter more than the list:

**1. `window` is a COPY, and it has to be.** The comment says why: "the stack is
truncated the moment the frame leaves, and whatever runs next reuses those
slots." The state of a suspended function is `frame_size` NaN-boxed words in a
vector, and nothing else.

**2. Only the top frame can suspend.** `registers_` is one flat vector indexed
by base, so lifting a frame out of the middle would move every frame above it.
The `01-objective-and-ground-truth.md` file calls this "the hardest interop
constraint in the tree."

**3. Resumption is `resume()` or `generator_resume()`, and they are the same
function twice.** Both push the window back onto `registers_` at a NEW base,
rebuild a `call_frame`, re-absolutise the handlers, drop the sent value into
`registers_[base + await_reg]`, and call `run_loop(stop)`. The differences are
that `generator_resume` owns the `{value, done}` record and the four protocol
bits, and `resume` owns the promise settlement and the microtask drain.

**`make_generator` truncates the window and keeps `argc`**, which is a real
miscompile already present in the VM: `saved->window.resize(frame_size)` after
assigning the arguments, while `saved->argc` keeps the arriving count. A
generator called with more arguments than `frame_size` loses them but still
reports having received them. Recorded because the argument track's design (a)
would read `registers_[argv_base + i]` against a frame restored somewhere else
entirely — and is saved only by the two `is_generator` guards.

### What a compiled frame has instead

Nothing that matches any of it. `aot_frame_storage` is 32 bytes — a context
pointer and three indices — and the values a compiled body produces live in
C++ locals, spilled ("parked") into a run of `registers_` slots purely so the
collector can see them. A compiled body's PROGRAM COUNTER is the host CPU's,
its locals are in host registers and its own stack frame, and neither is
addressable from `coroutine_object`.

## Candidate designs

### (a) Refuse any function containing a suspension point, and say so precisely

What is implemented. The importer scans for `may_suspend` — read out of
`bytecode_opcodes.def`'s own column, not a list of two names — and refuses the
whole function with a reason naming the mechanism.

*Cost.* Every `async` function that awaits and every generator stays
interpreted, for ever, until this is replaced. Nothing else.

*What it buys.* It is the recommendation `01-objective-and-ground-truth.md`
already makes ("Restrict AOT to non-resumable functions initially... Recommended
as the Phase 14 starting point"), it costs no ABI, and it makes the boundary a
DIAGNOSTIC rather than an absence. It also composes: `wrap_promise` lands under
it, because an `await`-free async function is not a suspending function.

*Failure mode.* A future `case` for a suspending opcode, added without a
lowering, would silently pass the pre-walk scan if somebody moved the scan.
`body_is_supported`'s allow-list is the second net and is unchanged.

*What it is NOT.* It is not a claim that generators are rare. They are not:
598 of Babylon's functions are refused for `yield_value`, which is more than
every other refusal reason in that corpus put together. Design (a) is cheap and
honest, not sufficient.

### (b) A state-machine transform: `ctjs-lower-suspension`

The master plan's design (`12-phases-13-14-coverage-async.md`, "Implementation —
State Machine Lowering"): find every `CTJSMaySuspend` operation, split its
block, run `mlir::Liveness`, assign each live value a deterministic slot in a
state object, rewrite the entry into a `cf.switch` over a resume index, spill
and `ctjs.frame_exit` at each suspension, reload and `ctjs.frame_enter` at each
resume block.

*Cost.* A new pass, a new state-object layout, an ABI for a compiled entry that
can be re-entered at a resume index (`ct_aot_entry_fn` takes no such
parameter), a `CT_AOT_SUSPENDED` status added to the shared enum — which the
`await_settled` row warns "makes every ctcompile status switch non-exhaustive
at once" — and a variant payload on `coroutine_object` plus a GC mark path for
it. It also has to interact with the try/catch work: the importer refuses more
than one protected region per function today, so `await` inside `try/finally` —
the master plan's first targeted test case — is refused for a second,
unrelated reason.

*What makes it the right long-term answer.* It reproduces the interpreter's
representation exactly: the spilled state IS a register window, the resume
index IS `ip`, and `coroutine_object` can hold both by construction rather than
by translation. Nothing in the runtime's resume path has to learn a second
mechanism.

*Failure mode, and it is the one the master plan names.* Liveness across
exception edges. "A value that is live on the throwing path but not spilled...
manifests as a corrupted local visible only when an exception crosses an
`await`." This is why the plan insists on `mlir::Liveness` rather than a
bespoke analysis, and it is right.

*Second failure mode, which the master plan does not name.* **A compiled
generator resumes at a NEW base**, because `generator_resume` appends the
window to `registers_` wherever it happens to be. Every parked slot index a
compiled body uses is relative to `ct_aot_slots(fr)`, so that is survivable —
but any design that hands a compiled body a raw index into `registers_` is not.
The argument-window design under discussion on the `arguments`/rest track
(`pending_argv_base_`) is exactly such a design, and its own plan notes that a
resumed compiled generator would break it.

### (c) C++20 coroutines in the emitted C++

The emitted standard is a knob: `compile-js-to-cpp.cmake` hands the generated
translation unit to the ordinary host compiler and the project already builds
at C++23, so `co_await` is available for free and the compiler does the
liveness analysis, the state layout and the resume dispatch.

*Why it is refused, and the master plan refuses it in one sentence for the
right reason.* "Both impose their own frame layout and ABI." Concretely:

* **The coroutine frame is heap-allocated by the compiler and is opaque.** The
  collector cannot walk it. Every live `value` at a suspension point would have
  to be parked into `registers_` anyway — which is the state-machine transform's
  spill, done twice.
* **`coroutine_object` cannot hold a `std::coroutine_handle`** without becoming
  a variant, which is design (b)'s cost with none of its benefit: the payload
  would be opaque to `generator_resume`, so `.throw()`, `.return()` and the
  handler rebasing would each need a second implementation.
* **It applies to ONE backend.** EmitC is primary but the LLVM dialect is the
  second backend (Phases 11–12A) and would need the transform regardless. A
  suspension mechanism that exists in one backend is two semantics to keep
  correct, which is the thing this project's ABI table exists to prevent.
* **It is a per-function cost even when unused.** A C++ function containing a
  single `co_await` is a coroutine end to end: its return type changes, its
  entry allocates, and `ct_aot_entry_fn`'s `int32_t (*)(...)` signature no
  longer describes it.

*One thing it would genuinely give.* Correct liveness for free, including
across exception edges — which is design (b)'s named failure mode. That is
worth remembering if (b) proves hard, but it is not worth two suspension
mechanisms.

### (d) Heap-allocate every AOT frame so suspension is uniform

Listed in `01-objective-and-ground-truth.md` as "cleanest in the long run,
largest change to the ABI". It is not a candidate for this phase: the shadow
frame is 32 bytes and caller-allocated precisely so that its layout stays
Phase 4's to change, and making every compiled call heap-allocate to serve the
minority that suspend inverts the cost.

## Phase 58 landed the generator TYPE, and measured the phase out of its own ordering

Added 2026-09-01 by the Phase 58 track (`24-native-cpp-backend.md`, the NATIVE
backend). It does not change anything above; it settles two things this document
left open and contradicts one thing it concluded.

### What landed

* **`ctbrowser/include/ctbrowser/aot/generator.hpp`** - `ctnative::generator<T>`
  over C++20 `<coroutine>`: a lazy, move-only, single-pass input range, with the
  move-only / input-range / not-forward-range invariants written as
  `static_assert`s rather than as tests. `namespace ctbrowser::ctnative`, not a
  bare global `ctnative`; the emitter is expected to put `namespace ctnative =
  ctbrowser::ctnative;` in the generated TU's preamble so generated code reads
  the way the specification writes it.
* **`ctcompile/test/NativeGenerator.cpp`** (`ctcompile_native_generator`, added
  as one appended block at the end of `ctcompile/test/CMakeLists.txt`) - four JS
  generator shapes run in the interpreter and compared against hand-written
  coroutines. Two must agree, and **two are declared divergences whose exact
  shape the test asserts**. It is outside the MLIR guard: it compiles no IR.

**Nothing lowers.** The `ctnative` dialect does not exist yet, and the
importer's `may_suspend` pre-walk (design (a) above) still refuses every
`function*` for the reason it already gives. Stage 58B is a refusal, and the
paragraphs below are why that is the right answer rather than an unfinished one.

### The toolchain fact is about the LIBRARY, not the compiler

`24-native-cpp-backend.md` §2 says `<generator>` is absent from "GCC 13.3.0 and
clang 18.1.3". Measured again today, with a third compiler:

| compiler on the devbox | `__cpp_lib_generator` |
|---|---|
| g++ 13.3.0 | absent |
| clang++ 18.1.3 (Ubuntu) | absent |
| clang++ 22.1.8 (brew) | **absent** |

The brew clang is four major versions newer and still answers no, because all
three use the system **libstdc++ 13.3** headers and libc++ is not installed.
Upgrading the compiler does not fix this; installing a newer libstdc++, or
libc++, would. The header records that, and its detection macro tests
`__cpp_lib_generator` rather than `__has_include(<generator>)` alone, because a
header can exist while the feature does not.

**And adoption is a separate switch from detection**, which is a correction to
the plan's wording. "Adopt `std::generator` behind `__has_include`" taken
literally means the type silently differs between the devbox and any newer
machine - `std::generator<T>`'s reference type is `T&&` where this one's is
`T&`, so `auto & x = *it` compiles on one and not the other. That is the same
machine-dependence the instruction exists to prevent, pointed the other way. So
`CTNATIVE_HAS_STD_GENERATOR` is detected automatically and
`CTNATIVE_USE_STD_GENERATOR` is set by hand.

### The ordering argument is right about the numbers and wrong about what follows

This document's strongest finding - repeated in `23-lexical-implementation.md`
Appendix A.6 and in `24-native-cpp-backend.md` Phase 58 - is *"Babylon has 598
generators and zero awaits, so generators first."* The corpus was re-measured
today, this time by reading the bundle rather than by counting refusals:

| measurement over `ctbrowser/vendor/babylon/babylon.js` | count |
|---|---:|
| `function*` bodies | **622** |
| of those, passed as the fourth argument to `ct(...)` | **622** |
| `function*` bodies that are anything else | **0** |
| `async function` | 0 |
| distinct source prefixes before `function*` | 9, every one a `ct(` call |

`ct` is TypeScript's `__awaiter`, minified. Its body is the helper verbatim:

```js
function ct(e, t, i, n) {
  return new (i || (i = Promise))(function (r, s) {
    function a(e) { try { l(n.next(e)) } catch (e) { s(e) } }
    function o(e) { try { l(n.throw(e)) } catch (e) { s(e) } }
    function l(e) { e.done ? r(e.value) : (t = e.value, ...).then(a, o) }
    l((n = n.apply(e, t || [])).next())
  })
}
```

So this document's own open item - *"Not verified: WHY Babylon's 598 generators
are generators"* - is now verified, and the inference in it was correct: they
are transpiled `async`/`await`. The evidence it offered was not. It looked for
`__awaiter` and `__generator` **by name**, and there are zero of either string
in the bundle: the helper is minified to two letters, and the `__generator`
state-machine helper is not used at all, because this is downlevel-to-ES6 rather
than downlevel-to-ES5.

**What that costs Phase 58 is the whole phase.** `__awaiter` drives its
generator with all three of the features a C++ `generator<T>` does not have:

1. `n.next(e)` - **a value sent in**. In the body this is `const x = yield p`,
   the awaited result. `co_yield` produces nothing: the promise type's
   `yield_value` returns `suspend_always`, and that is true of `std::generator`
   too, by specification - so adopting the standard type would not change it.
   There is no C++ expression that receives the sent value.
2. `n.throw(e)` - **an exception injected at the suspension point**, which is
   how a rejected promise becomes a `throw` inside the async function. A C++
   coroutine can be resumed and destroyed; it cannot be resumed *throwing*.
3. `e.done ? r(e.value)` - **the generator's return value**, read off the done
   record. `return_void` is what `std::generator` has, and no C++ range can
   observe a return value at all.

So the mapping `function*` -> coroutine, `yield` -> `co_yield` compiles **zero
of Babylon's 622 generators**. p5, phaser and bootstrap have none at all - their
`function *` text hits are all inside comment prose. Across every corpus in this
tree, the population Stage 58B's clean lowering would newly compile is **zero**.

**The corrected ordering:** the 598 refusals are not a generator problem that
happens to look like async, they are *async wearing a generator's clothes*, and
they need design (b), the state machine, for exactly the reasons `await` does.
"Generators first" is still right as a statement about which OPCODE the
suspension transform should handle first - `op::yield_value` is what those
functions contain, and `op::await_value` is not. It is wrong as a statement
about which PHASE is cheap. Phase 58 is not a shortcut past Phase 14; it is a
different feature, and this tree's corpora do not use it.

### Where the mapping IS exact, said precisely

Worth writing down because it is the eligibility predicate a future Stage 58B
proof has to discharge, and because the two agreeing cases in the new test are
exactly it:

* A generator whose `yield` expression's **value is discarded** (`yield e;` as a
  statement) maps exactly.
* A generator that **falls off its end, or executes a bare `return;`**, maps
  exactly.
* Consumption through **`for...of`** maps exactly - `for...of` cannot see a
  returned value in JavaScript either, so nothing is lost there. Consumption
  through explicit `.next()` is lossy at exactly one field, which is what the
  `earlyValue` case measures.
* `yield*` needs nothing extra: `op::yield_value` is the only yield opcode, so a
  delegating yield is already a loop. (The dialect's unreachable
  `SuspendKind::yield_star` enumerator, flagged above, is still unreachable.)
* **Resuming a finished coroutine is undefined behaviour**, where "a generator
  that has finished keeps answering" is free in JavaScript. Any `.next()`
  emulation has to carry that check; `record_native` in the test does, and it is
  commented as a cost rather than as a detail.

### Candidate (c) is refused for the BOXED backend, and that is not this question

The "C++20 coroutines in the emitted C++" candidate above is refused with four
arguments. Three are about the boxed backend specifically - the collector cannot
walk an opaque coroutine frame, `coroutine_object` cannot hold a
`std::coroutine_handle` without becoming a variant, and `ct_aot_entry_fn`'s
signature no longer describes a coroutine - and **the first does not apply to
the native backend at all**, because a native-subset function's values are
`double`, `std::string` and structs, not GC-managed `value`s. There is nothing
in such a frame for the collector to find.

That does not rescue candidate (c); it relocates it. The surviving objections
are the two structural ones: a coroutine's return type is not
`ct_aot_entry_fn`'s, so a compiled generator cannot be dispatched through
`proto->aot_entry` as it stands, and the mechanism would exist in one backend
only. **A native generator therefore needs an entry point of its own, and that
is the first thing Stage 58B has to design** - not the `co_yield` lowering,
which is the easy half and is why the phase looks cheaper than it is.

### The §1.4 ratio

`def` lines of new TableGen: **0**. New C++: **258** lines of header and **309**
of test. That needs the sentence §1.4 demands, and it is the one §1.2 of part 23
already grants: neither file is IR. `generator.hpp` is a runtime header - the
C++ the emitter will *print*, which has no TableGen surface, exactly as the
emitter fork itself does not - and `NativeGenerator.cpp` is a test. **No
operation, attribute, type or pattern was added, so there was nothing this phase
could have written declaratively and did not.** The moment Stage 58B adds a
suspension operation or a generator type to a dialect, the rule applies to it in
full and this exemption stops covering it.

### What was falsified

Three mutations, each built on the devbox and run:

1. **`initial_suspend` changed to `suspend_never`** - an eager generator. `ids`
   and `early` both went red with the whole sequence shifted by one, and
   `earlyValue`'s divergence assertion went red too because the divergence moved
   from one field to four. The laziness the JS `.next()` protocol requires is
   load-bearing and the test sees it.
2. **`echo_native` changed to yield an increasing sequence** rather than a
   constant. The two answers still differ - so a test that only asserted "these
   differ" would have stayed green - and the case went red, because the
   assertion pins the field COUNT and the FIRST DIFFERING INDEX, which moved
   from (5, 1) to (4, 2). A declared divergence has to be pinned that tightly or
   it certifies any wrong answer at all.
3. **One `must_agree` call removed.** The first attempt at this was not a
   falsification: deleting the call left `early_native` unused and the build
   failed on `-Werror,-Wunused-function` before any test ran, which is the
   warning set doing its job and says nothing about the counter. Re-done with
   both sides still computed and only the comparison dropped, the counter fired
   alone - three cases printed pass, nothing printed FAILED, and the test still
   exited 1 with "expected 4 comparisons, 2 agreements and 2 declared
   divergences; got 3, 1 and 2".

## Recommendation

**Design (a) now, design (b) next, and (c) never — and when (b) is written it
must do `yield` FIRST, not `await`.** That last clause is the measurement's
doing: Babylon's 598 generators against its zero awaits mean a suspension
transform that starts with `await` would compile nothing new on the biggest
corpus in the tree. Three reasons for the ordering, in order of weight.

1. **(a) is not a placeholder for (b) — it is the part of (b) that is already
   true.** Whatever suspension mechanism lands, a function is either transformed
   or refused, and something has to decide which. The `may_suspend` scan IS
   that predicate, derived from the table so a third suspending opcode joins it
   automatically. Design (b) replaces the refusal's BODY, not its position.
2. **(b) cannot start until `ctjs.check`/`push_handler` can nest.** The master
   plan's own list of targeted cases opens with "await inside try/finally" and
   "yield inside a loop inside try/catch"; the importer refuses a function with
   more than one protected region today, and try/finally is two. Building the
   suspension transform first would produce a pass whose interesting cases are
   all refused upstream, which is untestable in exactly the way this project has
   already been burned by.
3. **`wrap_promise` is worth landing on its own and (b) does not have to wait
   for it.** It is one row, one shared `context` member, one op and two lowering
   halves, and it makes every `await`-free async function compilable — which
   `ct_aot_wrap_promise`'s row predicted in as many words ("Has REAL tier-1
   callers").

### What the dialect's existing sketch is worth

`ctjs.suspend`, `ctjs.resume_point` and `ctjs.resume_throw` are declared in
`CTJSOps.td` with no lowering. Assessed one at a time:

* **`ctjs.suspend` is RIGHT and should be kept.** `SuspendKind` is
  `await | yield | yield_star`, it carries `CTJS_MaySuspend`, and design (b)
  step 1 is "find every operation carrying `CTJSMaySuspend`" — a trait-driven
  search the operation already satisfies. Its one operand and one result are
  the right shape: the awaited/yielded value in, the sent value out.
  **One correction it needs:** `yield_star` has no opcode. `op::yield_value` is
  the only yield opcode in `bytecode_opcodes.def`, and `yield*` compiles to a
  loop around it. The enumerator is currently unreachable from any bytecode.
* **`ctjs.resume_point` is RIGHT and its verifier is better than it looks.** It
  already enforces indices unique and dense within a function, which is exactly
  what design (b) step 5's `cf.switch` needs — "a duplicate index means two
  states that cannot be told apart while a gap means a switch arm that resumes
  nowhere." That verifier is written and passing today with zero producers.
* **`ctjs.resume_throw` is MISNAMED and is not about suspension at all.** Its
  own description says it is "what the end of a `finally` block does on the
  thrown completion" — a re-throw of the pending completion record. It belongs
  to try/finally, not to Phase 14, and it sits under a section heading that says
  "Suspension". `BytecodeImport.cpp`'s finally refusal already cites it as a
  reason it refuses finally. **The heading was the stale part**, and this work
  corrected it: the section is now "Async and suspension — Phase 14", and
  `ctjs.resume_throw` should move out of it to sit beside `ctjs.throw` when
  finally lands.

## Step list, file by file

### Landed with this document

The order is the project's established one, and each step is chosen so the
previous one cannot be spelled two ways.

**1 — `ctbrowser/include/ctbrowser/script/vm.hpp` and
`ctbrowser/lib/Script/vm/run_loop.cpp`: lift the handler into a shared
`context` member.** `value context::wrap_in_promise(value v)` — the
already-a-promise test AND `make_promise`, in one place, with the three facts
the ABI row demands written above it. `VM_CASE(wrap_promise)` becomes
`reg(in.a) = wrap_in_promise(reg(in.a));`. First, so the two tiers cannot
drift — the move `make_closure`, `construct_new`, `iterable_values`,
`has_property`, `instance_of`, `delete_index` and `own_keys` have all already
made.

**2 — `ctbrowser/lib/Script/aot_bridge.cpp`: the static plus the `extern "C"`
wrapper.** `aot_bridge::wrap_promise` is `cx.wrap_in_promise(...).bits()` and
**returns the value plainly with no status test** — raise tier, following
`ct_aot_cell_new`'s body exactly. No new ABI row: the row was written in Phase
2 and had no body.

**3 — `ctbrowser/include/ctbrowser/aot/aot_helpers.def`: repair the row's
citations by name.** Not cosmetic — the semantics MOVED in step 1, so the
`DELEGATES TO` paragraph was going to be wrong either way, and every line
number in it was already wrong before that. See *What could not be verified*.

**4 — `ctcompile/include/ctcompile/CTJS/IR/CTJSOps.td`: `CTJS_WrapPromiseOp`.**
`CTJS_RuntimeOp<"wrap_promise", "ct_aot_wrap_promise", [CTJS_MayThrow,
CTJS_Safepoint]>`, one `CTJS_ValueType` operand, one result.
**Not `CTJS_GenericEffects`**: the row is `may_reenter 0`, so
`CTJS_MayReenterJS` would be a lie the ABI shape trait does not check.
`CTJS_CreateCellOp` is the pattern. It passes `verifyABIShape`: roles are
`{frame, value}`, so zero out-parameters and a value return means exactly one
result and at most one inherent attribute, and it declares none.

**5 — `ctcompile/lib/CTJS/Import/BytecodeImport.cpp`: one case and one
refusal.** `case op::wrap_promise:` reads `in.a` and writes `in.a` — the
compiler emits `{op::wrap_promise, r}` over the register the return value is
already in, and `b` and `c` are `unused` in the opcode row.

The refusal is a **pre-walk scan over a `may_suspend` table expanded from
`bytecode_opcodes.def`**, with a `static_assert` that the table's length matches
`opcode_count`, exactly as the file's existing `opcode_names` table does. Two
reasons it is not a `case` label, and both are load-bearing:
`ctcompile_importer_coverage`'s matcher reads a `case op::x:` as coverage and
carries a sanity check that FAILS if `op::await_value` is ever seen as
dispatched; and the reason belongs to the function rather than the instruction,
because nothing in a body containing an `await` is compilable, including the
parts before it. It reports the FIRST suspension point's offset so the
diagnostic names something a person can go and read.

**6 — `ctcompile/lib/CTJS/Lowering/CTJSToEmitC.cpp`: BOTH halves.**
`body_is_supported`'s allow-list gains `WrapPromiseOp`; `convert()` gains a
branch emitting `ct_aot_wrap_promise(frame, v)` and mapping the result. Adding
one without the other has happened four times and now reaches
`llvm::report_fatal_error` — **but only if an operation actually arrives at
`convert()`**, which is what the fixture is for.

**7 — the tests, in the same commit.**

* `ctcompile/test/ImporterCoverage.cpp` — the `wrap_promise` row deleted from
  `not_yet`, which the ratchet demands.
* `ctcompile/test/CTJS/Import/async.mlir` — a new lit test asserting BOTH
  halves: `async function settles(a)` produces a `ctjs.func` containing
  `ctjs.wrap_promise`, and `async function waits(p)` produces none and a
  `ctjs.skipped` row whose reason begins `"a suspension point"`. Two RUN lines
  rather than one, because `ctjs.skipped` is a MODULE attribute and prints
  above every function — a single ordered CHECK sequence would be asserting the
  printer's layout.
* `ctcompile/test/linkable.js` — `async function settles(a)`, so the new symbol
  is link-checked.
* `ctcompile/test/differential.js`, `Differential.cpp` and the `-DENTRIES=`
  list in `ctcompile/test/CMakeLists.txt` — all three, or the entry is compiled
  and never declared, or declared and never compiled.

### Not landed, and what it needs first

**8 — nesting for `ctjs.check` / `push_handler`.** Blocks step 9's testable
cases. Not this track's.

**9 — `ctjs-lower-suspension`**, per the master plan's seven steps, plus the
three things the master plan does not list: `CT_AOT_SUSPENDED` in
`ct_aot_status` (and the resulting non-exhaustive switches), a resume-index
parameter or a second entry point in `ct_aot_entry_fn`, and a variant payload
on `coroutine_object` with a GC mark path. Delete
`ct_aot_suspend_unsupported`'s row when it lands, as its own row instructs, and
delete both named exemptions in `ctcompile/test/Inventories.cpp`.

**10 — copy `is_async` onto `function_proto` and into
`engine_contract::proto_header`.** `ct_aot_suspend_unsupported`'s row is right
that Phase 3's dispatch and ctcompile "must each derive the predicate and
CANNOT derive it from the same data". It is a program-image change —
`ProgramImage.cpp` serialises the proto and `image_fingerprint` covers it — so
it is not free, and it is not needed while design (a) holds, because the
importer's refusal means no suspending proto ever gets an `aot_entry` to
dispatch to. **It becomes necessary the moment design (b) lands**, and it should
land with it rather than before.

## The differential fixture

Three bodies and two arms, chosen so that each plausible mistake changes an
answer.

```js
// ASYNC WITHOUT await - op::wrap_promise, the only half of async that does not
// suspend, and therefore the only half a compiled C++ stack frame can do.
//
// THEY ARE READ THROUGH __value AND __settled ON PURPOSE. Those are own
// properties of the promise the runtime's own factory makes - not an API - and
// reading them is what lets this compare the OBJECT rather than whatever a
// `.then` chain would eventually deliver. A driver that awaited would need the
// microtask queue and would be testing the event loop instead.
async function wrapped(a) { return a + 1; }

// RETURNING A PROMISE MUST NOT NEST IT, which is the already-a-promise test.
// `passes(p) === p` is the separator: a lowering that dropped the test answers
// false and every other field here stays right.
async function passes(p) { return p; }

// AND is_object() IS heap_kind::object EXACTLY, so an ARRAY returned from an
// async function is ALWAYS re-wrapped. A lowering that used is_object_like -
// which is what "is it already a promise" reads like in English - would pass
// the array straight through, and then __value is undefined instead of the
// array.
async function arrayOut() { return [7, 8]; }
```

```js
  if (which === 50) {
    var p = wrapped(1);
    OUT = "" + (typeof p) + "/" + p.__value + "/" + p.__settled + "/" + p.__rejected;
  }
  if (which === 51) {
    var q = wrapped(2);
    OUT = "" + (passes(q) === q) + "/" + arrayOut().__value[0] + "/" +
          (typeof arrayOut().__value);
  }
```

| arm | anchor | what it separates |
|---|---|---|
| 50 | `object/2/true/false` | the wrap existing at all — a body returning its value raw answers `number/2/undefined` and the second field still looks nearly right |
| 51 | `true/7/object` | the already-a-promise test (dropped, `===` is false) from the EXACTNESS of `is_object()` (widened to `is_object_like`, an array passes through and `__value` is undefined) |

**50 and 51 rather than 41 and 42**, and the gap is deliberate: the
`arguments`/rest track is landing arms 40–43 on a branch of its own, and two
tracks numbering the same arm differently is a merge that compiles and tests
the wrong thing.

**WHAT NO CASE HERE COVERS, said plainly.**

* **The identity path when no promise factory is installed.**
  `install_builtins` always calls `install_promise`, so `make_promise` is never
  the identity in any test in this repository. A context without builtins is a
  configuration the ABI row names and nothing exercises.
* **The pending-promise pass-through.** A pending promise carries an own
  `__value` of `undefined`, so `wrap_promise` leaves it alone — but building one
  needs `pending_promise_factory_` and a settler, and observing the difference
  needs the microtask queue. This is the case where `wrap_promise`'s `__value`
  test and `is_pending_promise`'s `__settled` test genuinely disagree, and it is
  untested.
* **Rooting across a collection.** `ct_aot_wrap_promise` is `is_safepoint 1` and
  allocates, so a `gc-roots.js` case looks called-for. It would prove nothing:
  the compiler emits `wrap_promise` **immediately before `ret` on every path**,
  and nothing between them is a safepoint — `ct_aot_return_value` is `(0,0,0)`
  and `ct_aot_leave` is not a safepoint either. There is no live value to lose.
  No case was added rather than adding a vacuous one.
* **The refusal message itself is covered by a lit test, not by the
  differential.** A differential arm cannot see a refusal: a refused function is
  interpreted in both tiers and they agree perfectly.

## What was falsified, and what the falsification found

Every new guard was removed and its test watched. Two went red, one did not and
had to be re-done, and one caught a defect in this document's own prose.

**1 — the compiled tier's wrap, deleted.** `aot_bridge::wrap_promise` changed to
`return v;`. Both new differential arms went red, in the per-case pass AND in
the all-entries-installed pass:

```
wrap_promise FAILED
    interpreted object/2/true/false
    compiled    number/undefined/undefined/undefined
promise shapes FAILED
    interpreted true/7/object
    compiled    true/undefined/undefined
AOT: 45 arms with all 49 entries installed, 2 disagreed
```

**And arm 51's FIRST field survived it.** `passes(q) === q` answered `true`
under a tier that wraps nothing, because two unwrapped values are trivially
identical. The `===` test separates a lowering that *nests* — which is what it
was written for — and separates nothing about a lowering that does not wrap at
all. The second and third fields are what caught this one. Recorded because a
one-field version of that arm would have been vacuous against exactly the
mutation a reader would try first.

**2 — the refusal message, replaced with the old vague one.**
`ctcompile_lit` went red on `CTJS/Import/async.mlir` and nothing else, so the
new lit test is load-bearing.

**It took two attempts, and the first one is the finding.** Replacing only the
TAIL of the reason string left the message still beginning `"a suspension
point"`, and `REFUSED-SAME: reason = "a suspension point` is a prefix match, so
the test passed against a mutated binary. A `CHECK-SAME` on a long string
asserts its head and nothing else — which is a property of the check, not of
the message, and would be equally true of any other string check in this
directory.

**3 — `WrapPromiseOp` removed from `body_is_supported`'s allow-list.** The
BUILD fails, not a test:

```
compile-js-to-cpp.cmake: no compiled entry named wrapped in the output -
the backend refused it.
```

That is the `-DENTRIES=` list doing its job, and it is why all three of
`differential.js`, `Differential.cpp` and `test/CMakeLists.txt` have to move
together.

**4 — not a deliberate mutation, but the strongest result here.**
`ctcompile_importer_coverage`'s sanity check fired on the FIRST draft of the
comment above the new refusal, which spelled out a `case` label naming a
suspending opcode and was therefore read by the matcher as a dispatch:

```
the matcher claims op::await_value is dispatched, which would mean it is
matching prose - every mention would then count as coverage
```

The guard is load-bearing, it caught prose exactly as it was written to, and
the comment is paraphrased now with a note saying why.

## Risks

**An async function is dispatched exactly like an ordinary one, and only the
importer stops it.** `function_proto` has no `is_async`, so
`enter_compiled_body` cannot refuse a suspending body the way it refuses a
generator. If some future path installs an `aot_entry` on a proto whose body
contains `await` — a hand-written test entry, a per-function AOT override,
a partial import — native code runs off the end of a suspension point with no
diagnostic. `ct_aot_suspend_unsupported` exists to turn that into a named fault
and **has no body**, so today the diagnostic does not exist either. Giving it
one is one function and is worth doing before step 9 rather than during it.

**"Does not suspend" is not the same as "suspension-neutral", and a compiled
frame is on the wrong side of that.** `op::apply`'s row spells it out: the
instruction "can still set the VM-wide `suspended_` flag and corrupt an
enclosing `resume()`". A compiled body calling `ct_aot_call` into an interpreted
async function that awaits gets a promise back correctly — the callee is the top
frame and pops itself — but leaves `suspended_` true. Nothing between there and
`context::resume` reads it, which is why this is a risk and not a bug today.

**`CT_AOT_SUSPENDED` entering `ct_aot_status` breaks exhaustiveness everywhere
at once.** The row says so. Whoever lands step 9 should expect the first build
after adding the enumerator to fail in every file that switches on a status,
and should treat that as the enum working rather than as a problem.

**A compiled generator would break the `arguments` track's window design.**
`make_generator` keeps `saved->argc` while truncating `saved->window` to
`frame_size`, and a resumed generator's frame is at a NEW base. Any design
handing a compiled body a raw index into `registers_` is unsafe the moment step
9 lands. It is unreachable today because both call paths test
`target.is_generator` before asking `enter_compiled` — but that is two guards in
two files, neither of which mentions AOT.

## What could not be verified

**The MLIR half of this change is not built by `tools/remote-build.sh`, and
neither is anybody else's.** The `default` preset does not set
`CTCOMPILE_ENABLE_MLIR`, which defaults to `OFF`, so the 108-test devbox run
builds `ctcompile`'s comparators, inventories and CLI and **not** the dialect,
the importer, the EmitC backend, the lit suite, `ctcompile_differential`,
`ctcompile_linkable` or `ctcompile_gc_roots`. It prints
`ctcompile: gc-roots test unavailable` and moves on.

A second configuration was stood up by hand for this work and everything above
was measured in it — 75/75 including all four — but **it does not configure
cleanly out of the box either**, and both obstructions are worth writing down
because the next person hits them in the first five minutes:

* `find_package(MLIR)` reaches `FindLibEdit.cmake`, which calls
  `check_include_file` — a C compile — and `ctcompile`'s `project()` declares
  `LANGUAGES CXX`. That is a hard CMake error, not a warning, and it does not
  depend on whether libedit is installed. Worked around with
  `-DHAVE_HISTEDIT_H:INTERNAL=0`, which makes `check_include_file` skip its
  own probe. The real fix is one word in a `project()` line and is not this
  track's file.
* `LLVM_EXTERNAL_LIT` defaults to a `llvm-lit` inside brew's Cellar that does
  not exist, so `add_lit_testsuite` warns and the target is wrong. Worked
  around with `-DLLVM_EXTERNAL_LIT=` pointing at the box's `lit` venv.

The whole invocation, recorded so it is not re-derived:

```
cmake --preset default -B ../build-mlir -DCTCOMPILE_ENABLE_MLIR=ON \
  "-DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew;/home/linuxbrew/.linuxbrew/opt/llvm" \
  -DCTBROWSER_BUILD_EXAMPLES=OFF -DHAVE_HISTEDIT_H:INTERNAL=0 \
  -DLLVM_EXTERNAL_LIT=$HOME/.lit-venv/bin/lit
```

**This deserves a preset**, so that the command which builds the compiler is as
short as the one that does not. `CMakePresets.json` is shared, so it was not
added here.

**`ct_aot_await_settled`'s citations are rotted and its behaviour is
unverified.** It cites `vm.hpp:822` for `is_pending_promise` (that line is
inside a comment about string-literal memoisation; the function is at
`vm.hpp:1139` before this change) and `run_loop.cpp:1207-1219` for the read path
(that is `VM_CASE(new_cell)` and its neighbours; the read path is at
`run_loop.cpp:823-890`). `ct_aot_suspend_unsupported`'s `vm.hpp:932` and
`vm.hpp:426` were not checked. Only the `wrap_promise` row was repaired, because
only its semantics moved; repairing rows for helpers with no bodies would be
rewriting prose about code nobody has written.

**`bytecode_opcodes.def`'s `await_value` row cites `value.hpp:127` for
`is_object()` and that one is CORRECT** — `is_object()` is at `value.hpp:127`.
Recorded because it is the only line number in this area that was.

**Not verified: whether `ctjs.suspend`'s `YieldStar` enumerator was ever
reachable.** `yield*` has no opcode of its own; the claim above that it compiles
to a loop around `op::yield_value` is inferred from the opcode table having no
other candidate, not read out of `compile/expressions.cpp`.

**Not verified: what a resumed compiled frame would cost.** Design (b)'s spill
and reload is `n` stores and `n` loads per suspension where `n` is the live set,
against the interpreter's one `std::vector` copy of `frame_size` words. Which is
cheaper is a measurement nobody has made, and the master plan asserts neither.

**Not verified: any claim in this document about the LLVM-dialect backend.**
Phases 11–12A are unwritten; the argument in candidate (c) that a C++20
coroutine mechanism "applies to ONE backend" is a statement about the plan, not
about code.

**~~Not verified: WHY Babylon's 598 generators are generators.~~ VERIFIED
2026-09-01, and the "some fraction are certainly ordinary `function*`" guess was
wrong: there is no such fraction.** All 622 `function*` bodies in the bundle are
the fourth argument to `ct(...)`, which is `__awaiter` minified. The inference
was right; the evidence offered for it could not have found it, because there
are zero occurrences of the strings `__awaiter` and `__generator` in the file.
See "Phase 58 landed the generator TYPE" above.

**Not verified: whether the 14 p5 functions this work made compilable actually
lower.** The corpus numbers above count what the IMPORTER accepts; a function
that imports can still be refused by `body_is_supported` for some unrelated
operation. The end-to-end population is smaller than 14 by an unmeasured
amount.

**`tools/format.sh --check` is red on `ctcompile-v1` for two files this work
did not touch** — `ctbrowser/lib/DOM/treebuilder.cpp` and
`ctbrowser/tools/ctdrive/ctdrive.cpp`, four lines between them. Reformatting
them would put unrelated churn in another track's files, so they were left
alone and this paragraph exists instead.
