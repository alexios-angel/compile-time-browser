# Script — the compiler, the VM, the standard library

`include/ctbrowser/script/` and `lib/Script/` — `compile.hpp`/`compile.cpp`
(JS -> bytecode; the header declares one function and the .cpp holds the whole
compiler), `vm.hpp`/`vm.cpp` (the register machine), `value.hpp` (NaN boxing),
`bytecode.hpp`, `builtins.hpp`/`builtins.cpp` (the standard library). The DOM
API pages call is not here - that is `shell/bindings.hpp`, in `docs/shell.md`.

## JAVASCRIPT (2026-07-25)

**The MDN breakout tutorial runs, unmodified** — `examples/demos/pong.cpp` loads
`examples/pages/pong.html`, a byte-for-byte copy. `examples/pages/fetchboard.html`
compiles too, and the 66 KB bundled `space-invaders.html` stops at exactly ONE
thing: a regex literal.

The compiler covers the language now: `+=` and friends, member/index `++`, real
`this`, `break`/`continue`/labels, `do..while`, `try`/`catch`/`finally`/`throw`
(VM handler stack, unwinds call frames), computed method calls (`a[m]()` keeps
its receiver), `for..of`/`for..in`, template literals, `switch` (with
fallthrough), `class` + `new` + `extends` + **`super`**, optional chaining,
spread (array and object), computed object keys, `delete`, `in`, `instanceof`,
the bitwise operators (ToInt32/ToUint32, so `-1 >>> 0` is 4294967295), and
`async`/`await`.

**Still rejected, each by name rather than mis-compiled**: `regex` (no regex
engine — this is what stops space-invaders), `yield`/generators, tagged
templates, object-literal get/set accessors. The comma operator is a ctjs
PARSER gap, not a compiler one.

**Functions are objects** — a closure carries a property table, which is where a
class keeps its statics, its `prototype` and the `__home` that makes `super`
resolve against the class a method was WRITTEN in rather than against `this`
(three-deep hierarchies recurse forever otherwise).

**Promises** — this paragraph used to say "settled-only, `then` runs its
callback immediately", which stopped being true on 2026-08-09 (a job queue,
`new Promise(executor)`, `await` on a pending promise suspends the frame; see
"A `value` captured by a native lambda" below). Since 2026-09-12 the rest of
the standard's shape is in as well: a throwing `then` handler rejects the next
promise (`call_fenced` in `deliver`), an async body's uncaught throw rejects
the promise it returned (the compiler's fence), async generators queue their
requests, `for await` runs the async iteration protocol, and `Array.fromAsync`
is written over it. `async function` returns a promise through `op::wrap_promise`,
via a factory hook the standard library installs — the VM cannot build a
promise by itself.

Later on 2026-09-12 `builtins/async.cpp` was rewritten to the whole of 27.2:
`resolve` is a real promise resolve function (CreateResolvingFunctions, with
`[[AlreadyResolved]]` shared by the pair), a thenable is adopted by a SEPARATE
NewPromiseResolveThenableJob - so `Promise.resolve(thenable)` takes two ticks
and a handler returning a promise takes three, as in every other engine -
`then` goes through SpeciesConstructor and NewPromiseCapability (so
`class P extends Promise` works, and `p.then()` on one is a `P`), `finally`
is ThenFinally/CatchFinally over the species constructor, `Promise.try` and
`Promise.withResolvers` exist, and `all`/`allSettled`/`any`/`race` walk the
ITERATOR PROTOCOL (any iterable, `resolve` read once per call, IteratorClose
on an abrupt step, each element function called once). The VM's settler hook
now RESOLVES rather than fulfils, so an async body returning a thenable
adopts it. What the VM still does by itself: `resume()` reads a returned
promise's `__value` directly, so an async body that returns a PENDING promise
after an await settles with `undefined` (the fix is one line in
`vm/call/coroutines.cpp`: hand `returned` to the settler unwrapped).
`%AsyncFromSyncIteratorPrototype%` and `%AsyncIteratorPrototype%` are real
prototype objects now, and `%AsyncGeneratorPrototype%` inherits the latter.

**`Iterator` and the iterator helpers** (`builtins/collections/iterator.cpp`,
2026-09-12): the abstract `Iterator` constructor (subclassable, `new
Iterator()` is a TypeError), `Iterator.from`, `Iterator.concat`,
`Iterator.zip`/`zipKeyed`, `%Iterator.prototype%` with `map`, `filter`,
`take`, `drop`, `flatMap`, `reduce`, `toArray`, `forEach`, `some`, `every`,
`find`, `includes`, `join`, `chunks`, `windows`, `@@dispose`, and the two
accessor properties (`constructor`, `@@toStringTag`) whose setters define an
own property on the receiver. A helper is an object on
`%IteratorHelperPrototype%` with a private state slot and the generator state
machine of 27.1.2.1 (`next` while running is a TypeError; `return` closes the
underlying iterator, inner one first for `flatMap`). `%GeneratorPrototype%`
is re-parented onto `%Iterator.prototype%`, so a generator object has the
helpers; the library's own array/map/set iterators (`list_iterator`) do NOT
yet, because their prototype is per-instance. `DisposableStack`,
`AsyncDisposableStack` and `SuppressedError` (`collections/disposable.cpp`)
are installed beside them, and `Promise.allKeyed`/`allSettledKeyed` and
`Iterator.zip`/`zipKeyed` (2026 proposals the corpus carries) with them. All
of these installers are called from `install_promise` because `builtins.cpp`
was not this change's to edit. `WeakRef` and `FinalizationRegistry`
(`collections/weak.cpp` - strong references, no finalisation, the WeakMap
deviation) are written and compiled but NOT installed: ctcompile's
escape-cycle test pins `typeof WeakRef === "undefined"` as the documented
divergence ND-2 (`ctcompile/docs/native-divergences.md`), and that call is
one commented-out line in `install_promise` once the pin is lifted.

**`===` compares STRINGS BY CONTENT** — it compared the NaN-boxed words, which
is right for objects (identity) and singletons and wrong for strings, since two
strings with the same characters are almost never the same allocation. So
`e.code === "Space"` was false for every event, `switch` on a string never
matched a case, and `indexOf`/`includes` could not find a string in an array.
`==` always did compare content, which is why the one page in the suite that
uses it (pong) worked and invaders did not. `NaN`, `Infinity` and `undefined`
are defined globals now too — `NaN` was an undefined global, so `NaN === NaN`
was TRUE.

**Standard library** is `ctbrowser/lib/Script/builtins.cpp` — `Math`, `Array.prototype`
(incl. map/filter/reduce/sort, which call back into the VM via
`context::call`), `String.prototype`, `Number.prototype`, `Object` statics,
`JSON` parse/stringify, `Promise` (resolve/reject/all),
`parseInt`/`parseFloat`/`isNaN`/`String`/`Number`. Reached through **prototype
tables per value kind** (`context::set_prototype`) plus **per-object prototype
chains** (`class`/`extends`). `Math.random` is seeded and DETERMINISTIC by
default — the test story is byte-comparable goldens, and a page drawing with
random cannot have one otherwise.

Top-level `var` is a GLOBAL by design (pages define functions the host calls by
name), so the only frame-0 locals are for..of items and catch parameters — and
those ARE capturable, which is what makes `for (const x of xs) fns.push(() => x)`
close over each element at the top level.

`ctbrowser/unittests/unit/page_scripts` compiles the real example pages and asserts what each
one does; `ctbrowser/unittests/js/vm_*.cpp` - six files, split by topic - have a
test per language feature.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="what-p5js-needed-2026-07-29"></a>
- [WHAT p5.js NEEDED (2026-07-29)](script/01-what-p5js-needed-2026-07-29.md#what-p5js-needed-2026-07-29)
<a id="two-silent-wrong-answers-found-chasing-loadimage-2026-07-29"></a>
- [TWO SILENT WRONG ANSWERS, FOUND CHASING loadImage (2026-07-29)](script/01-what-p5js-needed-2026-07-29.md#two-silent-wrong-answers-found-chasing-loadimage-2026-07-29)
<a id="a-value-captured-by-a-native-lambda-is-not-a-gc-root"></a>
- [A `value` captured by a native lambda is not a GC root](script/01-what-p5js-needed-2026-07-29.md#a-value-captured-by-a-native-lambda-is-not-a-gc-root)
<a id="a-destructured-name-is-a-local-not-a-temporary"></a>
- [A destructured name is a local, not a temporary](script/01-what-p5js-needed-2026-07-29.md#a-destructured-name-is-a-local-not-a-temporary)
<a id="escapes-that-carry-a-code-point"></a>
- [Escapes that carry a code point](script/01-what-p5js-needed-2026-07-29.md#escapes-that-carry-a-code-point)
<a id="iteration-optional-calls-and-every-colour-string-2026-07-29"></a>
- [ITERATION, OPTIONAL CALLS, AND EVERY COLOUR STRING (2026-07-29)](script/01-what-p5js-needed-2026-07-29.md#iteration-optional-calls-and-every-colour-string-2026-07-29)
<a id="an-optional-call-is-still-a-method-call"></a>
- [An optional call is still a method call](script/01-what-p5js-needed-2026-07-29.md#an-optional-call-is-still-a-method-call)
<a id="nothing-could-iterate-a-map-or-a-set"></a>
- [Nothing could iterate a Map or a Set](script/01-what-p5js-needed-2026-07-29.md#nothing-could-iterate-a-map-or-a-set)
<a id="reading-a-property-of-undefined-throws-since-2026-09-12"></a>
- [Reading a property of undefined throws (since 2026-09-12)](script/01-what-p5js-needed-2026-07-29.md#reading-a-property-of-undefined-throws-since-2026-09-12)
<a id="every-await-in-a-function-is-a-job-since-2026-09-12"></a>
- [Every `await` in a function is a job (since 2026-09-12)](script/01-what-p5js-needed-2026-07-29.md#every-await-in-a-function-is-a-job-since-2026-09-12)
<a id="private-names-are-keys-per-class-and-a-read-is-a-brand-check-since-2026-09-12"></a>
- [Private names are keys per class, and a read is a brand check (since 2026-09-12)](script/01-what-p5js-needed-2026-07-29.md#private-names-are-keys-per-class-and-a-read-is-a-brand-check-since-2026-09-12)
<a id="strict-mode-the-part-that-changes-what-runs-since-2026-09-12"></a>
- [Strict mode, the part that changes what runs (since 2026-09-12)](script/01-what-p5js-needed-2026-07-29.md#strict-mode-the-part-that-changes-what-runs-since-2026-09-12)
<a id="the-language-forms-that-landed-with-testlanguage-2026-09-12"></a>
- [The language forms that landed with test/language (2026-09-12)](script/01-what-p5js-needed-2026-07-29.md#the-language-forms-that-landed-with-testlanguage-2026-09-12)
<a id="session-12-2026-09-16-what-moved-after-the-round-one-merges"></a>
- [Session 12 (2026-09-16): what moved after the round-one merges](script/01-what-p5js-needed-2026-07-29.md#session-12-2026-09-16-what-moved-after-the-round-one-merges)
<a id="reading-an-unresolvable-name-throws-since-2026-09-12"></a>
- [Reading an unresolvable name throws (since 2026-09-12)](script/01-what-p5js-needed-2026-07-29.md#reading-an-unresolvable-name-throws-since-2026-09-12)
<a id="the-front-end-costs-more-than-the-vm-2026-07-31"></a>
- [THE FRONT END COSTS MORE THAN THE VM (2026-07-31)](script/01-what-p5js-needed-2026-07-29.md#the-front-end-costs-more-than-the-vm-2026-07-31)
<a id="can-the-script-engine-use-threads-yes---the-front-end-not-the-runtime-2026-07-31"></a>
- [CAN THE SCRIPT ENGINE USE THREADS? Yes - the front end, not the runtime (2026-07-31)](script/01-what-p5js-needed-2026-07-29.md#can-the-script-engine-use-threads-yes---the-front-end-not-the-runtime-2026-07-31)
<a id="collect_captured_names-two-attempts-both-measured-both-reverted-2026-07-31"></a>
- [collect_captured_names: TWO attempts, both measured, both reverted (2026-07-31)](script/01-what-p5js-needed-2026-07-29.md#collect_captured_names-two-attempts-both-measured-both-reverted-2026-07-31)
<a id="webgl-works-2026-07-31-and-getting-there-was-four-wrong-answers"></a>
- [WEBGL WORKS (2026-07-31), and getting there was four wrong answers](script/01-what-p5js-needed-2026-07-29.md#webgl-works-2026-07-31-and-getting-there-was-four-wrong-answers)
<a id="what-phaser-4-needed-2026-08-01"></a>
- [WHAT PHASER 4 NEEDED (2026-08-01)](script/01-what-p5js-needed-2026-07-29.md#what-phaser-4-needed-2026-08-01)
<a id="x-was-a-register-copy-not-a-conversion"></a>
- [`+x` was a register copy, not a conversion](script/01-what-p5js-needed-2026-07-29.md#x-was-a-register-copy-not-a-conversion)
<a id="alength--n-was-silently-dropped"></a>
- [`a.length = n` was silently dropped](script/01-what-p5js-needed-2026-07-29.md#alength--n-was-silently-dropped)
<a id="windowhasownproperty-was-undefined"></a>
- [`window.hasOwnProperty` was undefined](script/01-what-p5js-needed-2026-07-29.md#windowhasownproperty-was-undefined)
<a id="the-diagnosis-itself-was-the-bottleneck"></a>
- [The diagnosis itself was the bottleneck](script/01-what-p5js-needed-2026-07-29.md#the-diagnosis-itself-was-the-bottleneck)
<a id="and-a-primitive-could-not-box"></a>
- [And a primitive could not box](script/01-what-p5js-needed-2026-07-29.md#and-a-primitive-could-not-box)
<a id="generators-and-what-babylonjs-actually-needed-2026-08-02"></a>
- [GENERATORS, AND WHAT BABYLON.JS ACTUALLY NEEDED (2026-08-02)](script/01-what-p5js-needed-2026-07-29.md#generators-and-what-babylonjs-actually-needed-2026-08-02)
<a id="it-is-the-same-suspension-await-already-had"></a>
- [It is the SAME suspension `await` already had](script/01-what-p5js-needed-2026-07-29.md#it-is-the-same-suspension-await-already-had)
<a id="three-places-had-to-learn-it-and-two-were-found-only-by-the-blocker-moving"></a>
- [THREE places had to learn it, and two were found only by the blocker moving](script/01-what-p5js-needed-2026-07-29.md#three-places-had-to-learn-it-and-two-were-found-only-by-the-blocker-moving)
<a id="what-is-not-implemented-by-name"></a>
- [What is NOT implemented, by name](script/01-what-p5js-needed-2026-07-29.md#what-is-not-implemented-by-name)
<a id="the-two-instruments-and-why-both"></a>
- [THE TWO INSTRUMENTS, AND WHY BOTH](script/01-what-p5js-needed-2026-07-29.md#the-two-instruments-and-why-both)
<a id="numbers-print-as-the-specification-says-since-2026-08-08"></a>
- [Numbers print as the specification says, since 2026-08-08](script/01-what-p5js-needed-2026-07-29.md#numbers-print-as-the-specification-says-since-2026-08-08)
<a id="math-differentially-tested-against-v8-2026-08-08"></a>
- [Math, differentially tested against V8 (2026-08-08)](script/01-what-p5js-needed-2026-07-29.md#math-differentially-tested-against-v8-2026-08-08)
<a id="what-the-test-deliberately-does-not-assert"></a>
- [What the test deliberately does NOT assert](script/01-what-p5js-needed-2026-07-29.md#what-the-test-deliberately-does-not-assert)
<a id="known-and-not-fixed-here"></a>
- [Known and NOT fixed here](script/01-what-p5js-needed-2026-07-29.md#known-and-not-fixed-here)
<a id="strings-differentially-tested-against-v8-2026-08-09"></a>
- [Strings, differentially tested against V8 (2026-08-09)](script/01-what-p5js-needed-2026-07-29.md#strings-differentially-tested-against-v8-2026-08-09)
<a id="a--b-was-false"></a>
- [`"a" < "b"` was `false`](script/02-a--b-was-false.md#a--b-was-false)
<a id="satundefined-hung-the-engine"></a>
- [`s.at(undefined)` hung the engine](script/02-a--b-was-false.md#satundefined-hung-the-engine)
<a id="the-rest"></a>
- [The rest](script/02-a--b-was-false.md#the-rest)
<a id="known-and-not-fixed"></a>
- [Known and NOT fixed](script/02-a--b-was-false.md#known-and-not-fixed)
<a id="the-utf-16-gap"></a>
- [The UTF-16 gap](script/02-a--b-was-false.md#the-utf-16-gap)
<a id="the-bugs-those-suites-found-fixed-2026-08-09"></a>
- [The bugs those suites found, fixed (2026-08-09)](script/02-a--b-was-false.md#the-bugs-those-suites-found-fixed-2026-08-09)
<a id="for-let-i---now-binds-per-iteration"></a>
- [`for (let i = ...)` now binds per iteration](script/02-a--b-was-false.md#for-let-i---now-binds-per-iteration)
<a id="tonumber-of-an-object-goes-through-toprimitive"></a>
- [ToNumber of an object goes through ToPrimitive](script/02-a--b-was-false.md#tonumber-of-an-object-goes-through-toprimitive)
<a id="the-rest-1"></a>
- [The rest](script/02-a--b-was-false.md#the-rest-1)
<a id="still-known-and-not-fixed"></a>
- [Still known and not fixed](script/02-a--b-was-false.md#still-known-and-not-fixed)
<a id="what-the-per-type-suites-found-fixed-2026-08-09"></a>
- [What the per-type suites found, fixed (2026-08-09)](script/02-a--b-was-false.md#what-the-per-type-suites-found-fixed-2026-08-09)
<a id="still-known-and-why"></a>
- [Still known, and why](script/02-a--b-was-false.md#still-known-and-why)
<a id="bigint-2026-08-09"></a>
- [BigInt (2026-08-09)](script/02-a--b-was-false.md#bigint-2026-08-09)
<a id="the-representation"></a>
- [The representation](script/02-a--b-was-false.md#the-representation)
<a id="the-gmp-backend-and-why-it-is-off-2026-08-09-the-option-itself-was-removed-2026-09-10"></a>
- [The GMP backend, and why it is off (2026-08-09; the option itself was removed 2026-09-10)](script/02-a--b-was-false.md#the-gmp-backend-and-why-it-is-off-2026-08-09-the-option-itself-was-removed-2026-09-10)
<a id="what-it-took"></a>
- [What it took](script/02-a--b-was-false.md#what-it-took)
<a id="the-refusals-are-the-feature"></a>
- [The refusals are the feature](script/02-a--b-was-false.md#the-refusals-are-the-feature)
<a id="one-trap-this-uncovered"></a>
- [One trap this uncovered](script/02-a--b-was-false.md#one-trap-this-uncovered)
<a id="typed-arrays-arraybuffer-and-dataview-2026-09-12"></a>
- [Typed arrays, ArrayBuffer and DataView (2026-09-12)](script/02-a--b-was-false.md#typed-arrays-arraybuffer-and-dataview-2026-09-12)
