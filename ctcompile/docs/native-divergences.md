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

*Next entry: ND-2.*
