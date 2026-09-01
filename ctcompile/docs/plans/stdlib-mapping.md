# The standard-library map, and where it diverges

**Phase 61 of the native backend** — `ctcompile-plan/24-native-cpp-backend.md`.
The specification's §8 table maps a JavaScript global onto a C++ or Boost
facility. This document is the verdict on each row.

The table itself is `ctcompile/include/ctcompile/StdLib/StdLibMap.td` and the
test that keeps this document honest is `ctcompile/test/StdLibMap.cpp`
(`ctest -R ctcompile_stdlib_map`). **Nothing here is asserted that the test does
not check**, and the two places a claim can be made — the `.td`'s `Verdict`
field and the probe beside it — disagree loudly rather than quietly.

## The shape of the answer

**Of the eight rows in the specification's §8 table, one is correct.**

| the specification's row | verdict |
|---|---|
| `Math.PI`, `Math.E` -> `std::numbers::pi`, `std::numbers::e` | **correct** |
| `Math.abs/sin/pow/floor/...` -> `std::abs/sin/pow/floor` | **mixed** - `sin` and `floor` are exact, `abs` is the wrong spelling, `pow` is a different function |
| `JSON.parse` / `JSON.stringify` -> `boost::json` | **wrong** - a different number formatter, and a different failure mechanism |
| `new Set()` -> `std::unordered_set<T>` | **wrong** - key equality and iteration order, independently |
| `new Map()` -> `std::unordered_map<K,V>` | **wrong** - the same, and a NaN key becomes unreachable |
| `new RegExp(...)` -> `boost::regex` | **wrong** - a different regex engine; disagrees silently in both directions |
| `Date.now()` -> `std::chrono::system_clock::now()` | **wrong** - breaks the determinism invariant and ignores the host's clock |
| `String.prototype.trim` -> `boost::algorithm::trim_copy` | **unprovable** - agrees today, but only because of the process's locale |

Four more rows the table's `...` implies were checked and are also wrong:
`Math.round`, `Math.min`, `Math.max` and `Math.SQRT1_2`. A fifth, `Math.cbrt`,
is refused for a subtler reason.

That is not an argument against the phase; it is the argument for the phase
being a table with verdicts rather than a `switch` somebody writes from memory.

| verdict | count | meaning |
|---|---|---|
| `exact` | 20 | the C++ target and the interpreter agree, on inputs chosen to separate them |
| `divergent` | 12 | they **disagree**, and the map names the expression that shows it |
| `refused` | 3 | no lowering, and no witness is possible — the reason is all there is |

**The oracle is the interpreter, not ECMAScript.** That is the dialect's own
policy — *"when a CTJS operation and the ctbrowser VM disagree, the VM is
correct by definition"* — so `exact` means *agrees with
`ctbrowser/lib/Script/builtins/`*. Where the engine itself deviates from the
standard, the mapping **inherits** the deviation, which is the right answer for
a backend whose job is to agree with the tier beside it. The two places that
happens are called out below.

## How a row is checked

Every `exact` and `divergent` row carries at least one probe. A probe is a
JavaScript expression evaluated in a real `context` and the proposed C++
evaluated beside it; both are reduced to a token and compared.

Two details make the comparison able to see what it is looking for:

* **Numbers are compared by BITS, not by text.** `String(-0)` is `"0"` in
  JavaScript, so a text comparison cannot tell `+0` from `-0` — and `-0` is
  the answer that separates `Math.max` from `std::max` and `Math.sign` from
  `copysign`. `Math.max(-0, 0)` is the row this actually saves: JavaScript
  answers `+0` and `std::max` answers `-0`, and **a printed comparison reports
  the two as identical**. It is the only divergence in this document that a
  test written the obvious way would miss entirely.
* **A `divergent` row must DISAGREE.** The test asserts the disagreement, so the
  refusal list cannot rot in either direction: promote a row to `exact` without
  repairing its target and the test goes red; repair the target, and the witness
  stops witnessing and it goes red the other way.

---

## The rows that are exact

### The rounding family — `floor`, `ceil`, `trunc`, `sqrt`

`std::floor`, `std::ceil`, `std::trunc`, `std::sqrt`. Exact on **every** host,
because IEEE-754 pins all four bit-for-bit including the two cases a rounding
helper usually gets wrong: the sign of zero and the propagation of NaN.
`Math.ceil(-0.5)` is `-0` and `std::ceil(-0.5)` is `-0` too.

`sqrt` is the one transcendental-*looking* row that is exact everywhere, because
IEEE-754 requires square root to be correctly rounded.

### `Math.abs` → `std::fabs`, **not** `std::abs`

The specification's table says `std::abs`. `std::fabs` is what the engine calls
and what has to be emitted. See the refusal below for why the difference is not
cosmetic.

### `Math.sign` → `(x > 0 ? 1.0 : (x < 0 ? -1.0 : x))`

`<cmath>` has no sign function, and `std::copysign(1.0, x)` is wrong twice: it
answers `-1` for `-0` where JavaScript answers `-0`, and `1` for NaN where
JavaScript answers NaN. The ternary is exact because its fall-through returns
`x` **unchanged**, which is precisely the set {`+0`, `-0`, `NaN`}.

### The transcendentals — `sin`, `cos`, `tan`, `exp`, `log`, `atan2`

Exact **against the oracle**, and that qualification is the whole content of the
row. ECMAScript leaves these implementation-approximated, so there is no
standard to be exact against; what makes the mapping sound is that the engine
calls the host's libm and so would the generated code, in the same process,
against the same libm.

**One measured caveat, and it is a real risk elsewhere.** A transpiler that
emits `std::sin(1.0)` for a constant argument hands the compiler a constant to
fold, and GCC folds with MPFR while the interpreter calls glibc at run time.
Those are two different implementations of `sin`. On the devbox (GCC 13.3.0,
glibc 2.39) they are bit-identical for all eight of `sin`, `cos`, `tan`, `exp`,
`log`, `atan2`, `pow` and `sqrt` — measured, not assumed. **This is a property
of that host pair and not of the mapping.** A different libm, a different
compiler, or `-ffast-math` anywhere near the generated file can separate them,
and the separation would be invisible: one ulp in a number nobody prints.

### The constants — `PI`, `E`, `SQRT2`, `LN2`, `LN10`, `LOG2E`, `LOG10E`

`std::numbers::*`. Exact by construction rather than by luck: the engine sets
each of them from `<numbers>` too, so the mapping and the oracle read the same
bits out of the same header. `values.cpp` says why they are not typed out — *"a
transcribed constant is a digit waiting to be wrong, and one that is wrong in
its last few places is invisible"*.

`Math.SQRT1_2` is **not** in this group. See below.

### `String.prototype.trim` → `ctbrowser::trim(s, ctbrowser::js_whitespace)`

**Not `boost::algorithm::trim_copy`**, which the specification named and which
is refused below.

The engine already owns this function. `core/algorithms.hpp` has a `constexpr`
`trim(text, set)` whose whitespace **set is a parameter**, because *"HTML,
JavaScript and the GLSL preprocessor genuinely disagree about what whitespace is
and unifying them would be a bug"* — and `js_whitespace` is the JavaScript one,
`" \t\n\r\f\v"`, byte-for-byte what `builtins/text.cpp`'s `trim` uses. It
returns a `string_view` rather than a `string`, which the same header records as
**7× faster** over 200,000 trims (2.0 ms against 14.2 ms).

---

## The rows that diverge, with the witness

### `Math.round` → `std::round` — **different functions**

| | `Math.round(-0.5)` | `Math.round(-2.5)` |
|---|---|---|
| JavaScript | `-0` | `-2` |
| `std::round` | `-1` | `-3` |

JavaScript rounds a tie toward **+Infinity** (21.3.2.28); `std::round` rounds a
tie **away from zero**. They agree on every positive input, which is exactly
what lets the mistake survive review.

`std::floor(x + 0.5)` is not the repair either, and `values.cpp` lists the three
separate ways it is wrong: `x + 0.5` itself rounds (so `0.49999999999999994`
answers 1), `+ 0.5` destroys the sign of zero, and above 2⁵³ the addition moves
an integral value that step 2 requires returned unchanged.

### `Math.min` / `Math.max` → `std::min` / `std::max` — **wrong three ways**

| | JavaScript | `std::` |
|---|---|---|
| `min(1, NaN)` | `NaN` | `1` |
| `max(-0, 0)` | `+0` | `-0` |
| `max()` | `-Infinity` | *does not exist* |

`std::min`/`std::max` are written in terms of `<`, and IEEE comparison says no
to everything involving NaN — so the NaN simply vanishes, where 21.3.2.24 step
4.a returns it the moment it sees one. The zero case is live rather than
theoretical: `-0 < 0` is false, so `std::max` returns its first argument.
`values.cpp` already says all of this about the *interpreter's* own
implementation; the specification's table reintroduces the bug the engine fixed.

### `Math.pow` → `std::pow` — C99 and ECMAScript disagree

`std::pow(1.0, NaN)` is `1` and `std::pow(-1.0, ∞)` is `1`; JavaScript answers
`NaN` for both. C99 says `pow(±1, y)` is 1 for **every** y including NaN and the
infinities, and `Number::exponentiate` says NaN for exactly those. A `1` here is
worse than a throw, because a page will do arithmetic on it rather than checking
it with `isNaN`.

### `Math.SQRT1_2` → `1.0 / std::numbers::sqrt2` — one ulp

`std::numbers::inv_sqrt2` **does not exist**; `<numbers>` has `inv_sqrt3` and
`inv_sqrtpi` and no `inv_sqrt2`, so the natural spelling does not compile. The
obvious replacement rounds **twice** — once in `sqrt2`, again in the division —
and lands one ulp low:

```
1.0 / std::numbers::sqrt2   0x3fe6a09e667f3bcc
std::numbers::sqrt2 / 2.0   0x3fe6a09e667f3bcd   <- correctly rounded
```

Halving is exact (it decrements the binary exponent and touches no mantissa
bit), which is why the engine derives it that way. 21.3.1.8 asks for the Number
value *nearest* the true root, so this is an exact requirement rather than an
approximation, and one ulp is exactly the size of error a printed comparison
hides.

### `Math.random` → any other generator — **the determinism invariant**

`CLAUDE.md`: *"`Math.random` is seeded and DETERMINISTIC by default"*, because
three example pages byte-compare their render against `ctbrowser/test/golden/*.ppm` and *"a
page drawing with random cannot have a golden otherwise"*. The stream is a
`xorshift64*` held per context in `builtins/values.cpp`. Any other generator is a
different stream, and every golden that touches it moves.

This is not a row that can be repaired by picking a better `<random>` engine. If
the native backend wants `Math.random`, it must call **the context's** generator.

### `Date.now()` → `std::chrono::system_clock::now()` — the same invariant

`Date.now()` reads `context::clock_ms()`, whose default is the **fixed** base
`1767225600000` (2026-01-01T00:00:00Z). `vm.hpp` says why: *"the frozen clock was
deliberate, for the reason `Math.random` is seeded"*. An embedder that wants real
time installs one with `set_clock`, and `browser::set_clock` is that embedder.

A wall-clock call in generated code bypasses **both**: it breaks every golden,
*and* it ignores the clock the host installed — so an application whose page is
supposed to see a mocked time would silently see the real one.

### `new Set()` / `new Map()` → `std::unordered_set` / `std::unordered_map`

Two **independent** refutations, either of which alone kills the row.

**Key equality.** A JavaScript Set keys by SameValueZero: `NaN` is equal to
itself and `+0` is equal to `-0`.

```js
s = new Set(); s.add(NaN); s.add(NaN); s.add(0); s.add(-0);   // s.size === 2
```
```cpp
std::unordered_set<double>{NaN, NaN, +0.0, -0.0}.size()       // == 3
```

`operator==` on doubles says `NaN != NaN`, so the two NaNs occupy two slots. For
`Map` it is worse than an extra element: a second `NaN` key makes the **first**
unreachable, because no lookup for `NaN` can ever equal a stored `NaN`.

**Iteration order.** JavaScript iterates in insertion order — the engine literally
stores entries in a `std::vector` — and `unordered_set` does not. Inserting
3, 1, 2 iterates back as **2, 1, 3** on libstdc++ where JavaScript gives
**3, 1, 2**.

The lowering is an insertion-ordered container with a SameValueZero hash, and
that is a data structure someone has to write. `unordered_set` is not it.

### `new RegExp(...)` → `boost::regex` — **the worst failure available**

`boost::regex` is not this engine's regex. `script/regex.hpp` is a
self-contained backtracker ported from ctjs's `rxd`, and its header says outright
what it does not have: *"NOT here, and said out loud rather than mis-matched:
lookbehind and backreferences."*

| pattern | subject | ctbrowser | `boost::regex` |
|---|---|---|---|
| `(.)\1` | `"aa"` | **false** | **true** |
| `(.)\1` | `"a1"` | **true** | **false** |
| `(?<=a)b` | `"ab"` | **false** | **true** |

It disagrees **in both directions**, and neither direction reports anything. A
`\1` falls through `rx_escape_char`'s default and becomes the literal character
`'1'`; a lookbehind makes the compiled program not-`ok`, so `test` answers false
for every subject forever.

**There is no proved subset yet, and claiming one would be the mistake.** Beyond
these two, `.` and every flag would each need their own proof — `lastIndex`
lives on the JavaScript object rather than in the matcher, `y` is handled by
`exec` rather than by the program, and the two engines' treatment of line
terminators inside `.` has not been compared. A regex that silently matches
differently is the failure this phase exists to prevent, so the whole row is
refused until a subset is *proved* rather than assumed.

### `JSON.parse` → `boost::json::parse` — the failure path is a different mechanism

`builtins/async.cpp` answers **`undefined`** for a document that does not parse —
this VM has no exceptions — where `boost::json::parse` throws
`boost::system::system_error`.

Which path a call takes depends on the **input**, so no static analysis can rule
the failure path out, and a page written as `if (parsed === undefined)` would
instead have an exception unwind through it.

### `JSON.stringify` → `boost::json::serialize` — a different number formatter

| | JavaScript | `boost::json::serialize` |
|---|---|---|
| `0.1` | `0.1` | `1E-1` |
| `1e21` | `1e+21` | `1E21` |

JavaScript's `Number::toString` is the shortest decimal that round-trips, which
the engine implements in `Script/number_format.cpp`. **Every number in every
serialised document changes.**

The top-level `undefined` rule diverges too: 25.5.2 step 12 makes
`JSON.stringify(undefined)` the *value* `undefined` and not any string — the
engine's builtin has a comment about a page that tested for it and was handed
`"null"` — and a `boost::json::value` cannot represent that at all.

---

## The rows that are refused, with no witness

These are the rows a test **cannot** pin, which is why the reason has to be
written properly.

### `Math.abs` → `std::abs` — an overload set with an integer in it

`std::abs` is an overload *set*, and `<cstdlib>` puts `int` and `long` overloads
in it. **Phase 55 narrows a bounded number to `int32_t` on purpose**, and the
moment it does, `std::abs(x)` selects the integer overload — where
`std::abs(INT_MIN)` is undefined behaviour and returns `INT_MIN` on this target,
against JavaScript's `2147483648`. Measured on the devbox:
`std::abs((int)-2147483648) = -2147483648`.

`std::fabs` has no integer overload and cannot be captured this way. Refused
rather than witnessed because **the witness is the undefined behaviour itself**,
and a test that relies on what UB happens to do is worse than no test — this
project has already declined to pin one guard for exactly that reason (see the
note on `put` in `differential.js`).

### `Math.cbrt` → `std::cbrt` — exactness that belongs to the host

The engine does **not** call `std::cbrt`: it rounds the result to the nearest
integer and returns that when the cube is exact, because `values.cpp` measured
glibc answering `3.0000000000000004` for `cbrt(27)`.

**On the devbox today (glibc 2.39) `std::cbrt(27.0)` is exactly 3**, and
`std::cbrt(216.0)` is exactly 6 — so the correction is invisible here and the two
agree. That is not a reason to map the row; it is the reason to refuse it. A
mapping whose exactness is a property of the host's libm cannot be proved, and
this project byte-compares renders across Linux and Windows. *(It also means the
comment in `values.cpp` records a measurement that no longer reproduces on this
box — the correction is still right to keep, but the number in the comment is
from a different libm.)*

### `String.prototype.trim` → `boost::algorithm::trim_copy` — locale, not program

`trim_copy` classifies with `std::isspace` against the **global locale**, so what
it trims is a property of the process rather than of the program. Under the
classic locale that set is `" \t\n\v\f\r"` — byte-for-byte the engine's — so it
**agrees today and there is no witness to write**. It stops agreeing the moment
anything calls `std::locale::global`.

`CLAUDE.md` rejects exactly this class of dependency: the engine's helpers are
*"ASCII-only ON PURPOSE"*, because *"goldens are byte-compared across Linux and
Windows, so a locale-aware fold would make a render depend on `LC_ALL`"*.
Refused for **unprovability**, not for observed disagreement.

---

## Declared divergences from ECMAScript, inherited from the engine

These are not mapping errors. They are places where the engine deviates and the
mapping is right to follow it — recorded here because the native backend's
divergence list (part 24, Phase 63 step 5) has to be one list, not two.

1. **`trim` does not trim non-ASCII whitespace.** ECMAScript trims U+00A0 and
   U+FEFF among others; `ctbrowser::js_whitespace` is ASCII-only and deliberate.
   `"\u{a0}x\u{a0}".trim()` returns the string unchanged. Both the engine and
   Boost behave this way; only the standard disagrees. **Pinned by a probe**, so
   a change to one side alone is caught.
2. **The transcendentals are the host's libm.** Exact against the oracle,
   unspecified by the standard, and not portable across libm implementations.
   See the caveat above.

## What this leaves for Phase 53 and Phase 63

The refusal list *is* the roadmap. In the order the corpora would want them:

1. **An insertion-ordered, SameValueZero-keyed map and set.** This unblocks
   `Set` and `Map` together and needs no new analysis — it is a container.
2. **`ctnative` helpers for the five arithmetic rows** — `round`, `min`, `max`,
   `pow`, and the `SQRT1_2` constant. Each is under ten lines and each already
   exists, correct, in `builtins/values.cpp`; the work is deciding where a
   shared helper header lives, which is Phase 53's call and not this one's.
3. **`Math.random` and `Date.now` through the context**, never through `<random>`
   or `<chrono>`. Both are determinism invariants with goldens behind them.
4. **A *proved* regex subset**, or nothing. Two engines, and the disagreement is
   silent in both directions.
5. **JSON last.** It needs the engine's number formatter to be callable from
   generated code, which is a bigger question than the row.

## Notes for whoever writes the lowering

* **`ctcompile/include/ctcompile/StdLib/StdLibMap.td` is the table.** It
  deliberately includes nothing — not `OpBase.td`, not a dialect — so it reads
  with `llvm-tblgen --dump-json` on a box with no MLIR. The declarative rewrite
  rules belong in a **second** `.td` that includes this one and `OpBase.td`;
  that is the layer where MLIR belongs, and it is why the rows are in TableGen
  rather than in a fifth `.def`.
* **The rewrite rules could not be written in this phase**, and the reason is
  ownership rather than design: a `Pat<>` needs a target operation, Phase 53's
  `ctnative` dialect did not exist when this landed, and every file where a
  pattern could be registered (`lib/CTJS/**`, `lib/CTNative/**`) belongs to
  another track. See the report in the commit message.
* **A mapped row is not a licence to emit.** Every one of them still needs the
  guard the specification's own §8 does not mention: that the global was not
  reassigned. `Math = {abs: () => 7}` is legal JavaScript, and `Math.abs` is a
  property read on an ordinary mutable object. That proof is Phase 56's shape
  closure applied to `globalThis`, and no row above is sound without it.
