# `arguments` and the rest parameter

`op::make_arguments` and `op::gather_rest` are the last two opcodes on Phase
13's list that are Phase 13's own work — `own_keys` and `delete_prop` landed
while this was being written, leaving `load_bigint` and these two — and they
share one blocker, which is not a missing helper:

**A COMPILED FRAME CANNOT SEE ARGUMENTS PAST `param_count`.**

Everything below was read rather than recalled. Where a fact cited by an
existing comment turned out to be false, that is recorded here as a finding
instead of being quietly worked around.

## What was measured

* **`aot_bridge::enter` never sets `call_frame::argc`.** It value-initialises
  `context::call_frame entered{}` and then assigns `proto`, `base`,
  `handler_base`, `receiver`, `new_target` and `closure` by name. `argc` keeps
  its default of 0, so `VM_CASE(make_arguments)`'s loop bound and
  `VM_CASE(gather_rest)`'s slot-path bound are both zero in a compiled frame.
* **`ct_aot_slots` does not point at the arriving arguments.** `enter` sets
  `register_base = cx.registers_.size()` and then grows the vector, so the
  frame's span begins ABOVE the caller's argument window, never at it.
* **The EmitC entry emitter reads `argv[i]` only for `i < declared`**, where
  `declared = body.getNumArguments() - implicit_arguments`. It binds
  `abi->getArgument(0)`, `(1)`, `(2)`, `(4)`, `(5)` and `(6)` — **argument 3,
  the ABI's `uint32_t argc`, is never bound at all.**
* **The importer seeds register `i` for `i >= param_count` with
  `ctjs.constant undefined`.** There is no SSA value anywhere in the module for
  an argument past the signature.
* **Both ABI rows take `slots` and `argc` as EXPLICIT PARAMETERS.** Neither
  reads them off the frame, which is what makes this solvable without changing
  `ct_aot_entry_fn`.
* **`aot_entry.h` is right about `argv` and it matters less than it looks.**
  "VALID ONLY UNTIL ct_aot_enter: that helper resizes `registers_` and may
  reallocate it, so a body copies what it needs first." The POINTER dies. The
  VALUES do not: `context::collect` marks `registers_` with
  `for (const value & v : registers_) { mark(v); }` — the whole vector, not a
  per-frame span — and every caller of `enter_compiled` passes
  `registers_.data() + new_base`, so the arriving window stays alive and rooted
  for the entire compiled call.

**That last pair is the whole design.** A pointer into a `std::vector` dies
when the vector reallocates; an INDEX into a vector that only grows does not.

## The two ABI rows, verbatim

From `ctbrowser/include/ctbrowser/aot/aot_helpers.def`:

```
/* MERGE REFUSED: the calls area fused this with gather_rest into one
 * ct_aot_arg_array. They are not one operation - this one has a SECOND
 * EFFECT, writing call_frame::arguments_object (GC root 7), and gather_rest
 * READS that side-slot on one of its two paths. A fusion either drops the
 * write (breaking gather_rest's first path) or adds it to gather_rest
 * (wrong). But the calls area's parameter correction is adopted and is the
 * important half: the interpreter reads the frame's LIVE slots (reg(i) for
 * i < argc), which are the parameter locals themselves, so the ABI must not
 * be founded on 'the incoming argv is immutable' - that holds today only
 * because both opcodes are emitted in the prologue. Hence `slots`, a frame-
 * local GC-rooted mutable array of size max(param_count, argc) that IS
 * where the parameters live. Sequencing: this runs BEFORE the parameter
 * prologue and claims a register an extra argument may sit in, so Phase 10
 * must not sink it below the prologue.
 * DELEGATES TO: context::make_array (vm.hpp:238) + the loop at
 *   run_loop.cpp:1083-1100, INCLUDING the frame side-slot write
 *   `vm_frame->arguments_object = list`.
 * FAILURE: RAISE TIER ONLY (the allocation ceiling). Plain value return;
 *   poll ct_aot_failed. No JS exception, no unwind. */
CT_AOT_HELPER(/* name */ ct_aot_make_arguments, /* ret */ uint64_t,
              /* params */ (struct ct_aot_frame *fr, const uint64_t *slots, uint32_t argc),
              /* may_throw */ 1, /* may_reenter */ 0, /* is_safepoint */ 1)
CT_AOT_COVERS(/* helper */ ct_aot_make_arguments, /* opcode */ make_arguments)

/* The consumer of the side-slot ct_aot_make_arguments writes; splitting a
 * producer from its only consumer across two areas is how one of them goes
 * missing, so both rows sit here. Both paths must be reproduced, not
 * merged: the handler picks the arguments_object when the body built one,
 * because building it claimed a register an extra argument may have been
 * in, and the compiler happening to emit a == b is not a general invariant.
 * `from` stays its own parameter and is not fused with argc - it indexes
 * the arguments ARRAY on one path and is a frame-slot offset on the other,
 * so it must not be lowered as a pure literal count.
 * DELEGATES TO: run_loop.cpp:774-798 - make_array, then either
 *   call_frame::arguments_object's items[from..] when it is an array, or
 *   registers_[base+i] for i in [from, argc).
 * FAILURE: RAISE TIER ONLY - the allocation ceiling, never a JS exception,
 *   and it does not unwind. Plain value return; poll ct_aot_failed. */
CT_AOT_HELPER(/* name */ ct_aot_gather_rest, /* ret */ uint64_t,
              /* params */ (struct ct_aot_frame *fr, const uint64_t *slots, uint32_t argc, uint32_t from),
              /* may_throw */ 1, /* may_reenter */ 0, /* is_safepoint */ 1)
CT_AOT_COVERS(/* helper */ ct_aot_gather_rest, /* opcode */ gather_rest)
```

**Tier: both are RAISE TIER.** `classify_return` in `RuntimeHelpers.hpp` maps a
`uint64_t` return to `return_role::value`, and the .def's own rule — a status
lives in the return register only when the return is `int32_t` AND the row takes
a frame handle — makes these two value returns with `may_throw` set. So each is
a plain call whose result is parked, exactly like `ct_aot_cell_new`, and a caller
polls `ct_aot_failed` at a back edge rather than testing a status. Both are
`may_reenter 0` and `is_safepoint 1`.

### What the MERGE REFUSED argument genuinely requires

It makes four claims and only two of them are requirements.

**REQUIRED, and verified.** *The two are not one operation.*
`VM_CASE(make_arguments)` writes `vm_frame->arguments_object` as well as
`reg(in.a)`, and `VM_CASE(gather_rest)` reads that field to choose between its
two arms. The only readers of `call_frame::arguments_object` in the whole tree
are that `if` and `mark(f.arguments_object)` in `context::collect`. Fusing them
really would have to drop the write or move it.

**REQUIRED, and verified.** *`from` must not be fused with `argc`.* On the
array arm it indexes `arguments_object`'s `items`; on the slot arm it is a
frame-slot offset. `compile_parameter_prologue` emits
`instruction{op::gather_rest, i, i}` — `a` and `b` are the same number in every
program the compiler produces — so the row's "the compiler happening to emit
a == b is not a general invariant" is a statement about future emitters, not
about today's bytecode. See the untestable-mutation note under Step 8.

**ASSERTED, and now false in one word.** *"writing `call_frame::arguments_object`
(GC root 7)"*. `GCRoots.def` lists `frame_arguments` eighth today; the closures
work inserted `pending_closure` above it. The ordinal has rotted. The field is
right, the number is not, and the fix is the one the .def's own header already
prescribes: cite by name.

**ASSERTED, and it is the load-bearing design instruction.** *"Hence `slots`, a
frame-local GC-rooted mutable array of size max(param_count, argc) that IS where
the parameters live."* This asks for a specific implementation, and it can be
honoured LITERALLY without a copy — see the recommendation. `op::call` fills
`registers_[new_base + i] = undefined` for `i` in `[in.b, target.param_count)`
before entering, and `context::call` fills
`for (i < max(param_count, args.size()))`. The arriving window in `registers_`
IS a frame-local, GC-rooted, mutable array of size `max(param_count, argc)`
where the parameters live. It is the same memory the interpreter reads.

**One thing neither row says and both need.** They are prologue-only in
practice: `compile_function_body` emits `op::make_arguments` before
`compile_parameter_prologue`, and the prologue emits `op::gather_rest` first
among its three passes. The row's sequencing warning ("Phase 10 must not sink it
below the prologue") is a constraint on a pass that does not exist yet.

## The VM handlers, verbatim

Both from `ctbrowser/lib/Script/vm/run_loop.cpp`. In scope there,
`base` is `vm_frame->base` and `reg(r)` is `registers_[base + r]`.

```cpp
        VM_CASE(gather_rest) do {
            {
                // The arguments past the declared parameters. They are still in
                // this (*vm_frame)'s registers - the caller wrote them there and the
                // callee's base IS the argument base - so this reads them in place.
                //
                // UNLESS the body also built an `arguments` object, which happens
                // before this and claims a register an extra argument may be in.
                // Then the (*vm_frame)'s copy is the one that still has them.
                value out = make_array();
                auto * rest = static_cast<array_object *>(out.as_heap());
                if (vm_frame->arguments_object.is_array()) {
                    const auto & held =
                        static_cast<array_object *>(vm_frame->arguments_object.as_heap())->items;
                    for (std::size_t i = in.b; i < held.size(); ++i) {
                        rest->items.push_back(held[i]);
                    }
                } else {
                    for (std::size_t i = in.b; i < vm_frame->argc; ++i) {
                        rest->items.push_back(registers_[base + i]);
                    }
                }
                reg(in.a) = out;
                break;
            }
        }
        while (0);
        VM_NEXT;
```

```cpp
        VM_CASE(make_arguments) do {
            {
                // The (*vm_frame) knows how many arguments ARRIVED; the proto only knows
                // how many were declared, and those are different numbers whenever
                // `arguments` is worth reading at all.
                value list = make_array();
                auto * items = static_cast<array_object *>(list.as_heap());
                items->items.reserve(vm_frame->argc);
                for (std::uint16_t i = 0; i < vm_frame->argc; ++i) {
                    items->items.push_back(reg(i));
                }
                // On the (*vm_frame) too: this claims a register that an extra argument
                // may be in, so whatever still needs the raw ones reads them here.
                vm_frame->arguments_object = list;
                reg(in.a) = list;
                break;
            }
        }
        while (0);
        VM_NEXT;
```

### Exactly which registers each touches

| | reads | writes |
|---|---|---|
| `VM_CASE(make_arguments)` | `reg(i)` for `i` in `[0, vm_frame->argc)` — **not** `in.a`, `in.b` or `in.c` as sources | `reg(in.a)`, and `vm_frame->arguments_object` |
| `VM_CASE(gather_rest)` | `in.b` as a START INDEX; then either `arguments_object`'s `items[in.b..]` or `registers_[base + i]` for `i` in `[in.b, vm_frame->argc)` | `reg(in.a)` |

`in.c` is unused by both, and `bytecode_opcodes.def` agrees: both rows carry
`c_kind unused`, `make_arguments` carries `b_kind unused`, `gather_rest` carries
`b_kind count`.

**The trap here is the opposite of `op::iterable`'s.** `make_arguments` has no
source operand to get backwards; its sources are registers `0..argc-1`, which is
a HIDDEN read the opcode row calls out ("b and c are unused but the instruction
has a HIDDEN read of slots 0..argc-1"). `gather_rest`'s `in.b` is a count, and
the compiler always emits it equal to `in.a`.

## Candidate designs for capturing the incoming window

### (a) `pending_argv_base_` / `pending_argc_` on `context` — an INDEX

`enter_compiled_body` sets a `std::size_t` index into `registers_` and a
`std::uint32_t` count; `ct_aot_enter` consumes them into the frame handle and
into `call_frame::argc`, then clears them. Two new ABI rows, `ct_aot_args(fr)`
and `ct_aot_argc(fr)`, hand the body the `slots` and `argc` its two helper calls
need.

*Cost.* Two `context` members, two stores per compiled call in
`enter_compiled_body`, two in `ct_aot_enter`, one field on
`aot_frame_storage`, two ABI rows, four bridge bodies. **No copy, no extra
frame slots, no change to the entry emitter's prologue.**

*It is NOT a GC root, and copying `pending_closure_` blindly would make it one.*
`pending_closure_` is a `value` reachable from nothing in its window;
`pending_argv_base_` is an integer, and the values it names are already inside
`registers_`, which `collect()` marks in full. Adding it to `GCRoots.def` would
add a row that traces an index.

*Failure mode 1.* `registers_` shrinking below `argv_base` while the compiled
frame is live. Every shrink site was enumerated: `aot_bridge`'s `leave`
truncates to that frame's own `register_base`; `call.cpp` truncates to `new_base`
in three places; `run_loop.cpp` truncates to `base` on an interpreted frame's
return and on a suspend. Every one of those bases is at or above ours, because
bases only grow down the stack. An unwind past our frame ends the compiled body
before either helper can run.

*Failure mode 2.* The pointer `ct_aot_args` returns inherits `ct_aot_slots`'
rule — "VALID UNTIL THE NEXT SAFEPOINT AND NOT ONE INSTRUCTION LONGER". A
backend that hoists it out of a loop containing a safepoint has miscompiled. It
must be emitted immediately before the call that consumes it.

*Failure mode 3.* The 13 hand-written `ct_aot_enter` call sites — see below.

### (b) Copy `argv` into a reserved run of frame slots, sized at run time

*It cannot be done by the body, which is the finding.* The destination does not
exist until `ct_aot_enter` returns, and `argv` is dead the moment it does. The
copy would have to be done BY `ct_aot_enter`, which can: `site` **is** the
`function_proto`, so it knows `param_count`; the pending pair gives it the
source; it resizes to `register_base + reg_count + max(param_count, argc) + 8`
and copies into `[reg_count, ...)`. The body then passes
`ct_aot_slots(fr) + window` and `abi->getArgument(3)`.

*Cost.* `max(param_count, argc)` extra register slots and an `argc`-long copy on
**every** compiled call, whether or not the body has either opcode. Gating it
needs a new `function_proto` flag, which is a program-image change:
`ProgramImage.cpp` serialises the proto and `image_fingerprint` covers it.

*Failure mode.* It is a SNAPSHOT, and the row exists specifically to refuse a
snapshot: "the ABI must not be founded on 'the incoming argv is immutable'".
This design re-introduces that assumption one level down — the copy is taken at
entry and a later backend that writes a parameter slot in place would diverge
from the interpreter, which reads live slots. It also makes `abi->getArgument(3)`
load-bearing, so the 32-bit/16-bit mismatch below becomes observable.

### (c) No new rows: the bridge reads the frame and `slots` is ignored

The cheapest to write and it should not be written. A declared parameter the
implementation ignores is exactly the class of drift `CTJSABIShape.cpp` exists to
catch, and it is the one instance of that class the trait **cannot** catch:
`slots` is materialised by the lowering, and the file says plainly there is no
operand-count rule and why. `ctjs.load_upvalue` carrying an `$index` the helper
had nowhere to put is the same defect, and it lowered silently for two commits.

## Recommendation

**Design (a).** Three reasons, in order of weight.

1. **It reproduces the interpreter's expression literally.** `VM_CASE(make_arguments)`
   reads `registers_[base + i]` where the callee's `base` IS the argument base.
   A compiled body reading `registers_[argv_base + i]` is the same expression
   against the same memory. Design (b) reads a copy, and the two are equal only
   under the assumption the row refuses to be founded on.
2. **It honours `slots` literally rather than by construction.** The window is
   already "a frame-local GC-rooted mutable array of size `max(param_count, argc)`
   that IS where the parameters live" — `op::call` and `context::call` both fill
   it to exactly that length with `undefined` before entering.
3. **It routes both tiers through one `argc`.** `ct_aot_argc(fr)` answers
   `call_frame::argc`, and `ct_aot_enter` sets that field with the same
   `static_cast<std::uint16_t>` truncation `context::call` writes. See the risks.

**What it costs a compiled function with neither opcode: four integer stores per
call and nothing else.** Two in `enter_compiled_body` (`pending_argv_base_`,
`pending_argc_`), two in `ct_aot_enter` (the frame handle's `argv_base`, the
`call_frame`'s `argc`), plus two clears. No extra register slots, no copy, no
extra call, no change to the entry prologue, and `abi->getArgument(3)` stays
unbound — so the entry emitter's long comment about reading `argv` before
`ct_aot_enter` stays true, because the extra arguments are never read through
`argv` at all.

### The 13 hand-written `ct_aot_enter` call sites

`ctbrowser/unittests/unit/aot_*.cpp` calls `ct_aot_enter` directly thirteen
times. Eleven are inside hand-written entry functions — `sample_leaf`,
`sample_caller`, `sample_point` (`aot_dispatch.cpp`); `sample_keeps`,
`sample_ctor` (`aot_gc.cpp`); `compiled_total` (`aot_program.cpp`);
`compiled_guarded`, `compiled_sloppy`, `compiled_middle`, `compiled_finally`
(`aot_throw.cpp`); `sample_addup` (`aot_basics.cpp`) — each installed on a
`function_proto::aot_entry` and reached only through `ctx.call`, which is
`context::call` → `enter_compiled` → `enter_compiled_body`. They get a correct
pending pair for free and none of them reads the window, so nothing changes.

**Two do not go through `enter_compiled_body`**, both in `aot_helpers.cpp`: the
frame the `ct_aot_binary_op_static` block enters over `tiny.functions[0]`, and
the one the truthiness block enters the same way. They exist only to have a
frame handle. Under design (a) they read whatever the pending pair holds; the
pair is cleared on consumption exactly as `pending_new_target_` and
`pending_closure_` are, and no compiled entry runs in that test at all, so both
see `argv_base 0, argc 0` — an empty window they never look at.

**No edit is required in any of the six files**, and asserting that is the
difference between the two designs: under (b), `ct_aot_enter` would resize and
copy for all thirteen, and those two would copy from a base nobody set.

## The step list

The order is the project's established one, and each step is chosen so the
previous one cannot be spelled two ways.

**1 — `ctbrowser/include/ctbrowser/script/vm.hpp` and
`ctbrowser/lib/Script/vm/run_loop.cpp`: lift both handlers into shared `context`
members.** First, so the two tiers cannot drift — the move
`make_closure`, `construct_new`, `iterable_values`, `has_property`,
`instance_of` and `delete_index` have all already made.

* `value context::make_arguments_object(call_frame & fr, const value * slots, std::uint32_t argc)`
  — `make_array()`, `reserve(argc)`, the loop over `slots[i]`, the side-slot
  write `fr.arguments_object = list`, return `list`. **The side-slot write is
  part of the member, not of the caller**, because it is the half a fusion would
  drop.
* `value context::gather_rest_values(const call_frame & fr, const value * slots, std::uint32_t argc, std::uint32_t from)`
  — both arms verbatim, the `arguments_object` arm first.
* `VM_CASE(make_arguments)` becomes
  `reg(in.a) = make_arguments_object(*vm_frame, registers_.data() + base, vm_frame->argc);`
  and `VM_CASE(gather_rest)` becomes
  `reg(in.a) = gather_rest_values(*vm_frame, registers_.data() + base, vm_frame->argc, in.b);`
* **The `slots` pointer must be computed at the call, not hoisted.**
  `make_array()` inside the member allocates; allocation can collect but does not
  resize `registers_`, so the pointer survives — that is a fact about
  `context::allocate` today, and it is the reason this is safe rather than an
  accident.

**2 — `ctbrowser/include/ctbrowser/script/vm.hpp`: the pending pair.**
`std::size_t pending_argv_base_` and `std::uint32_t pending_argc_`, beside
`pending_closure_`, with the comment saying why they are NOT in `GCRoots.def`.

**3 — `ctbrowser/include/ctbrowser/script/dispatch.hpp` and
`ctbrowser/lib/Script/dispatch.cpp`: `enter_compiled_body` gains
`std::size_t argv_base`.** Five `enter_compiled` call sites, every one of which
already has the number: `context::call`, the module-evaluation path and
`context::run` in `call.cpp`, and `op::call`'s and `op::construct`'s compiled
arms in `run_loop.cpp`. `context::run` passes 0 with `argc` 0.

**4 — `ctbrowser/lib/Script/aot_bridge.cpp`: `aot_bridge::enter` consumes them.**
`aot_frame_storage` gains `argv_base`; `entered.argc = static_cast<std::uint16_t>(cx.pending_argc_)`;
both pending fields cleared AFTER `frames_.push_back(entered)`, in the same
place and for the same stated reason as `pending_new_target_` and
`pending_closure_`.

**5 — `ctbrowser/include/ctbrowser/aot/aot_helpers.def`: two new rows.**

* `ct_aot_args(struct ct_aot_frame *fr) -> uint64_t *`, `(0, 0, 0)`.
  **`uint64_t *`, not `const uint64_t *`**: `classify_return` in
  `RuntimeHelpers.hpp` has an arm for `uint64_t *` and none for the const form,
  which would classify as `return_role::unknown` and trip `Inventories.cpp`'s
  assertion that nothing is unknown. `ct_aot_slots` sets the precedent. The row
  says it is read-only by contract and carries `ct_aot_slots`' safepoint rule.
* `ct_aot_argc(struct ct_aot_frame *fr) -> uint32_t`, `(0, 0, 0)`. It answers
  `call_frame::argc` — **not** the entry's own 32-bit `argc`. The row must say
  why: routing both tiers through one field is what stops them disagreeing.
* Neither carries a `CT_AOT_COVERS`; neither covers an opcode. `helper_count` is
  derived from the table, so 69 becomes 71 with no other edit.

**6 — `ctbrowser/lib/Script/aot_bridge.cpp`: four statics and four `extern "C"`
wrappers.** `args` and `argc` are one line each. `make_arguments` and
`gather_rest` delegate to the Step 1 members against
`cx.frames_[held.frame_index]` and **return the value plainly, with no status
test** — raise tier, following `ct_aot_cell_new`'s body exactly.

**7 — `ctcompile/include/ctcompile/CTJS/IR/CTJSOps.td`: two operations.**

* `CTJS_MakeArgumentsOp : CTJS_RuntimeOp<"make_arguments", "ct_aot_make_arguments", [CTJS_MayThrow, CTJS_Safepoint]>`
  — no operands, one `CTJS_ValueType` result.
* `CTJS_GatherRestOp : CTJS_RuntimeOp<"gather_rest", "ct_aot_gather_rest", [CTJS_MayThrow, CTJS_Safepoint]>`
  — `I32Attr:$from`, one `CTJS_ValueType` result.
* **Not `CTJS_GenericEffects`.** Both rows are `may_reenter 0`, so
  `CTJS_MayReenterJS` would be a lie the shape trait does not check.
  `CTJS_CreateCellOp` is the pattern.
* They pass `verifyABIShape`: `make_arguments`' roles are
  `{frame, values, count}` → 0 out-parameters and a value return → exactly 1
  result, at most 1 inherent attribute. `gather_rest`'s are
  `{frame, values, count, count}` → 1 result, at most 2 attributes. Zero
  operands is the normal case; the file explains at length why there is no
  operand-count rule.

**8 — `ctcompile/lib/CTJS/Import/BytecodeImport.cpp`: two cases.**
`set(in.a, MakeArgumentsOp::create(...))` and
`set(in.a, GatherRestOp::create(..., into.getI32IntegerAttr(in.b)))`.

**`in.b`, not `in.a`, and this mutation is UNTESTABLE from JavaScript.**
`compile_parameter_prologue` emits `{op::gather_rest, i, i}` and there is no
other producer, so the two fields are equal in every program the compiler can
build. This is `op::iterable`'s r0 defect in a form the `pad` trick cannot
reach: the aliasing is in the bytecode, not in the fixture. The guard is the
attribute's name and this paragraph, and the plan says so rather than pretending
a case covers it.

**9 — `ctcompile/lib/CTJS/Lowering/CTJSToEmitC.cpp`: BOTH halves.**

* `body_is_supported`'s allow-list — the `mlir::isa<CompareOp, ...>` run — gains
  `MakeArgumentsOp, GatherRestOp`.
* `convert()` gains two branches. Each emits `ct_aot_args(frame)` and
  `ct_aot_argc(frame)` immediately before the helper call, and maps the result;
  the existing per-operation rooting parks it.
* Adding one without the other has happened four times. It now reaches
  `llvm::report_fatal_error`, not `llvm_unreachable`, so it is loud in release —
  **but only if an operation actually arrives at `convert()`**, which is what the
  fixture in Step 10 is for.
* The entry emitter needs no change at all.

**10 — the tests, in one commit with the code.**

* `ctcompile/test/ImporterCoverage.cpp` — delete the `gather_rest` and
  `make_arguments` rows from `not_yet`.
* `ctcompile/test/linkable.js` — a body with `arguments` and a body with a rest
  parameter, so all four new symbols are link-checked. "Anything the backend
  learns to lower belongs here on the same day."
* `ctcompile/test/differential.js`, `Differential.cpp`, and the `-DENTRIES=`
  list in `ctcompile/test/CMakeLists.txt` — the four cases below. All three, or
  the entry is compiled and never declared, or declared and never compiled.
* `ctcompile/test/gc-roots.js` and `GCRoots.cpp` — an `arguments` array built by
  a compiled body, held only in a frame slot across a collection. Both helpers
  are `is_safepoint 1` and allocate.

## The differential fixture

Four bodies, chosen so that each plausible mistake changes an answer. All values
distinct, for the reason `total`'s leading `pad` exists.

```js
// `arguments`, WHOSE WHOLE POINT IS THE ARGUMENT THE SIGNATURE DOES NOT NAME.
// argsOf(7, 8, 9) reads index 2, which is past param_count and therefore has NO
// SSA VALUE anywhere in the imported body - the importer seeds every register at
// or above param_count with `ctjs.constant undefined`. A body answering from its
// declared parameters gets "3/7/undefined": the LENGTH right and the value
// wrong, which is why a case reading only arguments.length separates nothing.
function argsOf(a, b) { return "" + arguments.length + "/" + arguments[0] + "/" + arguments[2]; }

// A REST PARAMETER THAT IS NOT PARAMETER ZERO, which is the only shape that
// separates `from`. Written `restOf(...xs)` the operand is 0, and 0 is also what
// a lowering that dropped `from` would pass - the same r0 coincidence that let
// op::iterable read the wrong operand field and stay green. At index 2 an
// off-by-one answers "4/2/5" where "3/3/5" is right, and dropping `from`
// altogether answers "5/1/5".
function restOf(a, b, ...xs) { return "" + xs.length + "/" + xs[0] + "/" + xs[xs.length - 1]; }

// BOTH IN ONE BODY, which is the only case that reaches gather_rest's SECOND
// path. In the interpreter `arguments` is built into the local AFTER the
// parameters - register 2 here - which is exactly where the third argument
// arrived, so the slot arm would yield [2, [1,2,3]] rather than [2,3]. The
// compiled tier's window is never overwritten, so ITS two arms agree.
//
// THAT ASYMMETRY IS WHAT KEEPS THE COMPARISON FROM GOING BLIND. The two tiers
// hand context::gather_rest_values DIFFERENT `slots` - one contaminated, one
// not - so deleting the arm selection from the shared member changes the
// interpreted answer alone and the tiers disagree. This is the one case in this
// batch where shared code is still visible to the differential premise.
function bothOf(a, ...xs) {
  return "" + arguments.length + "/" + xs.length + "/" + xs[0] + "/" + xs[1] + "/" + arguments[2];
}

// ONE OBJECT PER CALL, NOT ONE PER MENTION. The bytecode builds it once into a
// local; a lowering that emitted ct_aot_make_arguments at each mention answers
// false, and every other case here would still pass.
function argsIdent(a) { var x = arguments; var y = arguments; return x === y; }
```

```js
  if (which === 40) { OUT = argsOf(7, 8, 9) + "|" + argsOf(7); }
  if (which === 41) { OUT = restOf(1, 2, 3, 4, 5) + "|" + restOf(1) + "|" + restOf(1, 2, 3); }
  if (which === 42) { OUT = bothOf(1, 2, 3); }
  if (which === 43) { OUT = argsIdent(1); }
```

| arm | anchor | what it separates |
|---|---|---|
| 40 | `3/7/9\|1/7/undefined` | an argument past `param_count` from one at it — **and** the reverse, `argsOf(7)`, where a count taken from `param_count` says 2 and a read past `argc` invents a value |
| 41 | `3/3/5\|0/undefined/undefined\|1/3/3` | `from` as its own operand from `from` fused with `argc` or dropped; the empty case pins `argc < param_count` |
| 42 | `3/2/2/3/3` | `gather_rest`'s array arm from its slot arm — through the INTERPRETED tier's clobber, which is the only tier that has one |
| 43 | `true` | one `arguments` per call from one per mention |

**Designed against the two failures this project has already had.** The first —
a mutation that named the same register by accident — is why every rest
parameter here sits at index 1 or 2 and every argument is distinct: `restOf(1,1,1,1,1)`
would answer `3/1/1` under a shifted window and under a correct one. The second
— a field written on both paths whose difference is never read — is why arm 42
exists and why the next paragraph is in this document rather than left to be
discovered.

**WHAT NO CASE HERE COVERS, said plainly.** In the compiled tier,
`ct_aot_make_arguments`' side-slot write to `call_frame::arguments_object` has
no observable consequence. The compiled window is never contaminated, so
`gather_rest`'s two arms compute the same array from the same origin, and the
array is separately rooted in a scope slot, so the GC root is redundant too.
Deleting the write from the compiled path alone would redden nothing. The only
thing that stops that is Step 1: the write lives inside
`context::make_arguments_object`, which both tiers call, so it cannot be omitted
on one side. That is the reason the lifting comes first, stated as a coverage
argument rather than as a style preference.

## Risks

**`call_frame::argc` is `uint16_t` and the ABI's `argc` is 32-bit, and the
interpreter already truncates.** `context::call` pushes
`static_cast<std::uint16_t>(args.size())`, so `f.apply(null, arrayOf70000)`
already reports `arguments.length` as 4464 in the VM. If the compiled tier took
its count from `abi->getArgument(3)` it would answer 70000 — **more correct, and
therefore a differential FAILURE**, because "when a CTJS operation and the
ctbrowser VM disagree, the VM is correct by definition." `ct_aot_argc(fr)`
answering `call_frame::argc` reproduces the truncation by construction. It is
not worth a fixture — a 70000-argument case would allocate 70000 registers to
prove a bug — so it is a comment on the row and a `static_cast` in
`aot_bridge::enter`, and it is written down here so the next person does not
"fix" it in one tier.

**A raise-tier helper must return a well-formed value on an unwound frame.**
`context::make_array` is `allocate<array_object>()`, and `ct_aot_cell_new`'s row
states the property both of these inherit: allocation "raises past the ceiling
and STILL returns a valid cell, so this returns a value plainly and never a
status. No try/catch can see it and there is no landing pad for it." A body that
returned a bare `0` on failure would hand back `value::from_bits(0)`, which is
not `undefined`, and the lowering would park it as a GC root and trace it.

**Landing `gather_rest` without `make_arguments` leaves half of `gather_rest`
dead — and landing both does not fix it.** As above, the `arguments_object` arm
is unreachable-in-effect from compiled code either way. Land them together
anyway, for three reasons that are not that one: `linkable.js` wants a body with
both; arm 42 needs both; and the `not_yet` row for `make_arguments` says in its
own words that it exists because "gather_rest's ABI row says it READS" it.

**A generator would break design (a), and cannot reach it.** `make_generator`
keeps `saved->argc` while truncating `saved->window` to `frame_size` — the
opcode row calls this a REAL MISCOMPILE already in the VM. Under design (a) a
resumed compiled generator would read `registers_[argv_base + i]` against a
frame restored somewhere else entirely. It is unreachable: both `op::call` and
`context::call` test `target.is_generator` and build a generator object BEFORE
they ask `enter_compiled`, so a generator proto's `aot_entry` is never called.
Worth an assertion rather than a comment, because the guard is in two places
and neither mentions AOT.

**`registers_` must not shrink below `argv_base` while the compiled frame is
live.** Argued above from an enumeration of every `registers_.resize` in
`lib/Script`. It is an invariant of the stack discipline rather than something
any one line enforces, and it is the assumption design (a) trades a copy for.

## What could not be verified

**Every DELEGATES TO citation in both rows is rotted, and the citation test
cannot see it.** `check-def-citations.cmake` only catches a line past
end-of-file; all four of these land inside a file that has since grown.

* `ct_aot_make_arguments` cites `run_loop.cpp:1083-1100` — that is inside
  `VM_CASE(load_home)`. The handler is `VM_CASE(make_arguments)`.
* `ct_aot_gather_rest` cites `run_loop.cpp:774-798` — that is the tail of
  `VM_CASE(load_new_target)`'s comment and the HEAD OF `VM_CASE(make_arguments)`,
  so `gather_rest`'s row currently points at `make_arguments`' handler. The
  handler is `VM_CASE(gather_rest)`, roughly 270 lines earlier.
* `ct_aot_make_arguments` cites `context::make_array (vm.hpp:238)`. That line is
  `make_object`. `make_array` is the next one.
* `bytecode_opcodes.def` compounds it: its `make_arguments` row cites
  `run_loop.cpp:1197-1199` (now `VM_CASE(pop_handler)`) and `vm.hpp:883` for
  `arguments_object` (now a comment about static accessors), and a nearby row
  cites `run_loop.cpp:1190-1202` and `881-905` for these two handlers and
  `objects.cpp:279-285` for a third — the first is inside `op::push_handler`'s
  construction, the second inside `op::await_value`'s settled-promise read, the
  third inside `context::instance_of`. **Six citations, all landing on unrelated
  code, all silently passing.** Repair them by name in the same commit; that is
  what the previous three batches did.

**`call_frame::arguments_object` is not "GC root 7".** `GCRoots.def` lists
`frame_arguments` eighth today. `closures.md` has the same problem in the other
direction — it calls `call_frame::closure` "GC root 4" and `frame_closure` is
now sixth. The numbering shifted when `pending_closure` was inserted, and
nothing checks it.

**`aot_contract.cpp` says "sixty-eight `extern "C"` prototypes" and
`aot_helpers.def` has 69 `CT_AOT_HELPER` rows.** The count in the prose is
stale; the count in the code is derived and correct. Adding two rows makes the
prose wrong by three.

**Nothing named `ct_aot_arg_array` exists**, which is expected — the MERGE
REFUSED paragraph describes a proposal that was rejected, not a symbol. Recorded
because `context::callee_type_error` was cited by two rows and had never existed
anywhere, and the way that was found was somebody looking for it.

**Neither `ct_aot_make_arguments` nor `ct_aot_gather_rest` has a body anywhere**
— verified by searching `lib/`, `unittests/` and `ctcompile/`. Neither name
appears outside `aot_helpers.def` and two prose lists.

**Not verified: whether `declare_local("arguments")` always lands on the register
immediately after the parameters.** Arm 42's anchor assumes it does, which is
what `compile_function_body`'s own comment describes ("argument i in register i,
so taking a register ahead of them would shift every one") and what makes the
clobber happen at all. If it does not, arm 42's expected string changes but the
case still separates what it claims to. Run the fixture interpreted first and
take the anchor from the answer, rather than from this paragraph.

**Not verified: any of this against a build.** No build was run; this is a
reading. In particular the EmitC spelling of two zero-operand runtime calls, and
whether `ec::CallOpaqueOp` on `ct_aot_args` produces a `uint64_t *` EmitC value
the subsequent call accepts without a cast, are both plausible and unchecked.

**The working tree moved underneath this reading, twice.** `git status` was clean
at the start; partway through it carried `own_keys` and `delete_prop` landing
across `run_loop.cpp`, `vm.hpp`, `aot_bridge.cpp`, `objects.cpp`, `CTJSOps.td`,
`BytecodeImport.cpp`, `CTJSToEmitC.cpp`, `Differential.cpp`, `differential.js`,
`ImporterCoverage.cpp` and `test/CMakeLists.txt`, and by the end it carried a
different set with `aot_helpers.def` in it — `load_bigint`, which is the third
of Phase 13's remaining three, and which is also **repairing rotted DELEGATES TO
citations by name in the same file**. The two rows quoted above were re-read at
the end and are unchanged, as are both VM handlers, byte for byte. But the six
rotted citations listed here may have been repaired by that commit rather than
by this work — check before repairing them again, and re-check the `not_yet`
list, the `body_is_supported` allow-list (which already carries `DeleteNamedOp`
and `OwnKeysOp`) and the `-DENTRIES=` list before editing any of the three.
