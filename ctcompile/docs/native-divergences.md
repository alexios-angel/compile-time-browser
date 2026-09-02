# The native backend's declared divergences

Where the native C++ backend and the thing it is compared against cannot agree,
the disagreement is **declared here, tested, and short** — never discovered.

> A backend with an undocumented divergence list is a backend nobody can trust;
> a backend with a short, tested one is a tool.
> — the master plan, part 24 §1.3

## The rules of this file

1. **Append-only.** An entry is removed only when the divergence itself is
   gone, and the commit that removes it says how.
2. **Two sign-offs per entry**, which part 24 §A.2 spells out: the divergence,
   and the test that pins it. An entry with no test is a note, not a
   divergence.
3. **A test pins against the RUNNING INTERPRETER, not against a number.** The
   ctbrowser VM is correct by definition for this project, so an entry that
   wrote its expected value down here would go stale silently the day the
   engine changed. Every pin below asks the VM.
4. **Two things get listed and they are not the same.** A DIVERGENCE is a
   difference that survives. An OBLIGATION is a thing a later phase must
   refuse, and it appears under the entry that creates it.

## What each entry is measured against

There are two different standards, and confusing them is how a divergence list
becomes useless:

| against | who wins | what a difference means |
|---|---|---|
| the **ctbrowser VM** | the VM, always | a **defect** in the native backend |
| **ECMA-262 / V8** | nobody, here | a divergence the *engine* has, which the backend inherits |

---

## ND-1 — `String.prototype.length` counts UTF-8 bytes, not UTF-16 code units

**Status:** declared. **Against:** ECMA-262. **Inherited from the engine.**
**Introduced:** Phase 53 (the `ctnative` type system).

### The divergence

ECMA-262 defines a String value as a sequence of **UTF-16 code units**, so
`.length`, indexing, `charCodeAt` and `split("")` all count code units, and
lone surrogates are legal members. `"\u{1F600}".length` is **2**.

ctbrowser stores a JavaScript string as **UTF-8 bytes**. The same expression is
**4** here. The engine already knows and already records it:

* `ctbrowser/docs/script.md`, "The UTF-16 gap" — *"A JS string is a sequence of
  UTF-16 code units; this engine stores UTF-8 bytes."*
* `ctbrowser/unittests/js/string_basics.cpp` pins the current answers in a
  labelled block with V8's answer beside each, and calls that block *"the
  acceptance list for a UTF-16 migration"*.

### What the native backend does about it

`!ctnative.str` carries an **encoding parameter**, `utf8` or `utf16`, so the
representation is a stated property of the type rather than an assumption. The
choice for an unproved string lives in exactly one place:

    kDefaultStringEncoding   —   ctcompile/include/ctcompile/CTNative/IR/CTNativeLattice.h

and it is **`utf8`**, because that is what reproduces the interpreter's
observable answers. The native backend therefore **inherits the engine's
divergence from ECMA-262 and introduces none of its own**, which is the only
position compatible with a differential harness whose oracle is that
interpreter.

### Where the plan is wrong, and why it matters

Part 24's Stage 53D says to *"Default to `utf16` (a `std::u16string`), which is
semantics-preserving and boring."* That is right against ECMA-262 and **wrong
here**. Defaulting to UTF-16 would make every native-compiled `.length` on a
non-ASCII string disagree with the interpreter it is being compared against —
manufacturing a differential failure on purpose, and doing it in the one place
part 24 §1.3 says the two "must agree exactly".

### The test

`ctcompile/test/CTNativeLattice.cpp`, the ND-1 block. It compiles and runs

```js
var S = "😀";
var L = S.length;
```

in the real VM, reads back the string's bytes and the engine's own answer, and
asserts:

* the code-unit count under `kDefaultStringEncoding` **equals** what the
  interpreter answered — flipping the constant to `utf16` turns this red;
* the UTF-16 count is 2 and the engine's answer is 4, so the divergence is real
  and not an assumption;
* `kDefaultStringEncoding` is `utf8`.

Registered as `ctcompile_ctnative_lattice`.

### The migration path

When the engine closes its UTF-16 gap, `string_basics.cpp`'s pinned block fails
and hands over the acceptance list. On this side the whole change is
`kDefaultStringEncoding`, and the ND-1 test fails until it is made — by asking
the interpreter, not by being edited.

### Obligation O-1 (Phase 54) — a widening encoding changes an observable answer

`meet(str<utf8>, str<utf16>)` is `str<utf16>`, because UTF-16 is the wider
*representation*: it holds lone surrogates and UTF-8 cannot encode them at all.
That is sound as a statement about which **values** are representable.

It is **not** sound as a statement about what `.length` answers, because the
two representations answer differently. The lattice cannot prevent this and
does not pretend to. **Phase 54 must refuse the widening**: on a path where
`.length`, an index, `charCodeAt` or `slice` is observed, a string whose
encoding would widen goes to `!ctnative.boxed` instead, which is a refusal and
therefore correct.

This obligation has no test yet because the phase that must honour it does not
exist. It is listed so that phase cannot be written without meeting it.

---

## ND-2 — no weak references exist in this engine

**Status:** declared. **Against:** ECMA-262. **Inherited from the engine.**
**Introduced:** Phase 55 (escape analysis), Stage 55C.

### The divergence

ECMA-262 has `WeakRef`, `FinalizationRegistry`, and the weakly-keyed
`WeakMap`/`WeakSet`. This engine has none of them as such:

* there is no `WeakRef` global and no `FinalizationRegistry` anywhere under
  `ctbrowser/lib/Script` — `typeof WeakRef` is `"undefined"`;
* `WeakMap` and `WeakSet` **are** `Map` and `Set` — the same heap objects under
  a second name (`ctbrowser/lib/Script/builtins/collections.cpp`, the two
  `define_global` lines at the end of `install_collections`), so
  `WeakMap === Map` is `true` and an entry keeps its key alive. The engine
  calls this out itself: *"the difference is a leak, not a wrong answer."*

### What the native backend does about it

Part 24 Stage 55C offers three answers to a cycle and the third — *"weak links
only where the SOURCE declares them"* — is **vacuous here**: nothing in a
program running on this engine can say `WeakRef`, and a `WeakMap` in the source
is a strong `Map`. So `!ctnative.weak<T>` (`CTNativeTypes.td`, `CTNative_WeakType`)
has **no producer**: the escape analysis has no state that maps to it, and the
type's own description forbids the compiler introducing it to break a cycle.

The reason that prohibition is not merely stylistic is pinned in the same test,
in C++: choosing which edge of a cycle becomes weak is not semantics-preserving.
*"A tracing collector keeps a cycle alive as long as any member is reachable
from a root, and `weak_ptr` does not. The weak side can be destroyed while
JavaScript would still see it."*

**Beside it — the native tier has no collector.** Ownership there is RAII: a
value is destroyed when its owner is, and nothing traces. So the tier has no
"fall back to the collector" for an object graph it cannot give an RAII
lifetime. A cycle the analysis cannot prove **confined** — created, cycled and
dropped inside one frame, with no escape (55C option 2) — is a **diagnosed
refusal**: a compile-time diagnostic naming the allocation sites, never a
silently chosen weak edge and never a leaking `shared_ptr` ring. The one
RAII-sound lowering of a confined cycle is a frame-owned region whose members
die together at scope exit; the test models it and counts every destructor
exactly once.

### The test

`ctcompile/test/EscapeCycle.cpp` over `ctcompile/test/escape-cycle.js`,
registered as `ctcompile_escape_cycle`. It asks the VM, in a fresh context:

```js
typeof WeakRef === "undefined" && WeakMap === Map && WeakSet === Set
```

and asserts `true` — with each conjunct on its own line so a failure is named
— plus `new WeakMap()` being an `instanceof Map` whose entry reads back. Around
that pin, the same executable:

* runs the fixture's ring under `set_gc_stress(true)` with `collections()`
  asserted to have grown, and asserts `walk(keep, 9).v` and
  `keep.prev.next === keep` answer what the unstressed VM answered — the
  reachable cycle survives a tracing collection (gate 4(a));
* pins `live_objects()`: a reachable 3-ring is exactly baseline + 3 (property
  names are constant-pool strings, numbers and null are immediates), a ring
  reachable only through a middle member keeps all three, an unrooted ring is
  freed whole, and `keep = null; collect()` returns to baseline (gate 4(b));
* pins what "under stress" means in this interpreter, which the design's
  sketch got wrong: the only safepoint is `context::invoke`'s entry
  (`vm/call.cpp`) - every C++ entry into JavaScript - and an interpreted
  JS-to-JS call is not one. So the fixture's `churn(1000)` collects exactly
  once, returns with 4,000 dead nodes, and one collection frees them all; the
  driver's `churnVia`, which enters `ringLocal` through
  `Function.prototype.call` (a C++ re-entry, hence a safepoint - the boxed AOT
  tier's cadence, since `ct_aot_call` is one), collects 1,001 times and is
  bounded by exactly one dead ring;
* models the two failure modes of the weak-link proposal with destructor
  counters: a `std::shared_ptr` ring with every handle dropped destroys **0**
  nodes, and a `std::weak_ptr` back-edge destroys the head while the node
  JavaScript still holds is alive — `prev.expired()` — where the VM keeps it
  (gate 4(c)); and the frame-owned region, all N destroyed exactly once at
  scope exit, none before, summing to the VM's `ringLocal(4)`;
* when `ctjs-translate` is built, reads the fixture's printed ctjs module as
  text and asserts it contains the four functions, that the importer skipped
  none of them (`ctjs.skipped` absent), and that there is no `ctnative.weak`
  (gate 4(f)).

### The migration path

Should the engine grow a real `WeakRef` or weakly-keyed collections, the
`typeof WeakRef` and `WeakMap === Map` lines fail and hand over the acceptance
list. On this side, 55C option 3 stops being vacuous: `weak<T>` gains a
producer — a `WeakRef` or `WeakMap` in the SOURCE, still never a compiler
choice — and the entry above is amended by the commit that adds it.

---

## ND-3 — a getter installed on `Object.prototype` does not fire for a plain object

**Status:** declared. **Against:** ECMA-262 / V8. **Inherited from the engine.**
**Introduced:** Phase 55 (found while pinning ND-2; it bears on the
"getter/setter on the path" refusal of part 24 Stage 55A).

### The divergence

In V8, `Object.defineProperty(Object.prototype, "x", {get() { return 42 }})`
followed by `({}).x` answers **42**: an object literal's `[[Prototype]]` is
`Object.prototype`, and property lookup calls the accessor it finds there.

Here `({}).x` is **undefined**. `context::lookup_property`
(`ctbrowser/lib/Script/vm/objects.cpp`) walks the object's chain calling any
accessor it finds — but a fresh literal's `prototype` field is `null`, so the
walk is one level long, and the shared `Object.prototype` table is consulted
**afterwards** with `find`, which sees data properties only. The same getter
DOES fire when that table is on an explicit chain: `Object.create(Object.prototype).x`
is 42. A data property on `Object.prototype` is seen by a literal either way.

### What the native backend does about it

Nothing the backend emits changes this — it is the interpreter's lookup, and
the boxed tier calls the same one. It is recorded because Stage 55A's escape
table treats a constant-key `get_property` on a confined object as NEITHER
sink nor carry on the cited grounds that no user code runs there, and one of
those grounds is this shortcut: in this engine a getter on `Object.prototype`
cannot reach a plain literal. **Obligation (Phase 55/56):** any lowering that
relies on that must cite this entry, so the day the engine walks the shared
table for accessors, the row is re-examined rather than silently wrong.

### The test

`ctcompile/test/EscapeCycle.cpp`, the ND-3 block, in a fresh context each:

* the literal read after the getter is installed is `typeof … === "undefined"`
  (V8: 42);
* `Object.create(Object.prototype).nd3` after the same install is `42` — the
  shortcut, not accessors, is what diverges;
* `Object.prototype.nd3d = 7; ({}).nd3d` is `7` — data properties on the shared
  table are seen.

Registered as `ctcompile_escape_cycle`.

---

## Phase 55 — obligations of the ownership lowering (Stage 55B)

Listed here, under the entry that creates them, as 25-escape-analysis.md §4.2
states them. None has a test yet, because the phase that must honour each does
not exist; each is listed so that phase cannot be written without meeting it.

### Obligation O-2 (static contents)

A confined site may be lowered by value only when **every value ever stored
into it has a proved non-heap 54A type**. A boxed field inside a stack object
is a reference the precise collector cannot see (it walks exactly
`GCRoots.def`), so the object it points at is freed under it. Until Phase 47's
per-field RAII rooting exists, anything else stays boxed. `couldBeHeap(Type)`
mirrors `couldBeBigInt` (`TypeInference.cpp`): conservative on `boxed`,
`json`, containers and `null`.

### Obligation O-3 (identity)

`confined` is not `unaliased`: a site reachable through two SSA values (a block
argument, `iterable`'s carry) must lower to a reference to **one** stack slot,
never a copied C++ value; identity-observing operations (`===` on the object)
need Phase 56's reasoning. Recorded so Phase 56 cannot be written without
meeting it.

### Obligation O-4 (frame lifetime, not scope lifetime)

`let last; for (...) { const p = {}; last = p }` is confined to the **frame**,
not the loop body; a C++ local declared in the body would dangle. 55B/56/57
place a confined object at function scope unless a separate scope proof
(Phase 47's liveness) exists.

### And the refusal 55C adds to the list

**"Cycle not proved confined" is a diagnosed refusal on the native tier.** The
tier has no collector to fall back to (ND-2 above), so an object graph with a
proved cycle that the analysis cannot show confined to one frame is reported
at compile time with its allocation sites named — the shape `ctjs.not_lowered`
already has — and is not lowered. A confined cycle lowers to a frame-owned
region (55C option 2); `EscapeCycle.cpp`'s (c3) block is the model of that
region, and its (c1)/(c2) blocks are the two answers the tier must never give
silently.

---

*Next entry: ND-4.*
