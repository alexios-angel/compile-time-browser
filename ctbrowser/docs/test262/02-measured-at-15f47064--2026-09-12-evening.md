[Back to test262.md](../test262.md)

## Measured at `15f47064` — 2026-09-12, evening

Same instrument, same corpus, engine at `15f47064` on `ctbrowser-wpt`
(browser gate 540/540 at that commit; `tools/check/test262-baseline.sh` on the
devbox, 4 workers, 10 s timeout, 2 GB cap):

| area | tests | pass at `d27d8f36` | pass at `15f47064` | delta | fail | crash / host |
|---|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 12,624 | **17,226** | +4,602 | 6,446 | 26 / 6 |
| `built-ins/Array` | 3,082 | 2,167 | **2,624** | +457 | 426 | 4 / 11 |
| `built-ins/Object` | 3,411 | 2,519 | **3,128** | +609 | 281 | 0 / 0 |
| `built-ins/Number` | 340 | 261 | **273** | +12 | 66 | 0 / 0 |
| `built-ins/Math` | 327 | 275 | **279** | +4 | 48 | 0 / 0 |
| `built-ins/String` | 1,223 | 893 | **975** | +82 | 245 | 0 / 0 |
| `built-ins/Boolean` | 51 | 28 | **42** | +14 | 8 | 0 / 0 |
| `built-ins/Function` | 509 | 291 | **331** | +40 | 165 | 0 / 0 |
| `built-ins/Error` | 93 | 31 | **72** | +41 | 16 | 0 / 0 |
| `built-ins/JSON` | 165 | 101 | **101** | +0 | 62 | 0 / 0 |
| **total** | **32,927** | **19,190** | **25,051** | **+5,861** | | |

**25,051 of 32,927 (76.1%), from 58.3% at `d27d8f36`.** Not one PASS became
a FAIL. `test/language` +4,602 is the compiler and VM work of the evening
(the list below) plus agent B's built-ins round one; the built-ins columns
are agent B (`d27d8f36..502c691f`: ArraySpeciesCreate, IsConstructor, the
`Object` descriptors) — agent G's round two (+650 measured on its own
branch: Date, encode/decodeURI, `@@toPrimitive`, JSON.rawJSON) merged AFTER
this run and is in the next row.

**The 30 crashes are all diagnosed and fixed after this run**, which is why
they are named rather than left as a number: 13 `dynamic-import` files are
`import('')` resolving to the test's directory, which ct262's loader opened
and then aborted on (`9dd67566`); 13 `for-of`/`derived-class-return-override`
files are a GC hole — `iterable_values` drained a page's `[Symbol.iterator]`
into an array reachable from nothing while `next()` ran, and a collection
under the call freed it (`e3344344`); the 4 `slice` files push 2^32 elements
until the cap kills the process (`e5772310`, then agent G's ArraySpeciesCreate
which throws the RangeError first).

**What moved, by name** (`d27d8f36..15f47064`, `ctbrowser-wpt`):

- **an unresolvable name is the ReferenceError** — `get_global` throws; the
  `typeof x` case is silenced by a run-loop peek at the following opcode, and
  the AOT bridge grew `ct_aot_global_get_soft` for the same case.
- **strict mode, the runtime half**: a rejected write throws in strict code,
  an assignment to an undeclared name throws (the probe runs before the RHS;
  the three `toFixed` files that want RHS-then-ReferenceError are the cost).
- **the early errors of strict code**: `eval`/`arguments` as bindings, the
  reserved words, duplicate simple parameters, `delete x`, legacy octal.
- **every `await` in a function takes a job**; a script's top level keeps the
  synchronous read and drains the queue for a pending promise.
- **`yield*`** (delegation through `__ctbrowser_delegate_*`, `.throw`/`.return`
  forwarded), **generator `.return()` running `finally`** (the `{@#return}`
  marker), the eager generator prologue.
- **NamedEvaluation** (`function_proto::inferred_name`, image format 5), the
  `name` of every anonymous function and class expression.
- **array destructuring through the iterator protocol** (`__ctbrowser_iter_*`,
  IteratorClose on the normal exit), empty and rest-first object patterns
  throw on `null`/`undefined`, computed accessors, computed class methods.
- **array literal elisions are holes**, `Object.keys([,1])` is `["1"]`.
- **`var` hoists across the whole script** (`program::hoisted_vars`), for-in
  walks the prototype chain, `iterable_values` honours `[Symbol.iterator]`.
- **private names are one name** (`@#x`), never a property key.

**The next clearest failures**, from the causes at this commit: 1,527
"negative parse expected" files (early errors still missing: `arguments`
in field initialisers, `await`/`yield` as names, `import()` arity — the
next commits take the first three), 482 parse errors at `;` (the
`for ([a, b] of xs)` and `catch ({x})` heads, 600 files, taken next), 350
`with`-statement files, ~200 `using` declarations, the private brand checks
(~150, taken next), `import.defer` (~150), TCO (30).

## Measured at `00b5ab38` — 2026-09-12, night

Same instrument, same corpus, engine at `00b5ab38` on `ctbrowser-wpt`
(browser gate 540/541 at that commit — the one red is `ctcompile_lit`, repaired
by Codex's `f57cab15`; `tools/check/test262-baseline.sh` on the devbox, 4
workers, 10 s timeout, 2 GB cap):

| area | tests | pass at `15f47064` | pass at `00b5ab38` | delta | fail | crash / host |
|---|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 17,226 | **18,350** | +1,124 | 5,348 | 0 / 6 |
| `built-ins/Array` | 3,082 | 2,624 | **2,774** | +150 | 280 | 0 / 11 |
| `built-ins/Object` | 3,411 | 3,128 | **3,275** | +147 | 134 | 0 / 0 |
| `built-ins/Number` | 340 | 273 | **333** | +60 | 6 | 0 / 0 |
| `built-ins/Math` | 327 | 279 | **326** | +47 | 1 | 0 / 0 |
| `built-ins/String` | 1,223 | 975 | **1,096** | +121 | 124 | 0 / 0 |
| `built-ins/Boolean` | 51 | 42 | **49** | +7 | 1 | 0 / 0 |
| `built-ins/Function` | 509 | 331 | **410** | +79 | 86 | 0 / 0 |
| `built-ins/Error` | 93 | 72 | **83** | +11 | 5 | 0 / 0 |
| `built-ins/JSON` | 165 | 101 | **137** | +36 | 26 | 0 / 0 |
| **total** | **32,927** | **25,051** | **26,833** | **+1,782** | | |

**26,833 of 32,927 (81.5%), from 76.1% at `15f47064`.** Not one crash left:
the 30 of the previous row are the fixes named there. The built-ins columns
are agent G's round two (Date, `encodeURI`/`decodeURI`, `@@toPrimitive`,
`JSON.rawJSON`, `Number`/`Math` edge cases), merged after the previous
measurement; `test/language` +1,124 is `15f47064..00b5ab38`: the `for ([a, b]
of xs)` and `catch ({x})` heads (`99879d88`, the 482 "parse error at `;`"
files), the early errors of `516e7522` and `00b5ab38`, the private-name brand
check (`308b8172`), `import()` arity (`11018eea`), `yield` as a name outside a
generator (`6559e566`).

**53 files went PASS -> FAIL**, two causes, both named so that they are not
read as noise:

- **12 early errors the new destructuring heads skip** —
  `statements/for-{in,of}/dstr/{array-elem-target-simple-strict,
  array-rest-before-elision, obj-id-init-simple-strict, obj-id-simple-strict,
  obj-rest-before-comma-invalid}.js`, the `for-await-of` twin and
  `statements/try/early-catch-duplicates.js`: the pattern checks (strict
  `eval`/`arguments` targets, a rest element before an elision, duplicate
  catch bindings) ran on declarations and not on a `for` head or a catch
  parameter. The compiler's, and next.
- **~40 bitwise and shift files** (`expressions/bitwise-*`, `left-shift`,
  `right-shift`, `unsigned-right-shift`, `compound-assignment/S11.13.2_A4.*`,
  `prefix-increment/S11.4.4_A4_T3`): `new String("1") & "1"` is 1 in the
  specification and is 0 here since `new String()` became a real wrapper
  object (agent B, `d27d8f36..502c691f`). `binary_op_static` converts with the
  STATIC `to_int32`, which never runs `valueOf` — a documented deviation
  carried in `include/ctbrowser/aot/aot_helpers.def` (the non-re-entering
  family, `may_reenter 0`) as a contract with ctcompile's native backend. The
  fix is to move the six bitwise opcodes (and `add`) to the re-entering
  family, which is an ABI change made together with Codex and is proposed in
  the journal, not made here.

**The next clearest failures** at this commit (test/language, by cause):
1,326 "negative parse expected" (`identifiers` 114, `literals/regexp` 179,
class elements ~180, `module-code` 111), 350 `with` files, 139
`eval-code/direct` "Expected a SyntaxError", the `dynamic-import` parse error
at `import.` (154), `using`/`await-using` (~130). Built-ins: `Array` 280
(60 are the BigInt typed arrays being absent, 41 "Expected a TypeError"),
`Object` 134, `String` 124, `Function` 86.

## Measured at `b346dc0b` — 2026-09-13, the WHOLE corpus for the first time

`tools/check/test262-baseline.sh` widened on 2026-09-12 from ten hand-picked
areas to `test/language`, `test/built-ins` and `test/annexB` - every file the
corpus holds for the language and its library, 48,624 of them against 32,927
before - because the old list scored the areas that were being worked on and
made the rest invisible: RegExp at 24%, Promise at 34% and TypedArray at 0.1%
were never a number in this file. Same instrument otherwise (devbox, 4 workers,
10 s timeout, 2 GB cap), engine at `b346dc0b` on `ctbrowser-wpt` (the merge of
`ctcompile-v1` `3e803401` into the audit cuts of the evening). Areas with
fewer than 93 files are in the JSON and not in the table:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `language` | 23,726 | 18350 | **19,829** | +1479 | 3,869 | 0/1/6 | 21 |
| `annexB` | 1,086 | - | **369** | - | 675 | 0/0/0 | 42 |
| `built-ins/Temporal` | 4,603 | - | **0** | - | 4,603 | 0/0/0 | 0 |
| `built-ins/Object` | 3,411 | 3275 | **3,284** | +9 | 125 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2774 | **2,774** | +0 | 280 | 0/1/11 | 16 |
| `built-ins/RegExp` | 1,879 | - | **938** | - | 924 | 3/0/2 | 12 |
| `built-ins/TypedArray` | 1,446 | - | **1** | - | 114 | 0/0/1192 | 139 |
| `built-ins/String` | 1,223 | 1096 | **1,149** | +53 | 71 | 0/0/0 | 3 |
| `built-ins/TypedArrayConstructors` | 738 | - | **74** | - | 39 | 0/0/495 | 130 |
| `built-ins/Promise` | 732 | - | **257** | - | 474 | 0/0/0 | 1 |
| `built-ins/Iterator` | 654 | - | **13** | - | 640 | 0/0/0 | 1 |
| `built-ins/Date` | 594 | - | **580** | - | 11 | 0/0/0 | 3 |
| `built-ins/DataView` | 561 | - | **0** | - | 442 | 0/0/0 | 119 |
| `built-ins/Function` | 509 | 410 | **432** | +22 | 64 | 0/0/0 | 13 |
| `built-ins/Atomics` | 389 | - | **0** | - | 0 | 0/0/0 | 389 |
| `built-ins/Set` | 383 | - | **368** | - | 14 | 0/0/0 | 1 |
| `built-ins/Number` | 340 | 333 | **333** | +0 | 6 | 0/0/0 | 1 |
| `built-ins/Math` | 327 | 326 | **327** | +1 | 0 | 0/0/0 | 0 |
| `built-ins/Proxy` | 311 | - | **146** | - | 129 | 0/0/0 | 36 |
| `built-ins/ArrayBuffer` | 221 | - | **24** | - | 161 | 3/0/5 | 28 |
| `built-ins/Map` | 204 | - | **190** | - | 13 | 0/0/0 | 1 |
| `built-ins/JSON` | 165 | 137 | **137** | +0 | 26 | 0/0/0 | 2 |
| `built-ins/Reflect` | 153 | - | **115** | - | 38 | 0/0/0 | 0 |
| `built-ins/WeakMap` | 141 | - | **135** | - | 5 | 0/0/0 | 1 |
| `built-ins/AsyncDisposableStack` | 104 | - | **0** | - | 101 | 0/0/2 | 1 |
| `built-ins/SharedArrayBuffer` | 104 | - | **0** | - | 0 | 0/0/0 | 104 |
| `built-ins/Symbol` | 98 | - | **48** | - | 31 | 0/0/0 | 19 |
| `built-ins/NativeErrors` | 94 | - | **82** | - | 6 | 0/0/0 | 6 |
| `built-ins/DisposableStack` | 93 | - | **0** | - | 91 | 0/0/1 | 1 |
| `built-ins/Error` | 93 | 83 | **83** | +0 | 5 | 0/0/0 | 5 |
| **total** | **48,624** | 26,833 | **32,295** | | 13,485 | 6/2/1718 | 1118 |

**32,295 of 48,624 (66.4%); of the 47,506 that ran, 68.0%.** The ten old
areas alone are 28,397 of 32,927 (86.2%), from 26,833 (81.5%) at `00b5ab38`.
`test/language` +1,479 is agent J's evening (`3fad5e7c`: the `with`
statement, restricted-production ASI, import attributes, hashbang - ctjs
`b2b5155`) and the class/destructuring early errors; `String` +53 and
`Function` +22 are the RegExp 22.2 work landing in `split`/`replace` and
`Function.prototype.toString`. **6 files went PASS -> FAIL** across the
same period: `comments/hashbang/multi-line-comment.js` (a refusal where the
parser must reject), the three `module-code/import-attributes/early-dup-
attribute-key-*` files (the duplicate-key early error the ctjs bump was
meant to carry - lost between the submodule and the checker), `class/
elements/privatefieldset-evaluation-order-3.js` and `class/subclass/derived-
class-return-override-for-of-arrow.js`. All six are in the language agent's
brief for the next round.

**Where the corpus says the holes are**, in files, largest first:
`Temporal` 4,603 (not implemented, not planned - it is a calendar library
the size of the rest of the standard library); the typed arrays - 1,707
files across `TypedArray`, `TypedArrayConstructors`, `Array` and `DataView`
die in `harness/testTypedArray.js` before the test runs, on
`BigInt64Array` being absent, and `DataView` is not defined (388); `Iterator`
is not defined (407, the ES2025 iterator helpers); `RegExp` 924 FAIL of
1,879; `Promise` 474 of 732; `Proxy` 129 of 311; `DisposableStack` /
`AsyncDisposableStack` / `SuppressedError` (ES2026 explicit resource
management, 201 files, none passing); `annexB` 675 of 1,044 - 192 of them
the "initialized binding is not created" family, which is Annex B.3.2's
web-compat block-level function hoisting, and 27 `escape`/`unescape`.
In `test/language` the top causes are the ones the `00b5ab38` row named,
each smaller: 544 early errors, 348 wrong `SameValue`s (classes), 193
"Expected a SyntaxError" (139 in direct eval), 154 `import.source`/
`import.defer` (parsed as a broken `import.meta`), 126 missing TypeErrors,
94 missing ReferenceErrors.

## Measured at `9c70aaa0` — 2026-09-16, the four round-one branches merged

The four agent branches of 2026-09-13 (`docs/plans/wpt-next.md` §1: T typed
arrays, P promise/proxy/iterator/disposable, W dom/html, E `test/language`)
merged onto `ctcompile-v1` `09341902` - which carries the ponytail audit of
2026-09-15/16 (Script dedups, plain DOM node payloads, the bindings helpers) -
as `c5682f32`, `20f17da4`, `247a7c53`, `9c70aaa0`, the ctjs gitlink at
`69cc3d8`. Same instrument (devbox, 4 workers, 10 s timeout, 2 GB cap,
`tools/check/test262-baseline.sh` over the whole corpus). Areas with fewer
than 93 files are in the JSON and not in the table:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `language` | 23,726 | 19829 | **21,402** | +1573 | 2,302 | 0/1/0 | 21 |
| `annexB` | 1,086 | 369 | **378** | +9 | 665 | 1/0/0 | 42 |
| `built-ins/Temporal` | 4,603 | 0 | **0** | +0 | 4,603 | 0/0/0 | 0 |
| `built-ins/Object` | 3,411 | 3284 | **3,300** | +16 | 109 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2774 | **2,800** | +26 | 265 | 0/1/0 | 16 |
| `built-ins/RegExp` | 1,879 | 938 | **939** | +1 | 925 | 3/0/0 | 12 |
| `built-ins/TypedArray` | 1,446 | 1 | **752** | +751 | 555 | 0/0/0 | 139 |
| `built-ins/String` | 1,223 | 1149 | **1,156** | +7 | 64 | 0/0/0 | 3 |
| `built-ins/TypedArrayConstructors` | 738 | 74 | **280** | +206 | 328 | 0/0/0 | 130 |
| `built-ins/Promise` | 732 | 257 | **721** | +464 | 10 | 0/0/0 | 1 |
| `built-ins/Iterator` | 654 | 13 | **608** | +595 | 45 | 0/0/0 | 1 |
| `built-ins/Date` | 594 | 580 | **583** | +3 | 8 | 0/0/0 | 3 |
| `built-ins/DataView` | 561 | 0 | **438** | +438 | 4 | 0/0/0 | 119 |
| `built-ins/Function` | 509 | 432 | **438** | +6 | 58 | 0/0/0 | 13 |
| `built-ins/Atomics` | 389 | 0 | **0** | +0 | 0 | 0/0/0 | 389 |
| `built-ins/Set` | 383 | 368 | **368** | +0 | 14 | 0/0/0 | 1 |
| `built-ins/Number` | 340 | 333 | **335** | +2 | 4 | 0/0/0 | 1 |
| `built-ins/Math` | 327 | 327 | **327** | +0 | 0 | 0/0/0 | 0 |
| `built-ins/Proxy` | 311 | 146 | **173** | +27 | 102 | 0/0/0 | 36 |
| `built-ins/ArrayBuffer` | 221 | 24 | **190** | +166 | 3 | 0/0/0 | 28 |
| `built-ins/Map` | 204 | 190 | **190** | +0 | 13 | 0/0/0 | 1 |
| `built-ins/JSON` | 165 | 137 | **137** | +0 | 26 | 0/0/0 | 2 |
| `built-ins/Reflect` | 153 | 115 | **150** | +35 | 3 | 0/0/0 | 0 |
| `built-ins/WeakMap` | 141 | 135 | **135** | +0 | 5 | 0/0/0 | 1 |
| `built-ins/AsyncDisposableStack` | 104 | 0 | **103** | +103 | 0 | 0/0/0 | 1 |
| `built-ins/SharedArrayBuffer` | 104 | 0 | **0** | +0 | 0 | 0/0/0 | 104 |
| `built-ins/Symbol` | 98 | 48 | **76** | +28 | 3 | 0/0/0 | 19 |
| `built-ins/NativeErrors` | 94 | 82 | **82** | +0 | 6 | 0/0/0 | 6 |
| `built-ins/DisposableStack` | 93 | 0 | **92** | +92 | 0 | 0/0/0 | 1 |
| `built-ins/Error` | 93 | 83 | **85** | +2 | 3 | 0/0/0 | 5 |
| **total** | **48,624** | 32,295 | **36,962** | | 10,538 | 4/2/0 | 1118 |

**36,962 of 48,624 (76.0%); of the 47,506 that ran, 77.8%** - from 32,295
(66.4%) at `b346dc0b`, **+4,717 FAIL -> PASS and 50 PASS -> FAIL**. The
agents' own numbers held through the merge: `TypedArray` 1 -> 752,
`TypedArrayConstructors` 74 -> 280, `DataView` 0 -> 438, `ArrayBuffer`
24 -> 190, `Uint8Array` 4 -> 66; `Promise` 257 -> 721 of 732, `Iterator`
13 -> 608, `DisposableStack` 0 -> 92, `AsyncDisposableStack` 0 -> 103,
`SuppressedError` 0 -> 20, `AggregateError` 0 -> 23, `Proxy` 146 -> 173,
`Reflect` 115 -> 150, `Symbol` 48 -> 76; `test/language` 19,829 -> 21,402.

**The 50 lost, each read:** 19 `annexB/language/function-code` (`block-decl-
func-*`, `if-decl-*`, `switch-case-*`: agent E made a block's function
declaration block-local per 14.2.1 and dropped the sloppy-mode var binding
B.3.3 gives it - fixed in `09798323` on top of this row, unmeasured until the
next); 11 `using`/`await using` use-before-initialization (the TDZ of a
`using` binding reads as a TypeError from the disposal register rather than
a ReferenceError; agent E's own report named these); 5 `Array/fromAsync`
(rejections that the two-tick thenable resolution of `20f17da4` now surfaces
in a different order); 5 decorator syntax files (`decorators` is a stage-3
feature test262 gates behind a flag; the parser refuses `@` on purpose now -
the runner should skip the feature); `tagged-template/tco-*` (2, tail calls);
`for-in`/`for-of` `head-var-bound-names-in-stmt` (a `var x;` inside the body
wrote undefined over the element - fixed in `09798323`); `S10.6_A5_T3`,
`namespace/internals/super-set-to-tdz-binding-with-accessor`, `AsyncFunction-
construct` (the `new AsyncFunction` body parse), `Object/prototype/toString/
proxy-revoked`, `TypedArrayConstructors/internals/Set/key-is-out-of-bounds-
receiver-is-not-object`, `annexB/language/statements` (1).

**Where the corpus says the holes are now**, in files: `Temporal` 4,603 (not
planned); `RegExp` 925 - 469 of them `property-escapes/generated` (the
Unicode property tables behind `\p{...}`, a data-generation job), 57
`unicodeSets/generated` and 28 `prototype/unicodeSets` (the `v` flag), and
175 "negative parse/SyntaxError" the regexp early-error pass does not raise;
`TypedArray` 555 + `TypedArrayConstructors` 328 + 60 in `Array/prototype` -
`BigInt64Array`, `BigUint64Array` and `Float16Array` are not defined
(`value.hpp`'s `element_kind` has no `big_i64`/`big_u64`/`f16`), which is
now the single largest lever in `built-ins`; `annexB` 665 (192 the B.3.3
family above, 27 `escape`/`unescape`, RegExp legacy statics, `__proto__`
and the `__defineGetter__` family); `language` 2,302 (decorators 300-odd
behind the feature flag, `import.source`/`import.defer`, the rest early
errors and class edge cases); `Array/prototype` 265; `Object` 109;
`Function` 58; `JSON` 26; `BigInt` 42 (`asIntN`/`asUintN` landed in
`2b9d7071` on top of this row); `parseInt` 16; `isFinite`/`isNaN` 9 each.

**And a note on the gate at this SHA.** The ctbrowser half of the build and
every ctbrowser test the run reached were green, but `tools/remote-build.sh`
stopped in ctcompile's `map_flow` pipeline fixture, which was no longer
provably native: agent E's compiler gave every object-literal method an own
`__home` property (a closure -> literal edge the escape analysis refuses).
`5865c08b` emits the link only for a method that says `super`; the next row
is the one measured behind a green gate.

## BigInt string grammar and loose equality — 2026-09-18

Focused devbox measurements recovered from the interrupted session and
replayed after the equality fix. Same pinned corpus, four workers,
10-second timeout and 2 GB address-space cap.

| area | files | PASS before / after | FAIL before / after | SKIP |
|---|---:|---:|---:|---:|
| `built-ins/BigInt` | 77 | 74 / 76 | 2 / 0 | 1 |
| `language/expressions/equals` | 47 | 44 / 47 | 3 / 0 | 0 |
| `language/expressions/does-not-equals` | 38 | 36 / 38 | 2 / 0 | 0 |

**Seven files gained, zero lost.** `be67d400` rejects numeric separators
and signed nondecimal prefixes in StringIntegerLiteral; its intermediate
BigInt replay measured 75 PASS, one FAIL and one SKIP. Source BigInt literals
retain their own grammar. The shared loose-equality operation converts
Booleans to Number and distinguishes heap-stored BigInt/Symbol primitives
from objects, so object comparisons perform ToPrimitive and preserve its
exceptions. VM opcodes and the AOT bridge call this same operation.

The browser CTest gate passed **216/216** (66.98 seconds), with compiler
tests excluded; formatting passed. Fresh final JSON files
`browser20-finalize-{BigInt,equals,does-not-equals}.json` reproduce the table.

The remaining BigInt skip requires cross-realm support. These focused runs
do not update the whole-corpus total below. Evidence:
`/tmp/ctbrowser20/` contains the before/intermediate/after JSON, failing and
passing regression logs, and `runtime-comparison.json` with the gained paths.

## BigInt measured at `96781de5` — 2026-09-18

Focused devbox replay of `test/built-ins/BigInt`: **63 -> 74 PASS**,
**13 -> 2 FAIL**, one SKIP, 77 files; **11 gained, zero lost**. The fresh
before run matched the saved `b722aa41` results. Same instrument: four
workers, 10-second timeout, 2 GB address-space cap. The two affected CTests,
`bigint_basics` and `symbol_basics`, passed.

`BigInt(value)` now converts objects with the number hint before choosing
NumberToBigInt or ToBigInt, preserving coercion exceptions. Its `toString`
method converts and checks the radix before narrowing it to an integer.
Remaining failures are `constructor-from-string-syntax-errors.js` (shared
BigInt string grammar) and `wrapper-object-ordinary-toprimitive.js` (shared
VM coercion). Native construction-state handling remains a separate gap.

Evidence: `/tmp/browser-resume-bigint-{before,after}.json` and
`/tmp/browser-resume-bigint-gate2.log`. A combined-tree replay at `b78e68cb`
confirmed 74 PASS, two FAIL and one SKIP in
`/tmp/ctbrowser-resume/final/browser-resume-final-bigint.json`. The whole
test262 corpus was not replayed for this change; its last complete
measurement remains below.

## Measured at `b722aa41` — 2026-09-18, round seven (J2: RegExp and the async tail)

**40,832 of 48,624 (84.0%; 85.4% of the 47,791 that ran)**, from 39,731 at
`9f8da347`: **+1,101 files, PASS->FAIL 0**. Round seven's agent J2: `\p{..}`
/ `\P{..}` property escapes over a generated UCD table
(`lib/Script/regex_properties.inc`, `tools/gen/unicode_properties.py`),
Canonicalize under `iu`, the `v` flag's ClassSetExpression (nested
classes, `--`, `&&`, `\q{..}`, properties of strings), `new RegExp` judged
by the literal's early-error scan, async generator `.return(v)` awaiting
`v` and running `finally`, `yield*` forwarding return/throw, `import defer
* as ns`, the test262 host resolving re-exports over the whole graph, an
object rest that leaves out computed keys. Same instrument (devbox, 4
workers, 10 s, 2 GB); the rows are every area that moved:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `built-ins/RegExp` | 1,879 | 940 | **1,822** | +882 | 45 | 0/0/0 | 12 |
| `language` | 23,726 | 22107 | **22,300** | +193 | 1,404 | 0/1/0 | 21 |
| `built-ins/AsyncFromSyncIteratorPrototype` | 38 | 18 | **30** | +12 | 8 | 0/0/0 | 0 |
| `built-ins/AsyncGeneratorPrototype` | 48 | 40 | **48** | +8 | 0 | 0/0/0 | 0 |
| `annexB` | 1,086 | 776 | **781** | +5 | 262 | 1/0/0 | 42 |
| `built-ins/String` | 1,223 | 1168 | **1,169** | +1 | 51 | 0/0/0 | 3 |
| **total** | **48,624** | 39,731 | **40,832** | +1,101 | 6,956 | 1/2/0 | 833 |

## Measured at `9f8da347` — 2026-09-17, round six (J: the VM tail)

Round six's agent J worked the VM: an `await` no longer truncates the
register stack below its caller (the "default parameter read as undefined"
bug that HARNESS_ERRORed every WPT file calling `promise_setup`), a boxed
block-level `let` whose initialiser holds a closure is a cell before the
initialiser runs, `export * as ns from` and `export` of a destructuring
declaration, structuredClone per HTML 2.7, `class A extends Array` instances
ARE arrays, typed arrays without an own `length`, %ThrowTypeError%, the
arguments object's shape, ToNumeric before the BigInt decision, proxy traps
as GetMethod with `[[Construct]]` carrying new.target and the get/set/has
invariants, an eval program's completion value, well-formed
`JSON.stringify`, `%AsyncIteratorPrototype%[Symbol.asyncDispose]`, `class
F extends Function`. Same instrument (devbox, 4 workers, 10 s, 2 GB), before
= `6edb7421` for `language`/`annexB` and `273773cd` for `built-ins` (which
were byte-identical through round five); the rows shown are those plus every
`built-ins` area that moved by five or more files:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `language` | 23,726 | 21904 | **22,107** | +203 | 1,597 | 0/1/0 | 21 |
| `annexB` | 1,086 | 776 | **776** | +0 | 267 | 1/0/0 | 42 |
| `built-ins/Object` | 3,411 | 3321 | **3,332** | +11 | 77 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2825 | **2,896** | +71 | 169 | 0/1/0 | 16 |
| `built-ins/TypedArray` | 1,446 | 1267 | **1,419** | +152 | 20 | 0/0/0 | 7 |
| `built-ins/TypedArrayConstructors` | 738 | 569 | **606** | +37 | 56 | 0/0/0 | 76 |
| `built-ins/Proxy` | 311 | 174 | **218** | +44 | 57 | 0/0/0 | 36 |
| **total** | **48,624** | 39,175 | **39,731** | +556 | 8,054 | 4/2/0 | 833 |
39,731 of 48,624 (81.7%); of the 47,791 that ran 83.1%

**39,731 of 48,624 (81.7%); of the 47,791 that ran, 83.1%** - from 39,175
(80.6%): **+556**. J's own per-directory measure on its gate-4 binary
(before its last two commits) saw +203 / -2 in `language` and +343 / -27 in
`built-ins`, the 27 lost all in `bc8b3f7f`'s proxy `[[Construct]]` and
addressed by `e80643ea`; this row, on the merged tree, is the first measure
with those in and the per-area totals above are the whole of it. `annexB`
is unchanged at 776.

**BYTECODE SHAPE** (for the native backend): a boxed block-level `let`/`const`
whose initialiser contains a function or class now emits `load_undef;
new_cell; init -> tmp; cell_set` where it was `init; new_cell`; other
declarations are unchanged. And an `await` no longer shrinks `registers_`
below the caller's `frame_size + 8`. AGENT-SYNC.jsonl carries the full JOURNAL
line; ctjs gitlink unchanged at `3cb2ef9`.

**Still the biggest holes** (`docs/plans/wpt-next.md` has the briefs):
`Temporal` 4,603; `RegExp` 924 - 469 `property-escapes/generated` (the UCD
tables behind `\p{...}`, a generated-data job, round seven's J2), 57 + 28
the `v` flag; `language` 1,597 - 173 "Expected SameValue" (class fields,
async generators), 93 negative-parse SyntaxErrors the compiler does not
raise, 91 "Expected a TypeError", 64 "Expected a ReferenceError", 60 `import
defer` (the loader takes `defer` as a default import's name), 32 "call stack
exhausted" (tail calls), the eval-code cluster (76: direct eval's scope);
`annexB` 267; `Array` 169; `Proxy` 57.

## Measured at `6edb7421` — 2026-09-16, round four (T2: language + annexB)

Round four's agent T2 worked `test/language` and `test/annexB`; the other
three agents (S, G2, U2) touch no `test262` path, so `test/built-ins` is
byte-identical to `273773cd`. Same instrument (devbox, 4 workers, 10 s, 2 GB),
before = `273773cd`; only the two areas that moved plus the total:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
| `language` | 23,726 | 21766 | **21,904** | +138 | 1,800 | 0/1/0 | 21 |
| `annexB` | 1,086 | 469 | **776** | +307 | 267 | 1/0/0 | 42 |
| **total** | **48,624** | 38,730 | **39,175** | | 8,610 | 4/2/0 | 833 |

**39,175 of 48,624 (80.6%); of the 47,791 that ran, 82.0%** - from 38,730
(79.7%) at `273773cd`: **+445 FAIL -> PASS, 0 PASS -> FAIL** (full PASS-set
diff, all three areas). `annexB` **469 -> 776** is the whole of it plus 138:
Annex B.3.3's block-level function declarations, done as the spec's two steps
(the lexical binding in the block, then the var write to the enclosing scope
as a DECLARATION's write) - which retired the 232-file "An initialized
binding is not created" cluster that was the single largest in the corpus.
`language` **21,766 -> 21,904**: a catch parameter block-scoped in a script,
`import`/`export` as ModuleItems with a module's own top-level rules, and
`export default function f(){}` binding `f`.

**BYTECODE SHAPE** (recorded for the native backend, which pins it): `5dddb500`
- a script's block-level function declaration now binds a local first, so the
sequence for `{ function f(){} }` at script top level changed. AGENT-SYNC.jsonl
carries the full JOURNAL line; ctjs gitlink unchanged at `3cb2ef9`.

**Still the biggest holes:** `Temporal` 4,603; `RegExp` ~924; `language`
1,800 (class/async SameValue clusters, module early errors, `import.source`/
`import.defer`); `annexB` 267 (the remaining B.3.2 "value is not updated"
cases, `escape`/`unescape`, the `__defineGetter__` family); `Array` 240.

## Measured at `273773cd` — 2026-09-16, rounds two and three merged

`docs/plans/wpt-next.md` §3 (round two: A animations, G properties, L
layout, C cascade, merged by session 13 as `c5c660cb`..`e29e197f`) and §5
(round three: B typed-array kinds, U URL, D parser, F forms/range, merged
by session 14 as `cb2cdcdf`), then the fixes the merged gate demanded
(`d626b2d6`..`273773cd`). Same run as the `docs/wpt.md` row of this SHA:
devbox, `tools/check/test262-baseline.sh`, 4 workers, 10 s, 2 GB. The
before column is `9c70aaa0`, the previous row; the rows shown are
`language`, `annexB` and every `built-ins` area that moved by five or more
files.

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
| `language` | 23,726 | 21402 | **21,766** | +364 | 1,938 | 0/1/0 | 21 |
| `annexB` | 1,086 | 378 | **469** | +91 | 574 | 1/0/0 | 42 |
| `built-ins/Object` | 3,411 | 3300 | **3,321** | +21 | 88 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2800 | **2,825** | +25 | 240 | 0/1/0 | 16 |
| `built-ins/TypedArray` | 1,446 | 752 | **1,267** | +515 | 172 | 0/0/0 | 7 |
| `built-ins/String` | 1,223 | 1156 | **1,168** | +12 | 52 | 0/0/0 | 3 |
| `built-ins/TypedArrayConstructors` | 738 | 280 | **569** | +289 | 93 | 0/0/0 | 76 |
| `built-ins/Iterator` | 654 | 608 | **649** | +41 | 4 | 0/0/0 | 1 |
| `built-ins/DataView` | 561 | 438 | **516** | +78 | 5 | 0/0/0 | 40 |
| `built-ins/Function` | 509 | 438 | **467** | +29 | 29 | 0/0/0 | 13 |
| `built-ins/ArrayBuffer` | 221 | 190 | **204** | +14 | 4 | 0/0/0 | 13 |
| `built-ins/Map` | 204 | 190 | **198** | +8 | 5 | 0/0/0 | 1 |
| `built-ins/JSON` | 165 | 137 | **146** | +9 | 17 | 0/0/0 | 2 |
| `built-ins/WeakMap` | 141 | 135 | **140** | +5 | 0 | 0/0/0 | 1 |
| `built-ins/BigInt` | 77 | 34 | **63** | +29 | 13 | 0/0/0 | 1 |
| `built-ins/GeneratorPrototype` | 61 | 47 | **61** | +14 | 0 | 0/0/0 | 0 |
| `built-ins/parseInt` | 55 | 39 | **55** | +16 | 0 | 0/0/0 | 0 |
| `built-ins/AsyncGeneratorPrototype` | 48 | 23 | **40** | +17 | 8 | 0/0/0 | 0 |
| `built-ins/FinalizationRegistry` | 47 | 0 | **46** | +46 | 0 | 0/0/0 | 1 |
| `built-ins/encodeURI` | 31 | 23 | **30** | +7 | 1 | 0/0/0 | 0 |
| `built-ins/encodeURIComponent` | 31 | 23 | **30** | +7 | 1 | 0/0/0 | 0 |
| `built-ins/WeakRef` | 29 | 0 | **28** | +28 | 0 | 0/0/0 | 1 |
| `built-ins/ArrayIteratorPrototype` | 27 | 15 | **22** | +7 | 5 | 0/0/0 | 0 |
| `built-ins/AsyncGeneratorFunction` | 23 | 8 | **20** | +12 | 1 | 0/0/0 | 2 |
| `built-ins/GeneratorFunction` | 23 | 8 | **19** | +11 | 2 | 0/0/0 | 2 |
| `built-ins/AsyncFunction` | 18 | 9 | **16** | +7 | 1 | 0/0/0 | 1 |
| `built-ins/isFinite` | 15 | 6 | **15** | +9 | 0 | 0/0/0 | 0 |
| `built-ins/isNaN` | 15 | 6 | **15** | +9 | 0 | 0/0/0 | 0 |
| `built-ins/MapIteratorPrototype` | 11 | 1 | **10** | +9 | 1 | 0/0/0 | 0 |
| `built-ins/SetIteratorPrototype` | 11 | 1 | **10** | +9 | 1 | 0/0/0 | 0 |
| **total** | **48,624** | 36,962 | **38,730** | | 9,055 | 4/2/0 | 833 |

**38,730 of 48,624 (79.7%); of the 47,791 that ran, 81.0%** - from 36,962
(76.0%) at `9c70aaa0`: **+1,818 FAIL -> PASS, 50 PASS -> FAIL**. Of the
gain, 1,048 is round three alone (`e29e197f` -> `273773cd`: 37,682 ->
38,730, and NOT ONE file lost between those two) - agent B's `BigInt64Array`,
`BigUint64Array` and `Float16Array` (`TypedArray` 826 -> 1,267,
`TypedArrayConstructors` 303 -> 569, `BigInt` 34 -> 63), and session 13's
VM rows: `Function` 452 -> 467 (`new (f.bind(o, 1))(2)`), `language`
21,563 -> 21,766 (the block-level static TDZ, the 19.1 refusal for a plain
assignment to NaN/Infinity/undefined, ToNumeric of an object operand for the
bitwise operators and ++/--, two lone surrogates that meet are one code
point, encodeURI's URIError).

**The 50 lost against `9c70aaa0` are all session 12/13's, read at the time:**
30 `annexB/language/function-code` "An initialized binding is not created
prior to evaluation" (Annex B.3.2's `if`/`switch`/`for` function
declarations: `09798323` made a block's function declaration block-local and
`07637be0` withheld the var binding when an enclosing block declares the
name lexically - the `*-skip-early-err-*` files want the var binding created
and left uninitialised, which the compiler does not distinguish from "not
created"); 4 negative-parse files the parser accepts; 2 `JSON/stringify`
through a revoked proxy; `S10.4.3-1-17/20-s` (`typeof this` under a direct
eval in strict code); and 12 singles. None of them moved in this session.

**Still the biggest holes, in files:** `Temporal` 4,603; `RegExp` 924 (469
`property-escapes/generated`, the `v` flag 85, ~175 early errors the regexp
pass does not raise); `language` 1,938; `annexB` 574; `Array` 240;
`TypedArray` 172 + `TypedArrayConstructors` 93 (a subclass instance is not a
typed array, `ta.buffer === ta.buffer` is false, `$262.detachArrayBuffer`
throws); `Proxy` 101; `Object` 88.
