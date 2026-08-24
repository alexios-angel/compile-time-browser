# `new` in compiled code — **DONE**

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
