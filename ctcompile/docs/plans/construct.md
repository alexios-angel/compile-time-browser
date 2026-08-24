# Closing the corpus: `new`, the missing jumps, and `for-of` — **DONE**

`ctjs.construct` was the last operation refused anywhere in the Bootstrap
corpus. With it lowered, **every function the importer produces from
`bootstrap.bundle.js` compiles**:

```
imported  483
refused     0        (was 27, all of them ctjs.construct)
```

and the whole module translates and compiles — 320k lines of C++ through
`mlir-translate --mlir-to-cpp`, clean under `-std=c++23` against the real
`aot.hpp`.

## The shape

`ct_aot_construct(fr, callee, argv, argc, site, out)` reuses `ctjs.call`'s
machinery exactly: a contiguous argument window reserved in frame slots, parked
just before the call. It is a safepoint that runs arbitrary user JavaScript —
field initialisers and then the constructor body — before it is done reading
them, which is the same reason a call needs it.

Two things are NOT like a call:

* **`site` is the entry's own `function_proto`**, where `ct_aot_call` ignores
  the field entirely. It is read on exactly one path: naming the enclosing
  function in the TypeError a `new` on a non-constructor throws. That made
  `compiled_entry` carry two site values — `memo_site`, the per-function marker
  `ct_aot_new_string` memoises under, and `entry_site`, the real proto. Passing
  the first where the second belongs reads a `function_proto` that does not
  exist.
* **new.target is not passed**, because the ABI has nowhere to put it.
  `context::construct_new` sets it from the callee, which is what
  `op::construct` does with `fresh.new_target = callee`.

The runtime half is `context::construct_new`, `op::construct`'s dispatch
factored out so both tiers share it, plus `context::new_callee_type_error` — the
`new` half of a general `callee_type_error` that two ABI rows cite and **nothing
has ever defined**.

## What it cost

* **The plan's guard was wrong and the mutant said so.** It proposed
  `program_ == nullptr && proto->aot_entry == nullptr`; `invoke` bails on
  `program_ == nullptr` *before* it reaches `enter_compiled`, so testing both
  would have let a compiled body with no program return a bare instance whose
  constructor never ran. It guards on `program_` alone.

* **The primitive-return rule is written in THREE places and exactly one runs
  per construct** — `VM_CASE(ret)`, `ct_aot_construct_result` inside a compiled
  constructor, and `construct_new` on what `invoke` returns. The first
  differential case patched the caller *and* the constructor, so the compiled
  body had already applied it and `construct_new`'s copy could be deleted with
  every case still green. There are two cases now: one leaves the constructor
  interpreted, which is the only configuration that makes that copy
  load-bearing.

  Underneath it: `context::invoke` does not set `call_frame::constructing` at
  all. Its aggregate initialiser fills eight members and `constructing` is the
  twelfth, so `VM_CASE(ret)`'s rule never fires for a constructor entered that
  way. Redundant rather than broken — but only because `construct_new` covers
  it, which is what the mutant established.

* **`gc-roots.js` and `GCRoots.cpp` were two DIFFERENT PROGRAMS**, not a
  transcription that had drifted: the compiled bodies in one file, the drivers
  in the other, with a comment asking that they stay identical. They could not
  be. `held` builds a closure and a compiled body bakes that closure's function
  index, so they agreed only because the extra functions happened to sit after
  `keep`. This is the defect `differential.js` already had, in a worse form, and
  it has the same fix: one file, `#include`d by the driver.

* **A mutant that does not build is not a result.** The first falsification
  swapped `ct_aot_construct` for `ct_aot_call`, whose row takes eight arguments
  rather than six. The compile failed, the STALE binary ran, and it printed
  "all 23 bodies agree" — a build that never happened, reported as a pass. The
  harness refuses a failed build now, and unbuffers, because a mutant that
  CRASHES otherwise loses every line `printf` had buffered and looks like a run
  that printed nothing.

## How it is proved

| falsification | what went red |
|---|---|
| the compiled tier CALLS where it should CONSTRUCT | `construct` (NaN) and `new on a number` (not thrown) |
| `entry_site` → `memo_site` | SIGSEGV, at `new on a number`, with `construct` still green |
| the primitive-return rule dropped from `construct_new` | `construct` (NaN); `construct compiled` stays green |
| construct's arguments never parked | the GC arm: `undefinedZ` where 65 characters are correct |

Each mutation reddens exactly the cases that claim to cover it and no others.


---

# Two more, after `new`

`ctjs.construct` closed the LOWERING. What was left was upstream, in the
importer, which skips a function at the first opcode it has no operation for.

```
                                imported   skipped   refused by the lowering
after `new`                          483        91         0
+ the two jumps                      491        83         0
+ `iterable`                         519        55         0
+ `in`, `instanceof`, `delete`       527        47         0
```

## The two conditional jumps the CFG classifier had never heard of

`is_conditional_jump` named `jump_if_false` and `jump_if_true`. The VM has four.
`jump_if_defined` and `jump_if_not_nullish` were **not unimplemented** — their
emission was already written, correct, and unreachable, because that predicate
is what marks a branch target as a block LEADER. Neither one's target nor
fallthrough ever got a block, and the emitter then refused with "branch target
is not a block leader": a message that reads like a malformed program rather
than a classifier that has not heard of the opcode.

Twelve functions, every one of them `??` or `?.`. The differential case passes
arguments where truthiness and definedness disagree — `0 ?? 9` is 0 — because
lowering either jump through `ctjs.truthy` is the plausible mistake and the one
optional chaining exists to avoid.

## `iterable`, at 36 functions the largest single opcode

One opcode, one line in the interpreter, and `context::iterable_values` was
already a named member — and `ct_aot_iterable_values` **already had a body**.
Only the dialect operation, the importer case and the lowering were missing.

It is not the iterator protocol: there is no `Symbol.iterator` dispatch, the
helper answers with a plain array and the loop that follows indexes it, which is
why a for-of needs no other new operation.

The row asks for a correction the implementation deliberately does not make. Its
array-like arm calls `lookup_property` up to 2^24 times with no `failed_` test,
so a throw part-way through still runs millions of lookups. That is the
INTERPRETER's defect; re-testing in the helper alone would make the compiled
tier fail earlier than the interpreted one on a program that can observe it.
Fixing it is a VM change with its own before/after test, in one place.

## What these two cost

* **A mutant that passed was the FIXTURE's fault, not the code's.** Reading
  `op::iterable`'s source from operand `c` instead of `b` changed nothing,
  because `c` is 0, 0 names r0, and r0 held the iterable — it was the function's
  first parameter. The three bodies take a leading `pad` now, so r0 is a number
  and iterating it yields nothing. Same mutation, red.

* **An index-based edit put `ConstructOp` in the wrong allow-list.** It admitted
  the operation, so everything worked and every test passed; it sat with the
  globals rather than beside `CallOp`, where a reader would look. Asserting a
  match count catches a patch that changes nothing — it does not catch one that
  changes the wrong thing.

* **`ct_aot_iterable_values` was implemented twice** for a few minutes, because
  the row was on the "no body yet" list and the list was stale. The build said
  so immediately. Worth remembering that the 24-rows-without-bodies count is
  itself a measurement that rots.


## `in`, `instanceof` and `delete` — 13 functions, and three shared members

All three already had a dialect operation and no body anywhere. Each was inline
in `run_loop`, so each was lifted into a `context` member first — the same move
`make_closure`, `construct_new` and `iterable_values` had already made, and for
the same reason: a compiled `key in obj` and an interpreted one must not be able
to disagree.

Their ABI tiers differ and the signatures say so. `has_property` and
`delete_index` answer an `int32_t` status, so a caller tests it. `instance_of`
returns its **boolean** — it is raise tier, so on failure the `uint32_t` is
meaningless and a caller polls `ct_aot_failed` at a back edge instead.

`ctjs.instanceof` answers an `i1` on purpose, which its own description explains:
`value::from_bits(1)` is a subnormal double, not `value::boolean(true)`. Boxing
is the front end's job, so this added **`ctjs.from_bool`** — the other half of
`ctjs.truthy`, Pure because a boolean is an immediate. Keeping the raw predicate
is what will let `if (x instanceof C)` branch without boxing at all.

Three falsifications, deliberately of three different kinds:

| falsification | caught by |
|---|---|
| `in` on an array stops requiring the WHOLE key, so `"1x"` is index 1 | the ANCHOR — shared code, so both tiers agreed |
| `instanceof`'s object-like guard dropped, so `5 instanceof Number` is true | the ANCHOR, same reason |
| the lowering's object and key swapped | the COMPARISON — compiled tier only |

The first two are the case the differential premise cannot see on its own, and
they are why those cases carry expected answers at all.

**And a negative test had to be repointed, for the second time.** `refusals.mlir`
used `ctjs.has_property` as its example of an operation with no lowering; before
that it used a property write. It names `ctjs.create_regexp` now, which is not a
`CTJS_RuntimeOp` at all — `aot_helpers.def` declares no helper for a regexp
literal, so there is nothing to call even if a conversion were written. A
negative test has to keep naming something genuinely unsupported or it asserts
nothing.

**Four ABI citations rotted the moment `run_loop.cpp` got shorter.** Lifting
three opcode bodies out of it moved the end of the file above four cited lines,
and `ctcompile_def_citations` failed — which is the only citation defect it can
see, and it saw this one. All four are cited by `VM_CASE(name)` now.
