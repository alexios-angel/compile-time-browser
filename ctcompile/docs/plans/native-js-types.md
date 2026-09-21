# Native JavaScript types and prototype interfaces

**Status: in progress, 2026-09-21.** `Element.prototype` composition is implemented
using the existing four selector method objects. Distinct `undefined_t` and
`js_null_t` tokens construct the existing optional scalar carrier; its ABI is
unchanged. `js_boolean_t` now carries JavaScript Boolean values through literals,
comparisons, calls, fields, closures and Maps, with explicit conversion to C++
conditions and numbers. `js_basic_num<double>` now supplies the `js_num` class
for Number literals, arithmetic, comparisons, calls, fields, captures, conversions
and browser counts. Its bitwise operators now implement JavaScript 32-bit
conversion and masked shifts through public Core; `>>>` uses
`.unsigned_shift_right(...)`. Raw Map/vector/JSON storage uses explicit adapters.
`js_nan_t` explicitly constructs Number NaN values and native binary64 NaN
literals. `js_basic_string<char>` now supplies `js_string` for String literals,
concatenation, calls, fields, captures and exceptions, with explicit raw storage
and browser adapters. Its first instance method is the already-proved ASCII-prefix
`startsWith`. Exact String unary `+`/`-` now uses `.to_number()`, and mixed
String numeric `-`, `*`, `/`, `%` and `**` reuse Number operations after that
conversion. String addition accepts Number, Boolean and finite nullable scalar
values with their proper text. C++ callers can concatenate built-in arithmetic
types and `js_boolean_t` in either order; Boolean words remain distinct from
numbers. Optional String arithmetic retains null/undefined tags until numeric
conversion. Closed Boolean/String values use typed owning alternatives through
parameters, returns, branch/loop edges and globals. Their optional form retains
undefined and null separately. Both support numeric arithmetic, generic addition,
concatenation, truthiness and `typeof`. These operations reuse public Core conversion/
formatting. Generic primitive addition now returns a proved closed String/Number
carrier, retaining its tags through calls, joins, loops and global stores. Its
optional form now preserves undefined, null, Number and String through source
global reads, calls and coercions. Strict/loose equality now covers these proved
String-containing unions while retaining type, absence, NaN and zero semantics.
Shared variables and nested capture pointers now carry those unions with owning
snapshots and observable absence. `js_symbol_t` and the native `Symbol` object now
provide fresh identities, immutable well-known keys such as `Symbol.hasInstance`,
owning descriptions and `toString`/`valueOf` methods. Fingerprinted DOM entries now
emit direct well-known Symbol reads, fresh construction with absent/String
descriptions, direct `toString`/`valueOf`, owning `.description` reads, identity
equality, truthiness and `typeof`, including branches, loops and typed returns.
Symbol-only exports use the same operations through a separate intrinsic contract,
with exact primitive parameters and local helper calls, without DOM inputs. Same-cell sibling calls now forward existing lifted capture
parameters after both functions pass the lift proof. Ordinary `instanceof`
on exact local class constructions now folds under the complete class/prototype
proof, preserving nominal identity and constructor effects. Broader Symbol
operations, custom hooks and nonconstant `instanceof` still require proofs.
`Runtime/Browser.hpp` now supplies borrowed `js_document_t` and `js_element_t`
views over public DOM/Style. Generated `matches` calls use the typed element
receiver. An explicit `current_document_parameter` host field now binds source
`document` to an element input's owner and emits guarded root/querySelector access
through the document view. Source document query-all, default-root helpers, the
application driver and remaining selector carrier migration still need work.
Object/Array prototypes remain planned.
This is the user's revised direction for the
native C++ interface and supersedes conflicting raw-carrier prescriptions in
master-plan part 24. Historical measurements retain their original scope.

## The request

Generate statically typed JavaScript semantics through purpose-built C++ classes.
Represent JavaScript values with concrete native types that expose appropriate
operators, instance methods and typed prototype methods. Make emitted code read
like `text.startsWith(prefix)`, `array.push(value)`,
`Element.prototype.querySelector.call(element, selector)` and
`document.querySelector(selector)`. Keep storage and ownership ordinary C++ and
implement browser operations by calling ctbrowser's public subsystems.

These types are the default public vocabulary for generated JavaScript values.
Standard-library types remain their storage and algorithm building blocks.
Compiler-only counters and proven storage operations may still use raw C++ types.
The existing native rule remains: prove types, operations, identities and owners
before emission; an unproved operation is a compile-time diagnostic. No VM,
collector, universal value box or reference-counted object graph is introduced.

The [semantic edge-case plan](native-js-semantics.md) uses a pinned wtfjs README
as a regression inventory and records additional coercion, equality, prototype,
evaluation-order, parser and host obligations. These classes centralize value
semantics; passing their unit checks does not establish that every source
specimen is admitted or behaves correctly.

## Target type vocabulary

All names below belong to `ctnative`. Number and String use basic class templates
with aliases for their default representations:

```cpp
namespace ctnative {
template<class T> class js_basic_num;
using js_num = js_basic_num<double>;
template<class T> class js_basic_string;
using js_string = js_basic_string<char>;
}
```

Prefer `ctnative::js_num` and `ctnative::js_string` in ordinary generated code.
Spell the basic templates explicitly only for an alternative representation
whose range, operations or encoding have been proved equivalent. Prefer
composition over public inheritance from standard containers. Add a method
when an admitted source operation needs it, with its semantic test; do not
implement every prototype up front.

| Target type | Meaning and implementation obligations |
|---|---|
| `js_string` (`js_basic_string<char>`) | Owning JavaScript String with byte/WTF-8 storage. Exposes String methods and admitted operators; `char` describes storage, not JavaScript indexing. Reuse public Core Unicode operations. |
| `js_basic_string<char16_t>` | The same String domain with UTF-16 storage where the encoding and conversion proof permits it. Storage choice must not silently change observable answers. |
| `undefined_t` | The distinct undefined value. No numeric, null, empty-string or invalid-handle sentinel substitution. |
| `js_null_t` | The distinct null value. Keep it separate from undefined at unions, calls, returns, fields and comparisons. |
| `js_nan_t` | Explicit NaN construction token for `js_num`. NaN remains a JavaScript Number, never an undefined/null or missing-value sentinel. |
| `js_num` (`js_basic_num<double>`) | JavaScript Number backed by binary64, including NaN, infinities and signed zero. Arithmetic and conversions use the existing semantic helpers where C++ differs. |
| `js_basic_num<T>` | Narrowed Number representation only when range and operation proofs establish equivalence; an integer storage parameter does not authorize C++ integer division or overflow. |
| `js_boolean_t` | The Boolean value with explicit contextual conversion to C++ `bool`. Numeric conversion is an admitted JavaScript operation, not an accidental implicit C++ promotion. |
| `js_vector<T>` | Owning dense native sequence/snapshot storage, normally backed by `std::vector<T>`. It does not by itself claim JavaScript Array identity, holes, prototype or species semantics. |
| `js_array_t<T>` | The interface for an admitted JavaScript Array, with `length()`, indexed operations and methods such as `push`, `pop` and `filter`. Use dense storage only under the existing density/content proof. Preserve reference identity, presence, mutation and eager evaluation. |
| `js_object_t` | A non-virtual interface/tag for generated closed-shape objects. Concrete generated classes keep concrete fields and checked methods; no generic property dictionary, universal payload or slicing through this base. |
| `js_bigint_t` | Owning arbitrary-precision integer with JavaScript BigInt semantics. Requires a public non-Script numeric implementation; a fixed-width integer or floating-point stand-in is insufficient. |
| `js_symbol_t` | Implemented native primitive identity, distinct from its description. `Symbol` exposes 15 immutable well-known keys and fresh construction; descriptions, `toString` and `valueOf` reuse public Core. DOM and primitive-only entries admit direct well-known reads, absent/String construction, direct primitive methods, identity equality, truthiness, `typeof`, branches, loops and returns. Source `.description` preserves owning undefined/String results. `Symbol.for`/`keyFor` and hook dispatch need further proofs. |
| `js_document_t` | An explicit borrowed document interface over a public `ctbrowser::document` and its live `style::engine`. Exposes methods/accessors through ordinary `document.member(...)` syntax. A containing session owns the resources when ownership is required. |
| `js_element_t` | The corresponding borrowed element interface, retaining document/node identity and its proved Style association. It enables receiver-only prototype calls without exposing a VM context. |

Primitive assignment has value semantics. JavaScript object/array assignment
aliases the existing identity; it must not invoke a deep container copy. An
allocation owns its storage, and uses borrow or transfer that owner according to
the existing escape proof. A copied native snapshot and an aliased JavaScript
Array are different operations even when both contain the same `T`.

Finite unions remain closed, proved sets of concrete alternatives. Reuse the
existing nullable/union machinery during migration. A single `std::optional<T>`
is suitable only when exactly one absence kind is possible or their distinction
is proved unobservable; it cannot collapse null and undefined generally.

## Symbol values and the Symbol object

`Runtime/Symbol.hpp`, included by `ctnative.hpp`, now implements:

```cpp
auto marker = ctnative::Symbol(ctnative::js_string{"marker"});
auto key = ctnative::Symbol.hasInstance;
auto description = marker.description(); // optional<js_string>: undefined or String
auto text = ctnative::Symbol.prototype.toString.call(marker);
auto same = ctnative::Symbol.prototype.valueOf.call(marker);
```

`Symbol()` and `Symbol(undefined_t{})` retain an absent description;
`Symbol(js_string{""})` retains a present empty String. Each call makes a new
identity, even with the same description. Copies and moves preserve that
primitive identity. Description results own their String snapshots. Symbols
are truthy only through explicit Boolean conversion; numeric and implicit String
conversion are unavailable. Other constructor coercions remain future work.

The 15 well-known fields come from one public Core catalog. They use constexpr
identities without startup allocation; fresh symbols own the existing Core
payload by value. A single atomic serial source covers translation units and
factory copies, refusing exhaustion before key reuse. VM payloads, equality,
fresh-key creation, descriptions and explicit formatting use the same Core.
The VM registry and key reconstruction retain their existing behavior.

`Symbol.hasInstance` is a symbol key, not a callable. JavaScript calls the
constructor's `constructor[Symbol.hasInstance]` hook, not `Symbol.hasInstance.call`.
The key's presence in this API does not yet supply that lookup or the compiler's
constructor/prototype proof. A measured non-callable object with a custom
hook returns true with one hook call in Node, but false with zero calls in the
current VM. Preserve that oracle gap explicitly; do not implement native
`instanceof` as an unchecked `std::holds_alternative` test.

**Implemented source slice:** `ctbrowser-dom-v1` entries with an explicit
`initial_intrinsics: ["Symbol"]` contract admit direct constant well-known reads.
The existing complete DOM source census, work bound and fingerprint checks
supply the proof; inference introduces `!ctnative.symbol` only from that live
query. Generated C++ uses `ctnative::js_symbol_t` and `ctnative::Symbol.hasInstance`
(or the requested property), with native identity equality, truthiness and
`typeof`. Local SSA copies retain identity. Rebinding, mutation, escape, dynamic
properties, coercion and custom hooks remain refused. No new Script or VM
dependency is introduced.

**Transport is implemented:** the retained `join`, `loop` and `symbol-return`
programs now compile unchanged. Structured state uses `std::optional<js_symbol_t>`
only because C++ declares control-flow storage before assigning it. Every source
edge supplies a Symbol; the empty storage state is never a JavaScript undefined,
null or fabricated identity. Reads and returns copy `js_symbol_t`, preserving
saved identities across later loop assignments and repeated calls. Mixed Symbol/
primitive or absence joins remain refused. GCC 13's inactive String-arm warning
is suppressed only around `js_symbol_t`; caller warnings remain enabled.

**Primitive-only exports are implemented:** `ctbrowser-intrinsics-v1` accepts
`version`, `provider`, `module_sha256`, `entry`, `initial_intrinsics: ["Symbol"]`
and optional `parameter_types`. That ordered array names every explicit parameter
as `"boolean"`, `"number"`, `"string"` or `"symbol"`; omitted/empty means no
parameters. String methods additionally require the explicit
`initial_intrinsics: ["Symbol", "String"]` contract. The fingerprinted named export
may have an inert declaration wrapper.
The complete bounded entry proof assigns exact categories without promising a
particular identity, description, truth value or Number range. Generated signatures
use the existing typed values by value, preserving Symbol identities, owning
String snapshots, signed zero and NaN. No document, DOM input, owned root or
script runtime is needed. Exact local helpers and uncaptured global helpers
expand on a charged private copy, followed by complete typed entry reproof. Global helpers
require unique inert declarations and a complete direct-callee use census.
**7660166b** admits their exact local declarations through the existing complete
closure proof when the original receiver has no observable use; receiver-observing
direct helpers retain their restricted path. Mutation, identity observations
and escaping functions remain refused. **7fde70e4** admits exact immutable local
primitive/Symbol captures, including nested captures inside global helpers,
through the existing complete cell and closure proof. Original implicit arguments,
initialization order, unused bodies and final typed proof remain checked.
Multiple invocations may supply
different primitive kinds; original argument evaluation order remains intact.
The frontend pads missing arguments with undefined, which still requires a valid
use and can produce a void export. Top-level effects,
ambient/wrapper captures, nullable/union/object entry parameters, extra host fields
and other intrinsics remain refused.

**Ordinary class tests are implemented:** the class initialization proof admits
`value instanceof Constructor` for exact same-block source constructions and
completed class setups. An explicit standard `Function` identity supplies the
original inherited `Symbol.hasInstance` method; the complete source census
excludes mutation, escape and reentry. The compiler compares nominal source
constructors and their proved heritage, then folds only the test. Constructors,
operand evaluation and their effects remain. Two unrelated classes with identical
C++ field shapes do not match. Custom hooks, replacement objects, unknown origins
and cross-block construction remain refused. No runtime wrapper is needed for
these constant answers. Six Node/VM cases pass in 48 native executions, with
26 refusal controls, including inheritance, aliases and retained effects.

**Fresh source construction and primitive methods are implemented:** the same
fingerprinted entry proof admits `Symbol()`, `Symbol(undefined)` and
`Symbol(exactString)`, including an immutable constructor alias. Each call emits
the existing native factory and produces a distinct identity. Direct zero-argument
`.toString()` and `.valueOf()` calls require the exact Symbol receiver and
original prototype. Generated code uses ordinary `js_symbol_t` values and methods;
the earlier fresh/method refusal sources now execute unchanged.

**Source descriptions are implemented:** `.description` calls the existing
`optional<js_string>` API and explicitly constructs an owning `nullable_string`
with undefined or String. Its proof kind remains distinct from DOM null/String.
Equality, truthiness, `typeof`, branches, loops and returns preserve that distinction;
saved descriptions survive later Symbol assignments. Exact `typeof` String/undefined
guards and equality with undefined now narrow the same saved value within its
proved String arm. Under an explicit String identity, `charAt` and `slice` with
nonnegative uint32 integer literal indices reuse the existing UTF-16 operations
and owning String extraction (**2e6dc90a**). **7f226c55** adds literal uint32
`slice(start, end)` with checked bounds; reversed bounds produce an empty String.
**1f315db4** adds signed literal `slice` bounds with uint32 magnitudes, including
negative zero and mixed start/end signs. Magnitudes clamp to the UTF-16 length
before unsigned subtraction. **d8f532c1** adds signed literal `charAt` indices
with the same magnitude bound: negative indices yield an empty String and negative
zero selects index zero. **72a99948** extends the same finite literal range to
fractional indices: truncation precedes sign testing and unsigned conversion, so
`-0.5` selects index zero. Exact `charAt(0)` and one-argument `slice(1)` retain their
separate first-unit and dataset-tail authority.
**cf37fb24** removes the uint32 magnitude ceiling for Number literals, including
literal overflow infinities. Lowering guards the unsigned conversion itself and
then clamps in `size_t`; NaN and dynamic/coercing origins remain refused.
**8e6e2e4d** also admits constant ASCII-prefix `startsWith` and
`charAt(0).toLowerCase()` through their existing proofs and public Core helpers.
Whole-string lowercase, non-ASCII prefixes and dynamic/coercing indices remain
separate. Dataset reconstruction and first-unit lowercase keep index 0/1 proofs.
Empty String remains present; truthiness, another description read and uses after
the guard grant no String authority. Mixed null/description joins remain separate.

**Primitive entry equality is implemented:** exact Number/Boolean operands use
the existing native strict/loose equality operations, including mixed kinds,
NaN and signed zero. Parameters, local helpers and loop-carried Boolean results
remain typed. With global/local helper composition, description guards and
**cf37fb24** wide literal String indices, the export fixture checks **648 native executions,
536 refusals and two mutations**, with **89 Node/VM agreements and seven known VM
casing/indexing differences**. Native/Node Unicode expectations remain intact;
no runtime implementation changed.
[Exact constructor Data and wide-index evidence](../handoff/2026-09-21-constructor-data-wide-indices.md).

**Next source slice:** broader String methods, dynamic/coercing indices and
conditional callee transport. Branch-mutated boxed locals remain separate from immutable captures;
mixed primitive unions and object coercion retain their own admission boundaries.
Registry operations, symbol-keyed fields and custom hook lookup/call/Boolean
conversion each retain their own proof and oracle obligations.

The focused API fixture checks 25 primitive observations against Node and the
VM, GCC/Clang compilation, two translation units, standalone-header use, a
mutation and three source refusal controls. It measures the native API, not
new source emission. [Exact checks](../handoff/2026-09-21-native-symbols.md).

The subsequent source fixture, `Browser/native-dom-symbols.test`, checks 21
attribute observations and two return paths against Node/VM and real public DOM
execution in eight GCC/Clang modes. It includes 38 refused contracts/operations
and one distinguishing identity mutation. Two selected CTests and four distinct
lit cases pass; full suites were skipped.
[Source integration evidence](../handoff/2026-09-21-native-symbol-source.md).

The transport follow-up checks 28 native DOM executions, including zero-iteration
loops, saved copies and repeated calls, with the same 38 refusal controls and
one identity mutation. The new intrinsic export fixture adds 16 native executions
and 56 refusals. Three selected Symbol lit cases and two exact CTests pass;
a generated Symbol loop also passes GCC ASan/UBSan.
[Transport and export evidence](../handoff/2026-09-21-native-symbol-transport.md).

Fresh construction and methods extend the existing export fixture to 48 native
executions and 74 refusals, including eight Node/VM observations and a 14-part
method transcript. The DOM fixture now runs 32 native executions and 36 refusals.
Each fixture includes an identity mutation. Two exact CTests and three distinct
Symbol lit cases pass across corrected runs; no new sanitizer run was made.
[Construction and sibling-capture evidence](../handoff/2026-09-21-symbol-construction-sibling-captures.md).

Exact intrinsic parameters extend the export fixture to **120 native executions,
107 refusals and two mutations**. Eighteen Node/VM observations cover identity,
loop selection, owning String/description snapshots, signed zero, NaN and infinities;
C++ assertions pin each exact signature. The former unused-parameter refusal
source now runs unchanged with an explicit Symbol parameter contract.
[Parameter and helper-publication evidence](../handoff/2026-09-21-helper-publication-intrinsic-parameters.md).

**990b752f** extends the intrinsic fixture to **152 native executions, 141 refusals,
two mutations and 22 Node/VM observations** with local helpers. The combined
base-publication/helper slice passes two exact CTests and three distinct selected
lit cases across corrected runs; full suites were skipped.
[Exact helper and base-publication evidence](../handoff/2026-09-21-base-publication-intrinsic-helpers.md).

## Operators and JavaScript semantics

The typed classes centralize semantics currently spread across free helpers and
emission sites. Instance methods and prototype method objects call the same
implementation. Lift existing helpers into that implementation; do not maintain
two algorithms for one operation.

- Use C++ arithmetic, comparison, indexing and conversion operators only where
  their expression behavior matches the admitted JavaScript operation. Number
  division/remainder, shifts, NaN, signed zero and Boolean conversion need their
  existing JavaScript rules. Disable unsafe implicit conversions to storage types.
- Define typed `operator==` as strict JavaScript equality for the admitted operand
  pair. Keep named `strict_equal`, coercive `equal`, SameValue and SameValueZero
  operations distinct. A C++ operator cannot spell `===`; container key equality
  must continue to use its required relation rather than inheriting `operator==`.
- Use `js_num{js_nan_t{}}` for explicit construction of a Number NaN. The token
  adds no JavaScript type or union alternative: `typeof` remains `"number"`,
  truthiness is false, strict equality with itself is false, and SameValue and
  SameValueZero still match NaNs. Keep a present NaN distinct from null,
  undefined and a missing Map entry. Do not use the token's C++ type identity
  as a runtime NaN test; computed NaNs remain ordinary `js_num` values.
- Keep `typeof` as a named operation. Null and objects, primitive wrappers and
  boxed primitive objects must retain their JavaScript distinctions. A
  `js_boolean_t` primitive is not `new Boolean(false)`.
- Fold exact ordinary class `instanceof` only under the source identity and
  prototype proof above, retaining operand effects. Route later nonconstant
  tests through a typed native wrapper. Invoke a proved
  `Symbol.hasInstance` hook with the constructor as receiver and the tested value
  as argument when present, then apply JavaScript Boolean conversion to its
  result. For the default path, use `std::holds_alternative<T>(value)` only when
  constructor identity and the prototype chain prove equivalence. Exact
  alternatives do not automatically match base classes, and a primitive is not
  an instance of its boxed constructor. Preserve hook lookup, evaluation order,
  effects and exceptions, including invalid right-hand operands. Unknown hooks
  or prototype behavior remain compile-time diagnostics; the wrapper adds no
  VM dispatch or universal value carrier.
- Do not overload `&&` or `||` to implement JavaScript short-circuit expressions.
  Overloaded C++ operators evaluate both operands, and JS returns an operand's
  value. Preserve explicit structured control flow for `&&`, `||`, `??`, optional
  chaining and conditional expressions.
- Preserve receiver, argument and getter evaluation order with temporaries where
  necessary. A readable call or overloaded operator cannot reorder source effects
  or alter exceptions. `.call(receiver, ...)` invokes the original method;
  `querySelector(selector).call()` is not its JavaScript equivalent.
- `char` storage does not make String length or indexing byte operations.
  JavaScript specifies UTF-16 code units, while iteration uses code points.
  Reuse public Core conversion, surrogate and casing routines and verify each
  operation against the current VM. Historical UTF-8 divergences are not evidence
  of current behavior for every String method. Preserve measured oracle behavior
  and existing refusals until any semantic alignment is separately implemented
  and tested; a wrapper migration must not silently change the oracle or output.
- Array holes, explicit undefined, missing properties, inherited properties and
  empty `pop` results stay distinguishable where observed. `std::vector::at` is a
  storage operation, not a universal JavaScript indexed read. Callback order,
  captured length, species and iterator/spreadability hooks still require proofs.

## Typed constructor and prototype objects

Expose `ctnative::Element`, `ctnative::Object` and `ctnative::Array` as statically
typed intrinsic objects with an ordinary C++ `prototype` member. Each supported
prototype member is a typed method object with `.call(receiver, arguments...)`.
Constructor-object static methods remain distinct: `Object.keys` belongs on
`Object`, while `hasOwnProperty` belongs on `Object.prototype`.

This is valid C++ member composition; it does not require overloading `.` or
building a dynamic prototype lookup mechanism. `Element.prototype.*` means a
specific supported member after the final dot, not a runtime wildcard.

The following combines implemented browser views with the **target interface**
for Array/Object, which is not yet compilable output.
`element` is a proved `js_element_t`, and `options` is a concrete generated
closed-shape object:

```cpp
ctnative::js_document_t document{dom, styles}; // Explicit borrowed resources.
ctnative::js_string selector{".selected"};
auto match = document.querySelector(selector);
auto nodes = ctnative::Element.prototype.querySelectorAll.call(element, selector);
auto count = nodes.size(); // Native snapshot; js_array_t is a separate interface.

ctnative::js_array_t<ctnative::js_num> values;
auto length = ctnative::Array.prototype.push.call(values, ctnative::js_num{1.0});
auto own = ctnative::Object.prototype.hasOwnProperty.call(
    options, ctnative::js_string{"enabled"});
```

The four stateless selector method types are members of `Element.prototype`.
Raw `element_ref` callers retain the explicit Style argument. `Browser.hpp`
adds receiver-only overloads for `js_element_t`; both spellings call the same
public core. Generated `matches` calls now use this view with a typed String.
Generated document-root and document `querySelector` calls now use `js_document_t`,
then bridge their null-only optional result to the existing raw element carrier.
Other generated selectors still use their existing raw nullable/snapshot carriers
pending migration of their result and ownership flow.

An intrinsic object's C++ type or spelling does not prove its JavaScript identity.
HostContract must still establish the original binding, prototype, method,
receiver, lookup chain and absence of relevant replacements/hooks. Source
shadowing, own method overrides, getters and mutation cannot be skipped. Expose
only methods with a complete source proof. `.apply` and `.bind` are added only
with their argument, receiver, capture and lifetime proofs; no catch-all dispatch.

User-defined JavaScript classes continue to become concrete generated C++ classes.
Their field layout, inheritance and method dispatch use the existing closed-shape
and prototype proofs. Intrinsic prototype method objects do not add virtual
inheritance or a mandatory heap-allocated superclass to every value.

## Browser ownership and accessors

`js_document_t` borrows both the document and its associated live Style engine.
This interface is implemented in `Runtime/Browser.hpp`, included after defining
`CTNATIVE_DOM`. Element views cannot be default-constructed or implicitly created
from null/undefined. Selectors return `std::optional<js_element_t>` for null-only
absence and `std::vector<js_element_t>` for snapshots. A snapshot owns its sequence,
while its elements still borrow their document and Style engine. Explicit checked
`.value()` extraction bridges to public `element_ref` callers. Element identity
compares document/node identity independently of the borrowed Style pointer.
The existing borrowed-entry caller or owned-session object owns the atom table,
document and engine. Views cannot outlive that owner; owner moves must not
invalidate outstanding views. Validate document/node generations and engine atom
association before source effects. Element equality includes document identity.

Use real accessor methods such as `document.documentElement()` and
`document.querySelector(selector)`. If an accessor returns a nullable root, keep
the source null/undefined distinction and require the appropriate use guard.
Do not cache a root across mutations without a proof. Property proxy classes are
unnecessary for this first interface: C++ does not have JavaScript property-getter
syntax, and an accessor method makes evaluation explicit.

All browser behavior stays in public ctbrowser DOM/Style and other owning
subsystems. An operation currently trapped inside a VM binding requires the
existing lift-core/thin-adapter extraction workflow. Generated code and these
classes contain no `script::context`, `script::value`, collector handles or
runtime lookup tables. ctbrowser never depends on ctcompile.

The implemented host binding is optional `current_document_parameter` on
`ctbrowser-dom-v1` and `ctbrowser-dom-session-v1`. Its index names a declared
element input whose owner is the source `document`; detached anchors still select
that document. Both JSON parsing and live proof reject invalid indices and foreign
providers. The contract promises the original binding, root accessor, query method
and lookup chains. Complete source proof rejects their replacement or mutation,
incorrect receivers, escaped handles and unsupported effects.

Source `document.documentElement` and `document.querySelector(String)` call the
typed view with the anchor's validated owner and live Style. Each root access
reads the current document root, and document queries include it. The
`element_or_null` bridge extracts checked optional views into the existing nullable
`element_ref` carrier. Exact truthiness guards authorize present-result uses;
borrowed returns, storage and joins remain refused. The existing caller/session
owns every resource, with handle and Style validation before source effects.
Source `document.querySelectorAll` remains unproved despite the C++ view method.
The full Bootstrap default-root helpers and application driver remain unfinished.
See [native DOM entries](../native-dom-entry.md#explicit-source-document-binding)
for the contract and focused validation scope.

A global-looking current-document accessor remains optional future work; none is
needed or added by this explicit binding. If a later proved entry requires one,
it must borrow the current invocation's document and Style association;
the invocation/session remains the owner. Bind it with an RAII scope that saves
and restores the previous binding on normal return and exceptions, including
nested calls into another document. Concurrent invocations need independent
bindings; thread-local storage alone is insufficient for interleaved asynchronous
tasks. Reject access outside a bound scope, and do not let borrowed views escape
their owner. Keep explicit document parameters available; primitive-only exports
need no document accessor.

## Existing implementation and migration

The current entry header is `include/ctcompile/CTNative/Runtime/ctnative.hpp`.
It contains a global `using js_num = double`, finite nullable carriers, object/Map
storage helpers, and `matches`, `closest`, `querySelector`, `querySelectorAll`
method objects. `Element.prototype` now owns those method objects, with the
flat names retained as constant reference aliases. Distinct absence tokens now
construct the existing nullable scalar, and `js_boolean_t` carries Boolean
values. `Number.hpp` supplies `js_basic_num<double>` and its `js_num` alias;
proved Number values and signatures use this class throughout lowering.
`js_nan_t` supplies explicit NaN construction. Number bitwise operators use
`to_int32()`/`to_uint32()` backed by `ctbrowser/core/number.hpp`, shared with the
VM and Math adapters. `&`, `|`, `^`, `~`, `<<`, `>>` and
`.unsigned_shift_right(...)` return `js_num`, including unsigned results above
2^31. The compiler converts admitted primitive operands through the existing
Number boundary; objects, BigInt, Symbol and wider unions remain refused.
`primitive-bitwise.test` checks 76 observations in eight native modes, the
original two refusal witnesses on GCC/Clang, four refusals and two mutations.
`String.hpp` now supplies
`js_basic_string<char>` and `js_string`. Object/Array intrinsic prototype classes
are not implemented yet.
Public Core already supplies String/Unicode primitives. BigInt currently lives
behind Script and needs extraction before native use.

Use `ctnative::js_basic_num<T>` and `ctnative::js_basic_string<T>` for the class
templates, with `ctnative::js_num` and `ctnative::js_string` as the canonical
default aliases declared above. The existing global `js_num = double` remains a
separate compatibility spelling until its callers migrate. Migrate one admitted
group of operations and all its type pins, signatures and clients together, then
retire that global alias. No regex-only rename or sudden reinterpretation of
existing `auto`/template deduction.

1. **Element prototype composition implemented.** `Element.prototype` reuses
   the four selector method types; the proven EmitC callees and emitted-code
   checks use its members. Current flat names alias the same objects. Establish
   the same layout for Object/Array when their first already-proved methods migrate.
2. **Primitive types in progress.** Distinct `undefined_t` and `js_null_t` tokens
   now construct `nullable_scalar` for source literals and other absence values.
   Default construction and `.null()` remain compatible. Calls, returns and
   optional joins still use the tagged carrier: inference currently gives both
   absence literals `Opt<Bottom>`, so this is not an exact-token ABI migration.
   `js_boolean_t` now separates JavaScript Boolean values from C++ control-flow
   conditions. Literals, comparisons, signatures, optional conversions, Map
   keys/payloads, captured fields and printing use the class. Its exact-bool
   constructor and contextual conversion are explicit; `.to_number()` reuses
   the numeric conversion boundary. Public JSON storage and internal predicates
   retain raw `bool`, with explicit adapters at generated boundaries.
   `js_basic_num<double>` now provides explicit binary64 construction/extraction,
   contextual truthiness, same-type arithmetic and partial ordering. Its
   `js_num` alias is the numeric global-read result; emitted observations extract
   `.value()` before C varargs. Nullable/object scalar adapters preserve the tag,
   NaN and signed zero. Only `double` is currently supported by the template.
   `js_nan_t` now constructs a Number NaN explicitly. Boolean and nullable
   `.to_number()` / `to_number()` conversions return `js_num`, with undefined
   producing NaN, null producing positive zero and numeric payloads retaining
   their value and zero sign. Native binary64 NaN literals use the token while
   ordinary EmitC and other floating-point formats retain their own spelling.
   The Number value carrier now uses `ctnative::js_num` for literals, arithmetic,
   comparisons, function/capture signatures, fields and exceptions. Numeric
   literals retain binary64 attributes and construct the class explicitly.
   Remainder and exponentiation extract operands for existing standard-library
   calls and wrap the result; the JavaScript exponentiation guard is unchanged.
   Map keys/payloads and vector/JSON storage retain binary64, with explicit
   construction/extraction at their boundaries. SameValueZero still operates
   on that storage. Vector/Map lengths and optional DOM Number conversion return
   the class; public Core calls and proved vector indices receive raw values.
   Const qualification, deduction pins and Map snapshot fusion recognize the
   typed carrier. The global raw `js_num` alias remains for raw f64 printer/storage
   compatibility and existing clients; generated JS signatures use the qualified
   class name. Remove that alias only with its remaining raw clients/printer.
   `js_basic_string<char>` now supplies the owning `js_string` carrier. Explicit
   construction preserves exact bytes, including embedded NULs and lone
   surrogates; `.value()` extracts storage without an implicit conversion.
   Same-type concatenation calls public Core `join_surrogates`; the old raw
   helper forwards to that implementation. Literals, strict equality, truthiness,
   calls/returns, captures, fields and exceptions use the class. The first
   instance method, `startsWith`, retains its existing constant ASCII-prefix
   source proof. Map/vector/nullable/Boolean-String-union/JSON storage remains
   raw `std::string` with explicit adapters; callback parameters and results
   cross that boundary too. Printing and deduced-type pins retain exact types.
   General String length still counts stored bytes (ND-1), as does the current
   VM. Admitted DOM/intrinsic `charAt`/`slice` with bounded finite literal indices
   truncated toward zero and isolated-first-unit lowercase keep their public Core
   UTF-16 operations and recorded VM differences.
   The class has no general length/index/casing API yet. Do not normalize all
   constructor bytes or silently broaden the ASCII-prefix admission.
   Exact String unary `+`/`-` now calls `.to_number() -> js_num`; the Number
   identity rewrite still requires an original Number operand. Mixed exact
   String/Number addition in either order uses class overloads and public Core
   `number_to_string`. Generated programs link Core and its configured dependencies,
   with no Script symbols. The focused `string-coercions.test` passes 18 distinct
   Node/VM/native observations across eight native modes, four refusal controls,
   optional String and generic-addition admission controls and a distinguishing
   `baNaNa` mutation. Core `number_to_string` now checks range before its integer
   fast-path cast.
   String numeric arithmetic (`-`, `*`, `/`, `%`, `**`) now uses that conversion
   and the existing Number operators/remainder/exponentiation guards. Addition
   with an exact String admits Boolean and finite nullable scalar alternatives;
   `nullable_scalar.to_string()` preserves null/undefined, Boolean words, NaN
   and Number zero spelling. The `string-arithmetic.test` source gate passes
   26 observations across eight native modes, three refusals, an optional String
   admission control and one mutation.
   Raw C++ arithmetic types and `js_boolean_t` also have constrained String
   addition overloads in both orders. Raw numerics enter the binary64 Number
   domain: for example, `9007199254740993LL` spells `9007199254740992`; extended
   floating-point overflow is checked before narrowing and spells Infinity.
   Boolean operands spell `true`/`false`. Pointers, enums and merely convertible
   classes are excluded; the explicit String constructors remain unchanged.
   Optional String unary `+`/`-` and binary `-`, `*`, `/`, `%`, `**` now call
   `nullable_string.to_number()`: undefined produces NaN, null produces positive
   zero, and present text uses public Core without changing its stored tag.
   Closed local Boolean/String unions use `std::visit` for numeric conversion
   and concatenation with an exact String. Boolean false becomes Number zero;
   String `"false"` becomes NaN. The `string-union-coercions.test` source gate
   passes 51 observations across eight native modes, two remaining refusals,
   two generic-addition
   admission controls and two distinguishing mutations.
   Generic primitive `+` now selects Number addition or String concatenation from
   the actual alternatives and returns `ctnative::number_string`, exactly
   `std::variant<js_num, js_string>`. It reuses the typed Number/String operations
   and existing finite optional/Boolean-String carriers. Number/String parameters,
   returns, conditional/loop edges, truthiness, `typeof`, later numeric conversion
   and global stores preserve the selected tag. The optional source carrier is
   `nullable_number_string`, exactly
   `std::variant<undefined_t, js_null_t, js_num, js_string>`. It preserves all four
   alternatives through parameters, explicit returns, branches, loops, global
   reads/writes, numeric conversion, `typeof`, truthiness and concatenation.
   Globals with Number/String contents use this carrier, initially undefined;
   definite observations retain their checked present-value extraction. The old
   `std::optional<number_string>` extraction helper remains for compatibility.
   The complete former changing-global refusal now runs unchanged in
   `optional-number-string.test`, whose 53 observations include early reads and
   saved copies across later writes. Eight native modes, four refusal controls
   and two mutations pass.
   Closed Boolean/String values now use `boolean_string`, exactly
   `std::variant<js_boolean_t, js_string>`, through parameters, returns, branches,
   loops and globals. `nullable_boolean_string` adds distinct `undefined_t` and
   `js_null_t` alternatives. Global storage starts as undefined; numeric/text
   conversion, truthiness, `typeof` and generic addition preserve actual tags.
   The original parameter/return refusal programs now execute unchanged in
   `boolean-string-transport.test`: 50 main observations across eight native modes,
   two return observations on GCC/Clang, four refusal controls and two mutations.
   Map storage retains its existing raw String variant and exact per-operation
   proof requirements; a proved String extraction unwraps the typed scalar at
   that boundary. No whole-union Map adapter is introduced.
   Strict and loose equality now cover the admitted String-containing primitive
   unions. `primitive_strict_equal` preserves JavaScript kinds;
   `primitive_equal` compares two Strings before numeric coercion and keeps
   null/undefined distinct from empty text, false and zero. Both return
   `js_boolean_t`; `!=` and `!==` negate those typed results. The helpers visit
   existing finite variants, borrow String payloads and reuse scalar equality
   and public Core parsing. No combined boxed carrier or comparison copy is used.
   The three original equality refusal programs execute unchanged in
   `primitive-equality.test`: 48 main observations in eight native modes, six
   further witness observations on GCC/Clang, five refusals and two mutations.
   Exact String pairs retain their direct C++ comparison.
   Finite String-containing unions now use the existing shared-cell and capture
   pointer path. The type join proves compatible stores, including whole
   sub-unions; existing conversions preserve their tags. Loads own snapshots
   across writes. The hoisted initial is omitted only when a write dominates
   every read, matching inference; observable undefined/null remain distinct.
   The original `shared-mixed.js` runs unchanged. `primitive-cells.test` checks
   44 observations in eight native modes, four remaining refusals and two mutations.
   Ordinary return dispatch now reuses `normalizeStructuredExits` on a disposable
   clone. Its work budget includes the initial copy; successful normalization
   precedes fresh inference/admission, and later refusal restores the original
   body. Unused union slots receive their destination type. The preserved
   `index-switch.js` now executes unchanged: `return-dispatch.test` checks eight
   observations in six native modes, mutation, budget and rollback controls.
   The original `nested-sibling.js` now lowers unchanged: a bound sibling call
   forwards captures only when both closures already capture the identical cell
   in the same frame. The existing immutable-value/shared-pointer proof remains
   authoritative. A disposable whole-lift attempt publishes only after both
   caller and callee lift; otherwise the original binding remains executable.
   Five observations in four native executions, four refusals, a mutation and
   a boxed refusal execution pass. Missing captures, deeper relays and escaping
   callers remain separate proofs.
   Broader unions and mixed container payloads remain separate; never stringify
   both sides unconditionally.
   Keep numeric relational conversion separate from String lexicographic ordering
   and loose equality. Object conversion hooks remain refused; an overload or class
   method does not establish their source proof.
   Resolve general UTF-16 length/index/comparison/casing alignment as a separate
   runtime/compiler change. Update literal creation, joins, calls/returns,
   print helpers and deduced-type assertions with each admitted operation.
   Keep MLIR's semantic types and proof authority; C++ classes do not replace
   inference. Extract type-specific headers only as their implementation grows,
   retaining `ctnative.hpp` as the generated-code include.
3. **Collections and objects.** Migrate dense snapshots to `js_vector<T>` and
   admitted Arrays to `js_array_t<T>`. Preserve alias ownership and generated
   concrete object shapes. Exact ordinary `instanceof` now folds under the class
   proof. Implement the wrapper for remaining tests once constructor identity and `Symbol.hasInstance`
   lookup/call proofs exist. Test a custom hook (including a non-Boolean return
   and thrown exception), default matching, inheritance and primitive rejection
   against Node and the VM before extending source admission. The current VM
   skips custom hooks; record or resolve that gap without matching native to it.
   Reconcile Shell's current NodeList indices above
   1,000,000 returning undefined before broadening indexed `R.find` consumers;
   retain the separate 2^24 proxy-spread cap. A NodeList is not a JavaScript Array.
4. **Document/element views in progress.** Borrowed/owned entries now emit typed
   `matches` receivers and explicitly bound document root/querySelector calls.
   Migrate remaining nullable/snapshot carriers, prove source document query-all
   and Bootstrap default-root calls, then connect an application driver. A C++
   accessor alone never broadens source admission.
5. **BigInt and Symbol.** Symbol values, fresh creation, well-known properties
   and primitive methods now have a shared Core/native API. Direct well-known
   reads, absent/String construction, direct primitive methods and branch/loop/
   return transport now emit in proved DOM and primitive-only entries. Source
   `.description` preserves owning undefined/String results. Exact primitive/Symbol
   parameters and local/global helper calls, including immutable local captures,
   are implemented. Exact description guards permit Number-literal `charAt` indices
   and `slice` bounds, including overflow infinities, with explicit String identity.
   Bounds truncate toward zero and unsigned conversion is guarded. ASCII-prefix
   checks and isolated-first-unit lowercase retain their separate proofs;
   broader methods and dynamic/coercing indices remain separate.
   Registry, symbol-keyed fields and hooks remain
   separate. BigInt still needs its public non-Script core and ownership
   proofs. Unsupported uses remain compile-time diagnostics.

## Focused acceptance criteria

For each implementation batch, run only its affected devbox targets and exact
CTest/lit cases under the build lock, plus required formatting. A docs-only plan
revision needs no build or CTest. Full suites and broad matrices require an
explicit user request.

- Emit and compile representative operator, instance-method and prototype-method
  expressions in both printing modes with GCC/Clang and the repository flags.
  Pin the actual purpose-built result types, reference categories and ownership.
- Compare changed semantics with the current VM and relevant public browser core.
  Select edge cases for the batch: NaN/signed zero, absence tags, surrogate strings,
  holes/aliases, evaluation order, property shadows or owner lifetime. Record
  public-API-only checks separately from differential observations.
- Preserve missing-identity, mutation, coercion, stale-proof, escape and insufficient
  work-budget refusals. Unsupported BigInt/Symbol operations must not gain accidental
  admission through a generic class interface.
- Inspect generated C++ and linked symbols: direct public subsystem calls, no
  Script/VM/collector dependency, no universal property/handle table. Use ordinary
  value ownership, scoped borrows and proved moves; do not replace the collector
  with a reference-counted object graph.
- Record what migrated, what passed and the exact next operation in HANDOFF and
  the master plan. Historical coverage numbers do not become type-migration claims.
