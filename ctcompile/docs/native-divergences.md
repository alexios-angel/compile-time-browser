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


# Phase 63 Step 5 — the whole list, at a glance

Part 24 Phase 63 Step 5 asks for "the declared-divergence list, as a document
and as tests". ND-1 to ND-3 above are entries the *engine* has and the backend
inherits. Everything from ND-4 down is the tier's own: a place where the C++ a
naive lowering would emit answers differently from the interpreter, and the
tier therefore either **emits a guard** (the two sides agree, and the
differential gate proves it on a running binary) or **refuses** (no program can
witness it, so the refusal *text* is the only thing a test can hold).

The distinction is the whole design, so it is the first column:

| # | witness (JavaScript) | the interpreter answers | naive C++ answers | the tier |
|---|---|---|---|---|
| ND-4 | `1 ** (0/0)`, `1 ** (1/0)`, `(0-1) ** (1/0)` | `NaN` | `std::pow` gives `1` | **guard** |
| ND-5 | `(0 - 7.5) % 2` | `-1.5` | `std::fmod` gives `-1.5` | **emitted, exact** |
| ND-6 | `1 / (0 * (0 - 1))` | `-Infinity` | `-Infinity` | **emitted, exact** |
| ND-6b | a NaN's sign | not observable | `-nan` or `nan`, per path | **folded by the gate** |
| ND-7 | `o.later + 1`, `o.later < 1`, `if (o.later)` | `NaN`, `false`, falsy | NaN arithmetic, the same | **emitted, exact** |
| ND-7 | `o.later === 5`, `typeof o.later`, printing it | `false`, `"undefined"`, `undefined` | `NaN == 5` false, `"number"`, `nan` | **refused** |
| ND-8 | `[10,20,30][7]`, the same `[-1]`, `[NaN]`, `[Infinity]` | `undefined` | `v[i]` is undefined behaviour | **guard** (`ctnative::vec_at`) |
| ND-8 defect | `[10,20,30][0.5]` | `10` — the engine truncates | — | **wrong answer, OPEN** |
| ND-9 | `2147483648 \| 0` | `-2147483648` | `static_cast<int32_t>` is UB | **refused** |
| ND-10 | `({x:1}).constructor` | a function, truthy | an uninitialised field | **refused** |
| ND-11 | `({class:1}).class`, `({NAN:1}).NAN` | `1` | does not compile | **refused** |
| ND-12 | `a.length = 5`, `a[100] = 1`, `delete a[0]` | resizes, or goes sparse | `std::vector` cannot hold a hole | **refused** |
| ND-13 | `[true,false][1]` | `false` | `vector<bool>`'s proxy | **refused** |
| ND-14 | `Math.round(-0.5)`, `Math.min(1,NaN)`, ten more | see the twelve rows | see the twelve rows | **refused** (no builtin lowers yet) |

**Where each one is tested.**

| # | emitted witnesses | refusal text |
|---|---|---|
| ND-4 | `native-divergence-fixture.js` (`pow_*`), `native-pipeline-fixture.js` | — |
| ND-5 | `native-divergence-fixture.js` (`mod_*`) | — |
| ND-6 | `native-divergence-fixture.js` (`negzero*`, `poszero*`) | — |
| ND-7 | `native-divergence-fixture.js` (`u_*`, `b_*`); `native-struct-fixture.js` | `CTNative/Lowering/divergence-refusals.mlir` (EQUALITY, TYPEOF); `global-undefined.mlir` (HOISTED, PICK, OUTERSTORE, READBEFORE, ONEPATH — the printing row, and DOMINATES / FIELD for the narrowing that pays for it) |
| ND-8 | `native-divergence-fixture.js` (`idx_*`) | — |
| ND-8 defect | `native-index-truncation-fixture.js`, registered to FAIL | — |
| ND-9 | — | `divergence-refusals.mlir` (BITWISE) |
| ND-10 | `native-struct-fixture.js`, the shadowed case | `CTNative/Lowering/shape-field-names.mlir` (INHERITED) |
| ND-11 | — | `shape-field-names.mlir` (KEYWORD, MACRO) |
| ND-12 | `native-array-fixture.js` (`counted`) | `CTNative/Lowering/native-array.mlir` (LENGTH, INDEXED, DELETED) |
| ND-13 | — | `native-array.mlir` (BOOLEANS) |
| ND-14 | — | `ctcompile/test/StdLibMap.cpp`, registered `ctcompile_stdlib_map` |

`native-divergence-fixture.js` goes through the Phase 62½-D gate as
`ctcompile_native_unit_pipeline_divergence` (and its deduced twin), and
through Phase 63 Step 7's two-toolchain compile as
`ctcompile_compile_clean_pipeline_divergence`. Its negative proof is
`ctcompile_native_unit_pipeline_index_truncation` — a program registered to
**fail**, over the one guard that is measurably wrong (ND-8's defect below).

**Writing this list found two things the code did not do**, which is the
reason part 24 §A.2 asks for a test per entry rather than a paragraph per
entry: the fractional-index defect under ND-8, and the fact under it that the
differential gate's own `-DMUTATE` negative proof has never run against a
generated module.

---

## ND-4 — `**` is not `std::pow`

**Status:** declared, **guarded**. **Against:** the ctbrowser VM (and ECMA-262,
which agrees with the VM here). **Introduced:** Phase 62½-C, the operator
lowering. **Found:** the adversarial audit of 2026-09-02.

### The divergence

C99 says `pow(±1, y)` is `1` for **every** `y`, the infinities and NaN
included. ECMA-262 and `Number::exponentiate` say **NaN** for exactly those
cases. So:

```js
1 ** (0 / 0)        // NaN in JavaScript,  1 from std::pow
1 ** (1 / 0)        // NaN in JavaScript,  1 from std::pow
(0 - 1) ** (1 / 0)  // NaN in JavaScript,  1 from std::pow
```

This is not a corner of the language reachable only from a literal `NaN`.
**Undefined is this tier's NaN** (ND-7), so `1 ** undefined` — a plain typo in
ordinary source — computed `1` natively and `NaN` in the interpreter, on a
program nothing refused. `StdLibMap.td` had classified the *library* spelling
`Math.pow` as Divergent with this exact witness since Phase 61, and
`native-numeric.mlir` **pinned the bare `std::pow` call** — so the defect had a
test defending it.

### What the tier does about it

`LowerToEmitC.cpp`, `exponentiate()`. The whole difference is one guard, so it
is emitted rather than refused:

```c++
(std::fabs(base) == 1.0 && !std::isfinite(exponent)) ? NAN : std::pow(base, exponent)
```

Refusing would also have been consistent and would have cost `2 ** 31` — a
case the compiler can prove — to avoid one it can also prove. The guard is the
cheaper correct answer.

### The test

The five diverging cases and eight non-diverging ones are globals of
`native-divergence-fixture.js`, compared against the interpreter by
`check-native-unit.cmake`. The negative half matters as much as the positive:
a guard that answered NaN whenever the exponent was infinite would pass
`pow_one_inf` and fail `pow_two_inf`; one that fired on any base of magnitude
one would fail `pow_nan_zero`, which is `1` in both languages.
`native-numeric.mlir` pins the *shape* — `call_opaque "std::fabs"` and
`call_opaque "std::isfinite"` must appear — so the bare call cannot come back.

---

## ND-5 — `%` is `fmod`, and that is the exact answer, not an approximation

**Status:** declared, **exact**. **Against:** the ctbrowser VM.
**Introduced:** Phase 62½-C.

### The divergence that is not one

JavaScript's `%` is a *remainder*, not a modulo: the result takes the sign of
the **dividend**. That is C's `fmod` exactly, including at the three edges.
The row is here because the natural suspicion — "surely the sign convention
differs somewhere" — deserves a measurement rather than confidence, and
because a future rewrite of this operator has to keep it true.

```js
(0 - 7.5) % 2         // -1.5, and fmod(-7.5, 2) is -1.5
7.5 % (0 - 2)         //  1.5, and fmod(7.5, -2) is  1.5
5 % 0                 //  NaN, and fmod(5, 0) is NaN
(1 / 0) % 2           //  NaN, and fmod(inf, 2) is NaN
5 % (1 / 0)           //  5,   and fmod(5, inf) is 5
```

### The test

`native-divergence-fixture.js`, the `mod_*` globals: all four sign quadrants
and all three edges, through the differential gate.

---

## ND-6 — `-0` survives the comparison, and a NaN's sign does not exist

**Status:** declared, **exact** for the zero and **folded** for the NaN sign.
**Against:** the ctbrowser VM. **Introduced:** Phase 62½-D, the printing
convention.

### The two halves

**`-0` is a value and the convention preserves it.** The gate's output
convention is `printf("%.17g")` of the double, which prints `-0` for negative
zero and `0` for positive zero, so a sign flip on a zero is a *failing* line
and not an invisible one. `check-native-unit.cmake`'s probe requires the
reference to print `m=-0` before any comparison is believed. And the
distinction is observable as arithmetic, not only as spelling: `1 / -0` is
`-Infinity` where `1 / 0` is `+Infinity`.

**A NaN's sign does not exist in JavaScript.** No operation in the language
distinguishes a NaN with the sign bit set from one without. The two sides
genuinely produce different ones — x86 arithmetic produces negative NaNs,
constant folding produces positive ones — so `check-native-unit.cmake` folds
`=-nan` into `=nan` on **both** texts before comparing. That is a declared
property of the harness, not a defect being papered over, and it is recorded
here because it is exactly the kind of comparison rule that would otherwise be
discovered by somebody debugging a failure it caused.

**And it has a cost the audit found the hard way.** Because a NaN is
un-failable in the differential, a fixture global whose right answer is NaN
proves nothing about the code that computed it. Most globals of a divergence
fixture are NaN by their nature, so this fixture's negative proof
(`-DMUTATE`) is taken on `mod_pp`, which is `1.5`. A mutation of a NaN global
would be swallowed and the negative test would pass on a gate with no teeth —
the "green on nothing" failure the 2026-09-02 audit found twice.

### The test

`native-divergence-fixture.js`: `negzero`, `poszero`, `negzero_recip`,
`poszero_recip`, `negzero_plus_zero` (IEEE says `-0 + 0` is `+0`, in both
languages, and it is the one case where adding zero is not the identity) and
`negzero_times_neg`. The NaN-sign half is `check-native-unit.cmake`'s own
probe.

---

## ND-7 — `undefined` is carried as NaN: exact in three places, refused in the rest

**Status:** declared; **emitted where exact, refused where not**.
**Against:** the ctbrowser VM. **Introduced:** Phase 62½-C, the representation
table.

### The divergence

The tier has no boxed value, so `undefined` needs a `double` to live in and
that double is NaN. `LowerToEmitC.cpp`'s representation table states where
that is exact and where it is not, and the second half is the load-bearing
one:

| use | undefined answers | NaN answers | exact? |
|---|---|---|---|
| arithmetic | `undefined + 1` is NaN | `NaN + 1` is NaN | **yes** |
| relational `<` `<=` `>` `>=` | all false | all false | **yes** |
| truthiness, `!` | falsy | falsy | **yes** |
| equality `==` `===` `!=` `!==` | `undefined === undefined` is **true** | `NaN == NaN` is **false** | **no** |
| `typeof` | `"undefined"` | `"number"` | **no** |
| printing | `undefined` | `nan` | **no** |

An undefined-or-boolean has the same shape with `false` as its carrier: exact
in a branch and under `!`, wrong at equality.

### What the tier does about it

The three exact rows are **emitted**. The three inexact ones are **refused**,
each by name:

* equality — `admission::defined()`, *"equality on a value that may be
  undefined - NaN would not compare the way undefined does"*. `!=` and `!==`
  import as the same op with a negate flag, so one refusal covers four
  spellings.
* `typeof` — *"typeof, void and ~ are not native yet"*.
* printing — `admission::printable()`, *"store to global `x` may be undefined,
  and a global is where a value becomes an observable: this tier prints a
  Number as `%.17g` of the double, so undefined carried as NaN prints `nan`
  where the interpreter prints `undefined`"*. A `ctjs.store_global` is the one
  place the tier turns a value into an observation, and it is the one use of an
  `opt` that is refused for the REPRESENTATION rather than for a comparison —
  which is why it has its own sentence and does not reuse `defined()`'s.

  **This was an obligation on the HARNESS until Phase 59 slice 2 step 3.** The
  paragraph here used to say so: a global holding `undefined` is not a Number,
  the differential reference skips it, the binary prints `nan`, and the gate
  fails by naming a missing line — so every native fixture assigned each global
  before reading it and nothing in the LOWERING stopped a `nan`. Two programs
  reached it anyway. `var u; var z = u;` printed `u=nan z=nan`, and once slice 2
  step 2 stopped refusing a carried cell, `function pick(k) { var v; if (k > 0)
  { v = 5; } function get() { return v; } return get(); } var out = pick(-1);`
  printed `out=nan`. Both are refused now and both are pinned in
  `CTNative/Lowering/global-undefined.mlir`.

  **What made it affordable is the narrowing beside it.** Asked on its own the
  clause refused 7 globals across 5 fixtures and 11 across 4 lit tests, because
  a value returned through a carried cell or a closed-shape field was
  `opt<num>` FLOW-INSENSITIVELY — the box is emitted holding its hoisted
  `undefined` and a field read is seeded with `undefined` "because nothing
  orders the read after a store". Slice 2 step 3 drops each of those where a
  write DOMINATES the read, which is the population those refusals were. What
  is left is two coercions, both of them real possibilities rather than
  imprecision: `idx_in_range` in `native-divergence-fixture.js` (a dense
  array's element type — an index past the end really is `undefined`, ND-8) and
  `defaulted5` in `native-constructor-fixture.js` (the only store of the field
  is inside the constructor, a different `ctjs.func` from the read, so it needs
  a callee summary and not a dominance query).

### The test

Emitted half: `native-divergence-fixture.js`'s `u_plus`, `u_minus`, `u_times`,
`u_div`, `u_mod`, `u_pow` (arithmetic), `u_lt`, `u_le`, `u_gt`, `u_ge`
(relational — all four, because the claim is about all four), `u_truthy` and
`u_not` (truthiness), and `b_undefined_is_false` / `b_set_is_true` for the
boolean carrier. `native-struct-fixture.js`'s `read_before_write` is the
older witness of the same row.

Refused half: `divergence-refusals.mlir`, the EQUALITY and TYPEOF cases. The
RELATIONAL case beside them is the negative proof that the equality refusal is
the narrow rule it claims to be and not a blanket ban on reading a field that
was never written — the same value under `<` must still compile, and the
mutation that turns EQUALITY's `===` into `<` fails the pin.

---

## ND-8 — an out-of-range index is `undefined`, which is NaN, not undefined behaviour

**Status:** declared, **guarded**. **Against:** the ctbrowser VM.
**Introduced:** Phase 57A, the dense-array lowering.

### The divergence

`[10, 20, 30][7]` is `undefined` in JavaScript. The obvious lowering,
`v[static_cast<size_t>(i)]` on a `std::vector<double>`, is **undefined
behaviour** — not a different number, an unbounded one. The same is true of a
negative index and of a fractional one, and JavaScript answers `undefined` for
all three (`a[-1]` and `a[0.5]` are property reads that miss).

### What the tier does about it

`ctnative::vec_at`, emitted as part of the dense-array preamble in
`LowerToEmitC.cpp`:

```c++
inline double vec_at(const std::vector<double> & v, double i) {
  if (!(i >= 0.0) || i != std::trunc(i) || i >= static_cast<double>(v.size())) {
    return NAN;
  }
  return v[static_cast<std::vector<double>::size_type>(i)];
}
```

`!(i >= 0.0)` rather than `i < 0.0`, so a NaN index takes the guard too. The
element type's join starts at `undefined` for exactly this reason
(`TypeInference::elementTypeOf`), so the NaN this returns is a value the type
system already admitted rather than one smuggled past it.

**`a.length` is the other half and it needs no guard**, because the density
proof is what makes `size()` the right answer — see ND-12.

### The test

`native-divergence-fixture.js`'s `idx_in_range`, `idx_past_end`,
`idx_at_length`, `idx_negative`, `idx_nan` and `idx_infinite`. Each
out-of-range read has `+ 0` after it, which converts the `undefined` into a
NaN the differential can compare: a global *holding* undefined is not a Number
and the reference would skip it (ND-7's printing row).
`native-array-fixture.js` says in its own header why it omits this case; this
fixture is where it is covered.

### THE OPEN DEFECT THIS ENTRY FOUND — a fractional index is truncated, not missed

**Status: OPEN. This is a DEFECT, not a divergence** — the standard it fails
is the ctbrowser VM, and this file's own table says the VM wins, always.
Found 2026-09-02 by writing the witness above and running it.

`vec_at`'s `i != std::trunc(i)` clause answers NaN for a non-integral index.
**This interpreter truncates toward zero and then bounds-checks.** Measured
with `ctcompile-test-native-reference` on the devbox, 2026-09-02:

| expression | the interpreter | `vec_at` | V8 |
|---|---|---|---|
| `[10,20,30][0.5]` | `10` | `NaN` | `undefined` |
| `[10,20,30][1.5]` | `20` | `NaN` | `undefined` |
| `[10,20,30][2.9]` | `30` | `NaN` | `undefined` |
| `[10,20,30][-0.5]` | `10` | `NaN` | `undefined` |
| `[10,20,30][3.1]` | `undefined` | `NaN` | `undefined` |
| `[10,20,30][-1.5]` | `undefined` | `NaN` | `undefined` |

`[-0.5]` is the sharpest of them: `std::trunc(-0.5)` is `-0`, and `-0 >= 0` is
true, so the engine reads element 0.

So the engine diverges from ECMA-262 here (V8 reads the *property* named
`"0.5"`, which a dense array does not have), and the native tier picked the
standard's answer over the interpreter's. That is exactly the mistake ND-1
warns about in the string encoding: "semantics-preserving against the
specification" is the wrong target for a tier whose oracle is this VM.

**The fix is one clause in `LowerToEmitC.cpp`'s `vec_at`** — truncate the index
instead of rejecting it, and keep the two bounds tests — and it is NOT made
here, because that file belongs to another agent this week. It is reported
instead, in the form that cannot be ignored:

`ctcompile/test/native-index-truncation-fixture.js` is a program registered to
**fail** the differential gate (`ctcompile_native_unit_pipeline_index_truncation`,
`-DEXPECT_FAILURE=global 'idx_fractional' differs`). It doubles as the negative
proof for `native-divergence-fixture.js`: a fixture that claims every guard
agrees is worth nothing until the gate is seen catching one that does not, on
a module the pipeline generated. **The day `vec_at` is fixed, that test goes
red** — because the gate will start passing — and this entry has to be
rewritten by whoever fixed it.

### A second finding, recorded where it was found

`check-native-unit.cmake`'s `-DMUTATE` negative proof anchors the off-by-one
on the literal text `std::printf(`. The emitter writes that spelling only for
the hand-written `native-fixture.emitc.mlir`; a **generated** module gets a
bare `printf(`. So the driver's own mutation proof has never run against
pipeline output — it aborts with *"cannot mutate - no std::printf( in the
emitted C++"* — and every `ctcompile_native_unit_pipeline_*` test is
consequently a gate with no registered negative proof of its own. Changing the
anchor to `printf(` matches both spellings and is a one-word fix;
`check-native-unit.cmake` is not this work's to edit, so it is reported.

---

## ND-9 — a bitwise operator is ToInt32, which is not a C++ cast

**Status:** declared, **refused**. **Against:** the ctbrowser VM.
**Introduced:** Phase 62½-C.

### The divergence

`x | 0` is `ToInt32(x)`: truncate toward zero, then reduce modulo 2^32 into a
signed 32-bit range. `2147483648 | 0` is **-2147483648** in JavaScript.
`static_cast<int32_t>(2147483648.0)` is **undefined behaviour** in C++ — the
value is not representable, so the standard imposes nothing at all. The same
is true of `NaN | 0` and `Infinity | 0`, which JavaScript answers `0` for.
`>>>` adds a second wrap on top, to unsigned.

### What the tier does about it

Refuses, and the refusal comes from the **static** operator family: *"a static
bitwise operator is not native yet"*. `BytecodeImport.cpp`'s `binary_rows`
marks all six bitwise opcodes non-re-entering, so `&`, `|`, `^`, `<<`, `>>`
and `>>>` all import as `ctjs.binary_static`.

**A note the test records rather than pins.** `ctjs.binary`'s own arm answers
*"a bitwise or string operator is not native yet"*, and that sentence is
reachable only through `BinaryKind::Concat`. A concatenation needs a string,
and a string in a native candidate is refused at its **constant** first, so
that string has no minimal witness today. It is left un-pinned and said out
loud rather than pinned with a program that does not produce it.

### The test

`divergence-refusals.mlir`, the BITWISE case (and the CONCAT case beside it,
which pins the constant refusal a string actually hits). The wrap is emittable
— `(int32_t)(uint32_t)fmod(trunc(x), 4294967296.0)` and its NaN guard — and
would be the same shape ND-4's guard has; until it is written, the operator is
diagnosed rather than approximated.

---

## ND-10 — an inherited `Object.prototype` name is not `undefined`

**Status:** declared, **refused**. **Against:** the ctbrowser VM.
**Introduced:** Phase 56 (closed shapes). **Found:** the adversarial audit of
2026-09-02.

### The divergence

A closed shape lowers to a C++ `struct` whose fields are the keys the program
uses; a key that is read and never written is `undefined`, which this tier
carries as NaN (ND-7). That is right for an ordinary key and **wrong for a
name `Object.prototype` answers**:

```js
var p = { x: 1 };
if (p.constructor) { /* taken in the interpreter */ }
```

`p.constructor` is a function — truthy — where the generated struct finds an
uninitialised field. The two sides took different branches, and nothing was
refused anywhere. `constructor`, `toString`, `valueOf`, `hasOwnProperty`,
`isPrototypeOf`, `propertyIsEnumerable`, `toLocaleString`, `__proto__` and the
four `__define*`/`__lookup*` accessors are the list.

### What the tier does about it

Refuses a **read-only** key that names an `Object.prototype` member
(`admission::namesObjectPrototypeMember`). A key that is also **written**
shadows the inherited one and stays admitted, which is the whole precision of
the rule: an own property is what both sides read.

### The test

`shape-field-names.mlir`, the INHERITED case for the refusal and the SHADOWED
case for the admission beside it — the second is what stops the rule
degenerating into a blanket ban on a set of names.

---

## ND-11 — a field name that C++ cannot spell

**Status:** declared, **refused**. **Against:** the C++ language, not the VM.
**Introduced:** Phase 56. **Found:** the adversarial audit of 2026-09-02.

### The divergence

Field names are emitted verbatim — `cIdentifier()` sanitises symbols, not
members — so `({class: 1}).class` emitted `double class;` and `({NAN: 1}).NAN`
emitted `double NAN;` under this tier's own `<cmath>`. Both are perfectly good
JavaScript and neither compiles, on a program the pipeline had already
declared native and which Phase 63 Step 7 then builds with `-Werror`.

This is the one entry where the disagreement is not about an *answer*. It is
recorded here anyway, because §2 makes ctcompile the diagnostician: a C++
diagnostic on generated code is a ctcompile bug, so "the generated file does
not compile" is a divergence from the interpreter in the only sense that
matters — the interpreter runs the program and the tier does not.

### What the tier does about it

Refuses, naming the field: *"field `X` is a C++ keyword or a macro of
`<cmath>`/`<cstdio>`, so the generated struct would not compile"`*. The list is
`admission::isReservedInCpp`. Refused rather than mangled: a generated field
keeps the JavaScript name a reader is looking for, and mangling every field to
buy this rare case is a trade a later phase should make deliberately.

### The test

`shape-field-names.mlir`, the KEYWORD and MACRO cases.

---

## ND-12 — `a.length` is `size()` only because density is proved

**Status:** declared, **refused** for the three sparsity routes.
**Against:** the ctbrowser VM. **Introduced:** Phase 57A.

### The divergence

A JavaScript array is not a `std::vector`. `a[100] = 1` on an empty array gives
`length` 101 with one element; `a.length = 5` resizes and leaves holes;
`delete a[0]` punches one. Under any of those, `length` stops being `size()`
and an element read stops being `v[i]` — and `std::vector<double>` cannot
represent a hole at all, so there is no lowering to get wrong: there is only a
refusal to make.

### What the tier does about it

Admits `length` as `size()` **only** at a site where the array is built by its
own literal and its appends and thereafter only read. The three routes out of
that are each refused by name, and the messages name the *route* rather than
the symptom:

* *"an array literal whose `length` is assigned - that resizes it, and a
  resize leaves holes no `std::vector` can hold"*
* *"an array literal written through an index - `a[100] = 1` gives `length`
  101 with one element, so density is not proved"*
* *"an array literal with an element deleted - `delete a[0]` punches a hole in
  it, so density is not proved"*

### The test

`native-array.mlir`'s LENGTH, INDEXED and DELETED cases for the refusals;
`native-array-fixture.js`'s `counted()` through the differential gate for the
admission.

---

## ND-13 — `std::vector<bool>` is not a container of `bool`

**Status:** declared, **refused**. **Against:** the C++ standard library.
**Introduced:** Phase 57A.

### The divergence

`[true, false]` has an obvious lowering that is wrong for a reason that has
nothing to do with JavaScript: `std::vector<bool>` is a bit-packed
specialisation whose `operator[]` returns a **proxy** that aliases the
container and converts differently from a `bool`. `vec_at` would return a
reference to a temporary; `auto x = v[0]` would deduce the proxy.

### What the tier does about it

Refuses: *"an array of booleans - `std::vector<bool>` is a bit-packed
specialisation whose elements are a proxy, not a `bool`"*. An array whose
elements are anything else non-numeric is refused by its element type instead,
so the two messages stay distinguishable.

### The test

`native-array.mlir`, the BOOLEANS case (and MIXED beside it).

---

## ND-14 — the library map: twelve functions whose obvious C++ spelling is wrong

**Status:** declared, **refused today**. **Against:** the ctbrowser VM.
**Introduced:** Phase 61 (`StdLibMap.td`).

### The divergence

`ctcompile/include/ctcompile/StdLib/StdLibMap.td` classifies every library
function the specification names as `exact` (20 rows), `divergent` (12) or
`refused` (3). The twelve divergent rows each carry a **witness expression**
and the reason, and they are not corner cases:

| row | witness | why the obvious spelling is wrong |
|---|---|---|
| `Math.round` | `Math.round(-0.5)` | JS rounds a tie toward +Infinity (`-0`); `std::round` rounds away from zero (`-1`) |
| `Math.min` | `Math.min(1, NaN)` | `std::min` is written in terms of `<`, so it answers `1` where the specification returns NaN; also wrong on `-0` and on the empty call |
| `Math.max` | `Math.max(-0, 0)` | `std::max(-0, 0)` is `-0` because `-0 < 0` is false; the specification requires `+0` |
| `Math.pow` | `Math.pow(1, NaN)` | ND-4, in its library spelling |
| `Math.SQRT1_2` | `Math.SQRT1_2` | `1.0 / std::numbers::sqrt2` rounds twice and lands one ulp low |
| `Math.random` | `Math.random()` | the engine's stream is seeded and deterministic, and three goldens depend on it |
| `Date.now` | `Date.now()` | the engine's clock is frozen at a fixed base unless an embedder installs one |
| `new Set()` | `{NaN, NaN, +0, -0}` | SameValueZero keying and insertion order; `unordered_set` has neither |
| `new Map()` | the same | worse: a second NaN key makes the first unreachable |
| `new RegExp()` | `/(.)\1/.test('aa')` | this engine has no backreferences or lookbehind; Boost has both, and they disagree in BOTH directions |
| `JSON.parse` | `JSON.parse('{oops')` | this VM answers `undefined` where `boost::json` throws, and which path runs depends on the input |
| `JSON.stringify` | `JSON.stringify(0.1)` | a different number formatter: `"0.1"` here, `"1E-1"` there |

### What the tier does about it

**Nothing yet, and that is the honest status.** `StdLibMap.td` is referenced
from `LowerToEmitC.cpp` only in a comment: no builtin call lowers natively at
all today, so every one of these is refused by the generic *"`ctjs.get_property`
is not native yet"* / *"`ctjs.call` is not native yet"* path rather than by a
row of its own. The table is the roadmap for when they do, and it exists so
that the day a `Math.round` is lowered, the person doing it meets the witness
before they write `std::round`.

**Obligation (Phase 61).** No divergent row may be lowered to the target its
`Target` field names. The field is kept so the refusal names the thing it
refuses, not so it can be emitted.

### The test

`ctcompile/test/StdLibMap.cpp`, registered as `ctcompile_stdlib_map`, over the
TableGen-generated table.

---

## What is NOT in this list, and why

* **PDLL's expressiveness.** After `retype()` a `ctjs` op carries an `f64`
  where its ODS declares `CTJS_ValueType`, so the function does not verify and
  no pattern driver can walk it — which is why exactly one arm of `replace()`
  is a declarative pattern. That is a property of this lowering's staging, not
  a place the tier's *answer* differs from the interpreter's, so it belongs in
  `ctcompile/docs/plans/ctcompile.md` and not here. This file's own rule 4 is
  the test: a divergence is a difference that survives into an answer.
* **The specialisation cap.** Part 24 Phase 63 Step 4 asks for numbers at cap
  1 and cap 4; Phase 62½-B is at option 3 (the join) only, so there is no
  specialisation to cap and no divergence it could introduce. Recorded in
  `ctcompile/docs/native-ab.md` rather than here.

---

*Next entry: ND-15.*
