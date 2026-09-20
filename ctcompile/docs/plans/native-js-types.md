# Native JavaScript types and prototype interfaces

**Status: in progress, 2026-09-20.** `Element.prototype` composition is implemented
using the existing four selector method objects. Distinct `undefined_t` and
`js_null_t` tokens construct the existing optional scalar carrier; its ABI is
unchanged. `js_boolean_t` now carries JavaScript Boolean values through literals,
comparisons, calls, fields, closures and Maps, with explicit conversion to C++
conditions and numbers. `js_basic_num<double>` now supplies the `js_num` class
for Number literals, arithmetic, comparisons, calls, fields, captures, conversions
and browser counts. Raw Map/vector/JSON storage uses explicit adapters.
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
conversion. Closed local Boolean/String unions support numeric arithmetic and
concatenation with an exact String. These operations reuse public Core conversion/
formatting. Generic primitive addition now returns a proved closed String/Number
carrier, retaining its tags through calls, joins, loops and global stores. Object/
Array prototypes and document views remain planned. This is the user's revised direction for the
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
| `js_symbol_t` | A symbol identity, distinct from its description. Well-known symbols and `Symbol.for` require separate identity/registry proofs; text equality cannot implement them. |
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
- Route admitted `instanceof` through a typed native wrapper. Invoke a proved
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

The following is the **target interface**, not currently compilable output.
`element` is a proved `js_element_t`, and `options` is a concrete generated
closed-shape object:

```cpp
ctnative::js_document_t document{dom, styles}; // Explicit borrowed resources.
ctnative::js_string selector{".selected"};
auto match = document.querySelector(selector);
auto nodes = ctnative::Element.prototype.querySelectorAll.call(element, selector);
ctnative::js_num count = nodes.length();

ctnative::js_array_t<ctnative::js_num> values;
auto length = ctnative::Array.prototype.push.call(values, ctnative::js_num{1.0});
auto own = ctnative::Object.prototype.hasOwnProperty.call(
    options, ctnative::js_string{"enabled"});
```

The first implemented step reuses the four stateless selector method
types as members of `Element.prototype`, retaining the explicit
Style argument: `Element.prototype.querySelector.call(element, styles, selector)`.
Once the typed element/document view carries a proved Style association, that
argument is supplied by the receiver. Both spellings call the same public core.

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

The document facade alone does not authorize a JavaScript `document` global or
the default `document.documentElement` argument. That source binding, root
nullability and session ownership still require the documented host proof.

## Existing implementation and migration

The current entry header is `include/ctcompile/CTNative/Runtime/ctnative.hpp`.
It contains a global `using js_num = double`, finite nullable carriers, object/Map
storage helpers, and `matches`, `closest`, `querySelector`, `querySelectorAll`
method objects. `Element.prototype` now owns those method objects, with the
flat names retained as constant reference aliases. Distinct absence tokens now
construct the existing nullable scalar, and `js_boolean_t` carries Boolean
values. `Number.hpp` supplies `js_basic_num<double>` and its `js_num` alias;
proved Number values and signatures use this class throughout lowering.
`js_nan_t` supplies explicit NaN construction. `String.hpp` now supplies
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
   VM. Existing admitted DOM `charAt(0)`, `slice(1)` and isolated-unit lowercase
   keep their separate public Core UTF-16 operations and recorded VM differences.
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
   String `"false"` becomes NaN. Their parameter/return/global ABI remains outside
   admission. The `string-union-coercions.test` source gate passes 51 observations
   across eight native modes, two remaining refusals, two generic-addition
   admission controls and two distinguishing mutations.
   Generic primitive `+` now selects Number addition or String concatenation from
   the actual alternatives and returns `ctnative::number_string`, exactly
   `std::variant<js_num, js_string>`. It reuses the typed Number/String operations
   and existing finite optional/Boolean-String carriers. Number/String parameters,
   returns, conditional/loop edges, truthiness, `typeof`, later numeric conversion
   and global stores preserve the selected tag. Global storage uses
   `std::optional<number_string>` solely to detect an uninitialized store;
   that optional is not a source null/undefined representation.
   Next implement optional Number/String transport, beginning with the preserved
   `generic-addition.test` changing-global read/copy/reassign refusal. Represent
   source null and undefined separately, including early reads, before admitting
   those paths. Boolean/String signatures, wider unions and mixed container
   payloads remain separate; never stringify both sides unconditionally.
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
   concrete object shapes. Implement the typed `instanceof` wrapper alongside
   class/prototype work once constructor identity and `Symbol.hasInstance`
   lookup/call proofs exist. Test a custom hook (including a non-Boolean return
   and thrown exception), default matching, inheritance and primitive rejection
   against the VM before emitting that operation.
   Reconcile Shell's current NodeList indices above
   1,000,000 returning undefined before broadening indexed `R.find` consumers;
   retain the separate 2^24 proxy-spread cap. A NodeList is not a JavaScript Array.
4. **Document/element views.** Adapt existing borrowed and owned entries to typed
   views, then prove document-root access and shorten receiver calls. Do not
   broaden source admission merely because a C++ accessor exists.
5. **BigInt and Symbol.** Supply their native core, identities, operations and
   ownership proofs in separate changes. Until then the names are plan targets
   and unsupported uses remain compile-time diagnostics.

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
