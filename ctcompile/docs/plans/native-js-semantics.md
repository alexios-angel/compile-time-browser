# JavaScript semantic edge cases in native C++

**Status: selected primitive coverage; broader work planned, 2026-09-20.** This extends
[native JavaScript types](native-js-types.md). The purpose-built types must
preserve JavaScript's observable behavior, including its surprising cases.
Readable C++ is the interface; JavaScript semantics remain the contract.

Use [wtfjs](https://github.com/denysdovhan/wtfjs/blob/master/README.md) as a
regression-case inventory. The reviewed source is
[README at 2c00c4a5759c3d8a37b1237619ea664c16ce90ee](https://github.com/denysdovhan/wtfjs/blob/2c00c4a5759c3d8a37b1237619ea664c16ce90ee/README.md).
It includes historical and host-specific examples. Its comments are not a
normative oracle: check the applicable ECMAScript/HTML rules and current engines.
For example, its pre-ES5 `parseInt("08")` alternative is historical; its timer
examples must distinguish browser and Node behavior.

No full wtfjs corpus has been imported or executed. The adapted primitive
`baNaNa` witness is now a named passing source check in
`CTNative/Lowering/Scalars/string-coercions.test`: 18 distinct Node/VM/native
observations, eight native executions, four refusal controls, optional String
and generic-addition admission controls and one distinguishing mutation. The follow-up
`string-arithmetic.test` passes 26 further observations across eight native modes,
three refusal controls, an optional String admission control and one mutation.
`string-union-coercions.test` adds 51 optional/Boolean-String observations across
those eight modes, two refusal controls, two generic-addition admission controls
and two mutations. `generic-addition.test` checks the closed String/Number result
through calls, joins, loops, coercions and observations, with explicit refusals for
unsupported hooks and unions; its optional-global body is now a native control.
`optional-number-string.test` adds 53 observations of all four alternatives,
early global reads, saved copies, explicit returns and coercions across eight
native modes, with four refusals and two mutations. Existing
focused Number/Boolean/String, scalar, ownership and browser
checks validate particular building blocks; they do not establish support for
every specimen below.

## Where the behavior belongs

Primitive classes own value operations and exact conversions. The compiler owns
evaluation order, reference capture, short-circuiting, overload selection and
proof of intrinsic identity. Proved object hooks call concrete generated methods.
Browser behavior calls public ctbrowser subsystems through the existing host
contract. A class name or C++ overload does not establish any of those proofs.

Retain separate operations for `ToPrimitive`, `ToBoolean`, `ToNumber`,
`ToNumeric`, `ToString`, `ToPropertyKey`, strict equality, coercive equality,
SameValue and SameValueZero. Implement each when an admitted operation needs it,
at an existing shared seam. Do not build a universal value box, runtime property
dispatcher, VM or collector to make a case compile. Unknown behavior stays a
specific compile-time diagnostic until its proof and implementation exist.

## Coverage and remaining work

The table distinguishes implemented building blocks from remaining work. Only
the named primitive String witness above is claimed as a measured wtfjs example.

| Family and representative witness | Existing basis | Additional work before claiming coverage |
|---|---|---|
| Binary64: `0.1 + 0.2 !== 0.3`, `NaN !== NaN`, rounding near 2^53, signed zero | `js_num` retains binary64, NaN, infinities, signed zero and partial ordering; arithmetic and conversions carry the class. | Add source witnesses for rounding, subnormals, infinities, remainder and exponentiation edge cases; preserve results through folding, specialization and code generation. Add `ToInt32`/`ToUint32`, modulo-2^32 bitwise operations and masked shift counts; raw out-of-range C++ integer casts are not equivalent. Never use integer arithmetic merely because inputs are integral. |
| Equality: `Object.is(NaN, NaN)`, `Object.is(-0, 0)`, `-0 === 0` | Strict scalar and closed primitive-union equality preserve type, NaN, zero and absence; loose equality compares two Strings textually and converts only mixed non-nullish primitives. Map SameValueZero remains separate. | Prove and emit `Object.is` through SameValue. Keep these relations distinct in classes, closed unions and containers. A C++ default comparison cannot supply them all. |
| Boolean/absence: `null == 0` versus `null >= 0`, `3 > 2 > 1`, `Number()` versus `Number(undefined)` | Boolean/nullable numeric conversion, contextual truthiness and Number arithmetic exist. | Test the different equality/relational conversion paths. Preserve omitted-argument count separately from an explicit undefined argument; the Number constructor's no-argument case is a distinct source proof. Keep null, undefined, false and present NaN distinguishable. |
| Primitive String coercion: `"b" + "a" + +"a" + "a"`, numeric/string `+`, `parseInt(1e-7)` | `js_string` owns values and supplies `.to_number()` for unary and binary numeric arithmetic. String `+` with Number, Boolean, finite nullable scalars or a closed Boolean/String temporary retains JavaScript text. Optional Strings and Boolean/String temporaries also support numeric arithmetic; generic primitive `+` selects concatenation or Number addition and retains a closed String/Number result. Optional Number/String and Boolean/String transport preserves null and undefined at early reads and later joins. Typed Boolean/String values also retain their tags through signatures and globals. Public Core supplies parsing/formatting; named fixtures pass runtime and optimized paths. | Object hooks and ordering need separate proofs. Distinguish Number conversion from prefix/radix parsing, coercive `isNaN` from `Number.isNaN`, and `toFixed`/precision formatting from locale-dependent C++ streams. Preserve UTF-16 length/index/comparison semantics where required, including lone surrogates; separately resolve existing byte-oriented oracle differences. |
| Object coercion: `[] == ![]`, `[] + []`, `({valueOf(){ return 1; }}) + 1`, throwing `Symbol.toPrimitive` | Closed object identities and some proved method calls exist. | Prove `Symbol.toPrimitive` lookup/invocation, hint and primitive result; otherwise preserve the required `valueOf`/`toString` order, receiver, observable calls and exceptions. Array-to-string and Date's special default hint need their own admitted paths. Do not route loose equality through truthiness or erase conversion side effects. |
| Arrays and property keys: sparse trailing commas, `[10, 1, 3].sort()`, objects/arrays used as property names | Dense vectors, snapshots, Map storage and exact field proofs exist. | Add `js_array_t<T>`/`js_vector<T>` interfaces with identity, holes versus explicit undefined, inherited indexed reads and length mutation rules. Default sort compares converted strings by UTF-16 code units, with stable ordering and specified treatment of undefined/holes. Prove callbacks, species/spreadability and iteration order. Use `ToPropertyKey`, preserving Symbols; an object property dictionary does not have Map key semantics. |
| Primitive versus boxed object: `"str" instanceof String`, `new String("str")`, constructor/prototype mutation | Typed primitive values and `Element.prototype` composition exist. | Represent admitted boxed primitives as distinct object identities. Prove prototype chains and constructor identity. Implement the scheduled `instanceof` wrapper: invoke proved `Symbol.hasInstance` first; use `std::holds_alternative<T>` only for an equivalent default test. Preserve inheritance, invalid operands, effects and thrown exceptions. |
| Calls and references: chained `.call`, arrows, `arguments`, `foo.x = foo = {n: 2}`, getters | Direct calls, captured environments, method objects and typed exceptions exist for admitted shapes. | Preserve the reference captured for the left-hand side before evaluating the right-hand side; evaluate receiver, lookup and arguments in source order. Distinguish callable from constructible, lexical arrow `this`/`arguments`, bound calls and constructor return rules. Getters, proxies, abrupt completion and `finally` cannot be implemented by rearranging C++ expressions. |
| Syntax and scope: ASI after `return`, labels, script HTML comments, redeclarations, tagged templates | The existing parser/importer and structured lowering own syntax and scope. | Keep source-form fixtures through parsing/import, with strict/sloppy and script/module context. Fix parser/importer or lowering defects at their owning layer. Value wrappers cannot repair a different parse, temporal dead zone, binding, or statement completion. |
| Host and asynchronous behavior: `document.all`, timer coercion, Promise/thenable resolution | Browser calls use public subsystems; ordinary document/element views remain planned. | `document.all` requires its HTML legacy exotic behavior (`[[IsHTMLDDA]]`) for `typeof`, truthiness and loose equality, while retaining object identity. Never tag an ordinary document/element as undefined. Timers need host-specific coercion/scheduling and captured-owner lifetime; Promise jobs need thenable lookup, resolution and job ordering. String-evaluating callbacks or `Function` construction need a wholly proved compile-time body or refusal, never a hidden interpreter. |

Other required cases extend beyond this README: Number/BigInt mixed arithmetic
errors, BigInt zero division and shifts, Symbol identity/registry and forbidden
coercions, `Symbol.toStringTag`, iterator closing on abrupt completion, and
prototype mutations invalidating earlier assumptions. BigInt still needs a public
non-Script implementation. Add these with their corresponding type/prototype
milestones rather than treating the external inventory as exhaustive.

## Shared Core conversion boundaries

Native String conversion and the VM call the same public Core parser/formatter.
The formatter's out-of-range integer fast-path cast was fixed in **d1f98f2a**:
its range guard now runs first. Focused Core/VM regressions retain their answers;
a sanitizer rejects the former `1e20` cast and accepts the corrected extremes.
Existing bounded Number-key proofs stay bounded.

Code review still identifies parser gaps for repeated signs, radix values beyond
uint64, overflowing/underflowing decimals followed by garbage, and exponents too
large for the internal integer parser. These were not remeasured in this batch;
they are shared Core/VM issues to fix and compare separately, not special cases
to duplicate in the native class. The named passing String observations exclude these gaps.
General String UTF-16 length/index/casing alignment also remains separate.

## Implementation order and acceptance

1. **Number witnesses and compatibility cleanup.** Keep `js_num` values throughout
   calls/fields/captures, with explicit raw storage adapters. Add a small selected
   set for equality, chained comparisons, absence and numeric edge cases. Finish
   migration of the raw global `js_num = double` compatibility spelling separately.
2. **String and primitive coercions.** String literals/calls and the first
   operation group now use the class. General length remains the current byte
   count; admitted DOM UTF-16 operations retain their existing separate behavior.
   The first primitive-only witness, `baNaNa`, now passes the named source gate.
   Exact/optional String and local Boolean/String numeric arithmetic, plus
   concatenation with an exact String, now pass their source fixtures. Generic
   primitive `+` preserves its numeric-versus-concatenating choice in a closed
   String/Number result. Its optional form now preserves null and undefined
   through source global reads, calls and joins. Closed Boolean/String signature
   and global transport now uses typed alternatives and preserves absence tags.
   Strict/loose equality now visits those finite alternatives, preserving type
   identity, String/String comparison, null/undefined, NaN and signed zero.
   `primitive-equality.test` executes all three former equality refusals unchanged,
   with Node, VM and native checks. Next handle finite-union shared-cell writes
   and captures, using `shared-mixed.js` in `Scalars/strings.mlir` as the retained
   source witness. Object hooks, String ordering and general Unicode alignment
   remain separate.
3. **Array/object/prototype semantics.** Add concrete wrappers and proved
   `ToPrimitive`/property-key paths, then boxed primitives and `Symbol.hasInstance`.
   Pair every admitted hook with replaced/unknown/throwing-hook controls.
4. **Frontend and host cases.** Track syntax/scope separately. Add document legacy
   behavior, scheduling, Promise and dynamic-source boundaries only with their
   owning subsystem contracts and lifetime proofs.

Use the existing focused lit/differential harnesses; no separate runner or new
dependency is needed initially. Each selected fixture records its source revision,
execution context, expected value **and type**, relevant zero sign/NaN/absence,
exception and side-effect trace. Give it an explicit state: **native with a named
passing check**, **expected refusal with a named diagnostic check**, **oracle
disagreement under investigation**, or **not yet tested**. Preserve specimens and
record adaptations such as replacing console rendering with observable globals.

Compare admitted core-language cases with Node and the current ctbrowser oracle,
then compile/run generated C++ under the existing GCC/Clang and printing checks.
Use actual browser context for browser-only cases; Node cannot validate
`document.all`. Keep identity/side-effect observations because console inspection
is not an evaluation trace. Check folded and non-folded paths where the case
depends on runtime coercion; test forged type/identity facts and missing hooks as
negative controls. If the oracle disagrees, read the synchronization journal and
resolve the discrepancy separately; never change it merely to match native output.

Run only the selected changed cases under the focused validation policy. A future
complete wtfjs replay is a broad corpus run and requires an explicit user request.
No projected specimen count is a pass count.

## Semantic references

- [ECMAScript abstract operations](https://tc39.es/ecma262/#sec-abstract-operations),
  especially conversion, equality and property-key operations.
- [ECMAScript Number type](https://tc39.es/ecma262/#sec-ecmascript-language-types-number-type)
  and [addition](https://tc39.es/ecma262/#sec-addition-operator-plus).
- [ECMAScript ordinary `instanceof`](https://tc39.es/ecma262/#sec-ordinaryhasinstance)
  and [`Symbol.hasInstance`](https://tc39.es/ecma262/#sec-function.prototype-%symbol.hasinstance%).
- [HTML `document.all`](https://html.spec.whatwg.org/multipage/obsolete.html#dom-document-all)
  and [timers](https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#timers).

Pin the applicable specification revision and engine versions when a fixture is
added; README output alone is insufficient evidence for an engine-specific case.
