# Closures in compiled code

Compiled bodies cannot see captured variables today, and the reason is
structural rather than a missing helper: **the entry ABI never delivers the
closure.** `ct_aot_entry_fn` is
`(ctx, site, argv, argc, receiver, constructing, out)`, where `site` is the
`function_proto`. Upvalues live on `closure_object::upvalues`, which is an
instance, not a proto — two closures over the same function have different
upvalues and the same `site`.

So every helper that would read them is blocked at the same place:
`ct_aot_upvalue_cell(fr, index)`, `ct_aot_callee(fr)` and `ct_aot_home(fr)` all
read `call_frame::closure`, and `ct_aot_enter` sets `proto`, `base`,
`handler_base`, `receiver` and `new_target` — never `closure`.

This is also why the importer refuses every top-level function: the `closure`
opcode declaring a nested function has no CTJS operation, so a file whose
top level declares anything is skipped whole.

## What is already in place

* **`call_frame::closure` is GC root 4** (`GCRoots.def:31`), so once a compiled
  frame has one the collector traces it with no further work.
* **`pending_new_target_` is the pattern to copy** — GC root 3, set by a caller,
  consumed and cleared by `ct_aot_enter` (`aot_bridge.cpp:176-184`) and by
  `context::call` (`call.cpp:133-134`). Its comment already documents the exact
  ordering hazard: push the frame *before* clearing the root, or the value is
  reachable from nothing in the gap.
* **Every caller of `enter_compiled` has the closure in scope.** `context::call`
  computes `fnobj` at `call.cpp:72` and calls `enter_compiled` at `:108` without
  it. It is an internal C++ signature, not the ABI.
* **`function_proto::aot_entry` already exists**, so a nested function's proto
  can carry a compiled entry. The row's warning that "AOT stops at the first
  nested function" predates that field and should be re-read, not trusted.

## Stage 1 — the closure reaches the compiled frame — **DONE**

The only stage that unblocks anything; the rest are ordinary helper work.

* Add `context::pending_closure_`, a `value`, beside `pending_new_target_`, and
  add it to `GCRoots.def` — it is reachable from nothing else in the window
  between being set and being consumed.
* `enter_compiled_body` gains a `closure` parameter and sets
  `ctx.pending_closure_` before calling `target.aot_entry`. Its five call sites
  (`call.cpp:112`, `:239`, `:324`, `run_loop.cpp:907`, `:1390`) pass what they
  already hold; the two in `call.cpp:239`/`:324` are entry points with no
  closure and pass `undefined`.
* `ct_aot_enter` consumes it into `entered.closure`, **pushing the frame before
  clearing the root**, exactly as it does for `new_target`.

Verified by: a differential case whose compiled body reads a captured variable.
It answers `undefined` today and the captured value afterwards, so the case
fails before the change and passes after — which is the whole point of writing
it first.

**The `undefined` closure is not the same as a null one.** `pending_closure_` is
a `value`; `call_frame::closure` is a `closure_object *`. The consumption must
map a non-closure value to `nullptr` rather than casting, because
`ct_aot_upvalue_cell` and `ct_aot_callee` both distinguish "no closure" from "a
closure with no upvalues".

## Stage 2 — reading upvalues — **DONE**

Three helpers, all mechanical once Stage 1 lands, all currently bodyless:

| helper | is | from |
|---|---|---|
| `ct_aot_upvalue_cell(fr, index)` | the guarded fetch | `VM_CASE(get_upvalue)` |
| `ct_aot_cell_get(cell)` | `is_kind(cell) ? slot : undefined` | `VM_CASE(cell_get)` |
| `ct_aot_cell_set(cell, v)` | the same guard, writing | `VM_CASE(cell_set)` |

**The composition reproduces the fused opcodes exactly**, which is the reason
the ABI splits them: `cell_get(upvalue_cell(fr, i))` is `get_upvalue`, because
`upvalue_cell` answers `undefined` for a missing closure or an out-of-range
index and `cell_get` no-ops on a non-cell. The two guards compose to the one
guard the interpreter writes inline.

In the dialect, `ctjs.load_upvalue` and `ctjs.store_upvalue` were demoted to
plain `CTJS_Op`s precisely because each is **two** calls, so the backend lowers
them by hand rather than through the one-helper interface.

## What stages 1 and 2 cost, in the end

Both landed as planned and the plan's two named findings held. Two things it
did not predict:

* **The callee had to be delivered before the `IRMapping` was built**, not
  after. Mapping a block argument to a still-null `Value` is accepted silently
  and the crash arrives later inside `Operation::create`, with a stack naming
  neither.
* **The two-closure case passed with the handoff removed**, because the
  differential harness installs one entry at a time: driving `counters` while
  patching `counters` runs `step` interpreted and exercises no closure at all. A
  subject now names the proto it patches separately from the arm it drives. The
  case was written to separate the instance from the proto and was separating
  nothing; the mutant is what found it.

## Stage 3 — creating closures — **DONE**

The largest stage, and the only one with a design question in it.

`ct_aot_make_closure(fr, enclosing_closure, function_index, local_upvalues,
upvalue_count, enclosing_this)` delegates to a **new `context::make_closure`**
factored out of `run_loop.cpp:884-920` verbatim, so the interpreter and the
compiled path share one copy. What that code does, and each part matters:

* selects the program as `enclosing_closure->owner` else `*program_`;
* walks `target.upvalues`, taking `from_parent_local` entries from **this
  frame's registers** and the rest from the enclosing closure's upvalues,
  substituting `undefined` when out of range;
* decides an arrow's `this` **where it is written**, from the enclosing frame's
  `effective_this`.

`local_upvalues` is **indexed in parallel with `target.upvalues`**, not packed:
only the `from_parent_local` slots are read and the helper fills the rest. A
packed array is the plausible wrong reading and would silently mis-capture.

The factored member must **guard three things the interpreter dereferences
unguarded**, because a compiled caller can reach it in configurations the
interpreter cannot: `program_ == nullptr` with no enclosing closure,
`function_index >= prog.functions.size()`, and
`upvalue_count != target.upvalues.size()`. All three raise; the row is
raise-tier only, so a caller polls rather than testing a status.

**`ctjs.create_closure` is wrong as declared and must change.** Its operand is
`CTJS_ProgramType:$program`, and nothing produces a `!ctjs.program` — which is
why the importer cannot emit it and skips the opcode. The ABI wants the
**enclosing closure as a value**, plus the enclosing `this`. The op should be
`(ins CTJS_ValueType:$enclosing_closure, I32Attr:$function,
Variadic<CTJS_ValueType>:$upvalues, CTJS_ValueType:$enclosing_this)`.

The ABI shape trait does not catch this, and deliberately: it has no
operand-count rule, because the dialect is allowed to be higher-level than the
ABI. This is the cost of that decision, paid once.

The importer then emits `ctjs.create_closure` for `op::closure`, taking the
enclosing closure from `ct_aot_callee(fr)` — which Stage 1 makes answerable.

## What stage 3 cost

Both of the plan's named findings held — the op was unproducible and the
upvalue array is parallel — and three more turned up, **all found by mutants
that passed**:

* **The fixture was duplicated** in a `.js` and a C++ string, with a comment
  saying they must stay identical. They drifted in *order*, and a compiled body
  bakes the function index of every closure it builds — so a reordered fixture
  makes it build a closure over a different function. It presented as a case
  reporting 3 where 302 was right, and passing. There is one file now.
* **`drive` left `OUT` stale** when an arm threw, so both tiers read the
  previous answer and agreed. It sets a sentinel the driver rejects.
* **The differential premise has a bound.** Where the two tiers share an
  implementation, a bug in it breaks both and they agree —
  `context::make_closure` is precisely that, factored out so the tiers cannot
  drift, at the price of the comparison going blind to it. Swapping its two
  descriptor arms made every closure case answer `undefined` and still agree.
  Those cases carry an anchor now.

And one that was not a test problem: `llvm_unreachable` is
`__builtin_unreachable()` under `-DNDEBUG`, so the "no default arm" net in
`convert()` was silent in release — an operation added to the allow-list and not
to the switch survived into the output with its operands rewritten.

Two things also changed shape. `arg0` is the **effective** receiver via
`ct_aot_this`, not the entry's raw one — the importer maps `op::load_this` to
it, and that is `effective_this`, which differs for every compiled arrow. And
`--ctjs-drop-uncompiled` removes refused functions so one refusal costs that
function rather than the whole translation unit.

## Stage 4 — nested compiled functions — **WORKS, measured**

The ABI row warns that "AOT stops at the first nested function unless
closure_object or function_proto carries a compiled entry point". That row
predates `function_proto::aot_entry`, which exists — and the warning is stale.
Measured by installing both entries at once, which the differential harness
never does because it patches one proto at a time:

```
both interpreted           12
creator compiled           12
closure compiled           12
BOTH compiled (stage 4)    12
```

**This is not yet a permanent test.** It needs a subject that patches more than
one proto, and until it has one, nothing in the suite covers a compiled body
calling a closure another compiled body built.

## DONE — all four stages, and what closed the last two gaps

Both gaps named above are closed, and the arrow one was not a formality.

**`ct_aot_this` returned `call_frame::receiver` directly** while its ABI row has
always said it delegates to `context::effective_this`. For every ordinary
function the two are the same value, so nothing could tell them apart — an arrow
is the only shape that separates them, and until compiled code could build one
there was no way to write the test. `() => this` inside a method read the arrow
frame's own receiver, undefined for a plain call, instead of the method's
object.

**A collection across `ct_aot_make_closure`** is covered: three things have to
survive it and the call after it — the string, the cell holding it, and the
closure_object — and none is reachable from anything but the frame's slots.
Falsified: dropping the upvalue window's rooting gives `undefinedZ` where 65
characters are correct.

**The harness now installs entries on several protos**, which is what made both
possible: `arrow this` needs the method and the arrow compiled, and
`nested compiled` needs the creator and the created. The lookup takes a name
**and an ordinal**, because an arrow has no name — the importer calls every
anonymous function `fn` when it builds a symbol while the proto's own name is
`""`, and this fixture has two anonymous protos.

One method note. The probe written to isolate the arrow failure was **wrong**:
it compiled a different program from the one the entries were built from, so the
function indices a compiled body bakes pointed at other functions entirely and
all four arms agreed for no good reason. The real isolation was inside the
fixture, patching one half at a time.

## Stage 4 — original note

Out of scope for the first working version, and worth stating so it is not
discovered as a surprise: a closure built by Stage 3 points at a
`function_proto`, and whether calling it enters compiled code depends on that
proto's `aot_entry`. Since the compiler compiles every function in a program,
the entries exist — so this should work without further design. **It has not
been tested and the ABI row claims otherwise**; the row predates
`function_proto::aot_entry` and the claim needs re-reading before it is either
relied on or repeated.

## How each stage is proved

Every stage gets a **differential case** — the compiled body against the
interpreter, with the interpreter defining the answer — and each case is chosen
so a plausible mistake changes the result:

* a counter closure (`function mk(){var n=0; return function(){return ++n;};}`)
  separates a captured cell from a copied value: a copy answers 1 every time;
* two closures over the same function separate the instance from the proto:
  sharing upvalues makes the second answer continue the first's count;
* a capture through **two** levels of nesting separates the
  `from_parent_local` arm from the enclosing-closure arm;
* an arrow inside a method separates `this` decided where it is written from
  `this` decided where it is called.

The last one is worth writing even though it looks like a language test: it is
the only case that distinguishes the `is_arrow` branch, and that branch is the
one this ABI's row calls out specifically.

**And a GC case**, because `ct_aot_make_closure` allocates and is a safepoint:
a closure built and then held only in a compiled frame's slot, across a
collection.
