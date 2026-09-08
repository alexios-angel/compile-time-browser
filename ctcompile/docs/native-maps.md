# Native Maps toward Bootstrap Data

The native backend can lower standard `Map` instances with primitive or proved
identity-only object keys and number, boolean, owning-string, closed
Bool/Number or Bool/String alternatives, or finite acyclic Map values, including
closed function parameters, returns and lifted captures. A returned handle
owns its storage after the factory frame ends. Monomorphic
[returned closures](native-returned-closures.md) can also own
Map captures after factory return. Proved
[returned method tables](native-method-tables.md) own their callable fields.
[Nested Maps](native-nested-maps.md) preserve child ownership and require
[proved membership](native-map-presence.md) for child lookup.
[Property-free object keys](native-object-keys.md) carry owning identity.
Bootstrap Data still needs component-instance values, general object/host
identities and typed publication.

## Representation and semantics

Each allocation owns its storage through
`std::shared_ptr<ctnative::number_map<K>>`. Primitive keys use `js_num`, `bool`
or owning `std::string`; object identities use an owning handle. Numeric values
use `js_num`, the alias of `double`. Different allocation sites may have
different schemas. The result of `set` shares the original Map, so chained
calls and aliases observe the same mutations. Parameters and return values
copy owning handles; factory invocations still allocate independent Maps.
This is the numeric-leaf representation. Boolean and UTF-8 string payloads use
`map_storage<K, bool>` and `map_storage<K, std::string>`. Nested schemas use
`map_storage<K, V>` with an owning child handle as `V`; a separate containment proof rejects
all schema cycles. Every instance use is proved. Generated programs link
neither the interpreter nor its collector.

Closed mixed key and payload schemas use exactly `std::variant<bool, double>`
or `std::variant<bool, std::string>` in that same owning storage. Each call
operand must have one proved scalar alternative. Key comparison first compares
the alternative, then applies its ordinary scalar equality: false and zero are
distinct keys, while numeric NaNs and signed zeros still use SameValueZero.
The final read's type never narrows the storage schema or removes other writes.

When no admitted native Map operation observes iteration order, the module
uses `std::map<K, V, map_key_less<K>>`. Lookup helpers call `map->find(key)`;
updates use `insert_or_assign`. The comparator groups all numeric NaNs into
one equivalence class and treats positive and negative zero as equal.
Ordinary `std::less<double>` would not provide the required NaN semantics.
String comparison operates on the owning byte strings, and object keys retain
their owning identity handles.

Every admitted `Array.from(map.keys())` or `Array.from(map.values())` selects
the insertion-ordered vector representation for every Map in that native
module. This conservative choice
is made before deforestation, so eliminating a temporary snapshot does not
turn a positional projection into sorted-key traversal. Deforestation requires
the exact ordered-storage and snapshot helper definitions as well as the
common helper contract. The ordered representation exposes the same `find`
interface, with linear lookup; the associative representation has logarithmic
lookup. No performance improvement is assumed for very small Maps.

String-to-number allocations use the shorter emitted names:

```cpp
namespace ctnative {
using string_to_number_map = number_map<std::string>;
inline std::shared_ptr<string_to_number_map> make_string_to_number_map() {
    return make_number_map<std::string>();
}
}
```

The forwarding function is valid C++; a `using` declaration cannot alias a
function-template specialization. `number_map` uses the native `js_num` alias
for its numeric payload, whose underlying type remains `double`.

| Operation | Native behavior |
|---|---|
| `new Map()` | Empty owning allocation; constructor arguments are refused |
| `set(key, value)` | Replace in place or append; return the same Map identity |
| `get(key)` | Homogeneous scalar value or absent; a child Map requires presence; a mixed payload requires independent presence and an exact scalar type |
| `has(key)` | Boolean membership test |
| `delete(key)` | Remove the entry and report whether it existed |
| `clear()` | Remove all entries and return absent |
| `size` | Number of current entries |
| `Array.from(map.keys())` | Independent numeric or string array snapshot after proved immediate iterator consumption |
| `Array.from(map.values())` | Independent numeric or owning string array snapshot after proved immediate iterator consumption |

Numeric lookup uses SameValueZero: NaN finds NaN, and positive and negative
zero are one key. Replacement preserves the original key and position;
deleting and reinserting moves the entry to the end. The current interpreter
retains the sign of the first inserted zero key, so native `keys()` does too.
This differs from ECMAScript Map's zero normalization.

Runtime commit `e6c77fc` changed `keys()` and `values()` to return iterators.
Native admission requires exactly one semantic use of each iterator: a proved
`Array.from` argument in the same block, with only constants, root bookkeeping
and proved Array builtin lookups in between. Direct indexing, mutation,
publication, delayed consumption and repeated consumption refuse. The internal
eager vector is equivalent only under this proof; it is not a general iterator
representation.

The materialized arrays survive subsequent mutation or clearing. Scalar reads
and `length` are supported; array mutation, escape and unsupported element
types remain refused. Standard `Array.from` materialization and optional string
scalar reads are covered
by [native-string-snapshots.md](native-string-snapshots.md). Strings retain
the interpreter's UTF-8/WTF-8 byte semantics, including embedded NUL and lone
surrogates, as described in [native-strings.md](native-strings.md).

Missing `get` results use the existing optional scalar or owning string representation. Arithmetic
converts a missing value to NaN for numeric operations. Equality and `typeof`
retain the distinction between a missing value and a stored NaN through the
[tagged optional scalar carrier](native-optional-scalars.md). The same scalar
carrier distinguishes stored `false` from undefined; the nullable string carrier
distinguishes `""` from undefined. Both ordinary and proved-present string reads
copy their payload, so later overwrite, deletion or destruction cannot invalidate
a saved result. String value snapshots also copy every element and retain
insertion order independently of later Map mutation. Boolean value snapshots
still refuse because this tier has no boolean-vector carrier.

Stored primitive values must be definite: numbers, including NaN, booleans,
UTF-8 strings, or the two closed mixed schemas above. Optional stored values,
Number/String or larger unions, mixed snapshots and general object keys remain
refused. A mixed `get` is admitted only when the live structured must-analysis
independently proves both membership and the payload type from the last write
on every reaching path. A write takes its tag from a literal or an independently
proved saved scalar read. `has` adds no payload evidence. Possible aliasing
writes, callee effects, deletion and loops invalidate contents conservatively.
A saved read keeps its own scalar tag after known mutations; branches intersect
these SSA facts separately from entry contents. Unknown values never gain a tag
from the Map schema, another invocation or input annotations.

The read returns its exact scalar by value, preserving saved string ownership.
Local read/write candidates must belong to the same proved Map family; arbitrary
parameters remain excluded. Every stored alternative still participates in schema
inference. All input annotations are cleared and rederived. See the nested-Map
document for its additional presence and containment proofs.

`native-map-mixed.mlir` checks saved Bool/Number/String chains across associative
and insertion-ordered modules with Node/interpreter, GCC/Clang, explicit/deduced
output and ASan/UBSan. Negative programs cover branch tags, possible alias
writes, direct callee writes, `has`, deletion and unproved nonliteral payloads,
including missing reads written back into storage, forged presence/type facts
and reruns. Neither mode emits Script symbols.

## Proof boundary

`NativeMap.cpp` derives its annotations after closure lifting and before type
inference. Input annotations are cleared, including on repeated passes. It
requires a standard `Map` global used only as the constructor, matching
`new.target`, supported constant property names, exact method arities and
the same receiver from which each method was read. A schema graph connects
`set` results, corresponding actual/formal arguments, and callee returns with
call results. Every producer in a connected family must be proved to be a
Map: one Map input cannot justify a scalar or absent input at another call.
Call boundaries must be closed private functions with visible callers and
returns. Argument counts also have to match, as the CTJS verifier requires.

The first identity proof is conservative across the whole module: assigning
`Map`, inspecting or exposing its constructor, reading other host/global
values, unknown calls or constructors, and dynamic invocation or deletion
prevent admission. Ordinary user calls already made direct may remain.
Lifted immutable captures are carried as owning Map parameters. The leftover
capture cells must be erased bookkeeping for lifted closures. Reassigning a
shared Map binding, publishing a Map in a global or ordinary object field, or
merging Maps through structured phi values remains refused. This proof does
not supply general builtin effect analysis or a general callable
representation. Proved returned closures and method tables can carry immutable
Map bindings in owning environments; each extraction joins its own capture
slot's schema family.

Inference joins every stored value and every queried or stored key across the
schema family. Independent allocations passed to the same formal parameter
share a C++ schema, while preserving separate runtime identities. Different
formal slots stay separate. A read with a different key type widens the schema
and is refused. Constructor and method identities are erased bookkeeping and are
excluded from the numeric global census; they never become globals to print.

## Snapshot indexing correction

Map snapshots reuse the dense-array helpers. Review exposed the existing
ND-8 defect: `vec_at` rejected fractional numeric indices, while the reference
truncates toward zero before checking bounds. It now truncates first, retaining
the NaN, infinity and bounds guards before integer conversion. The original
`native-index-truncation-fixture.js` is a passing regression with an off-by-one
negative control. The Map fixture independently exercises fractional reads
through both snapshots, including `-0.5` and a computed parameter.
See [native-divergences.md](native-divergences.md#nd-8--an-out-of-range-index-produces-tagged-undefined).

## Bootstrap boundary

A [source-derived probe](bootstrap-data-probe.md) keeps Bootstrap 5.3.8's UMD
wrapper and Data declaration, ending the factory after that declaration with
`return e`. CommonJS and browser interpreter calls made after factory return
produce 19 observations covering identity, replacement, absent/wrong-key
operations, duplicate-component rejection, removal isolation and object-valued
reinsertion. Delayed AMD invocation adds a twentieth observation confirming
the factory was retained before invocation.

Native lowering still refuses all seven functions in the CommonJS/browser
probes and all eight in AMD, including the console recorder added to check
the rejection path. The factory reports an escaping method-bearing
object; its methods retain unlowered captures. The UMD entry also needs `this`
and host-environment types, while AMD exposes the factory value. These are
measured prerequisites, not evidence that Data or full Bootstrap compiles.

Map handles and proved callable method tables now outlive factory return.
Finite nested Maps retain child Maps, conditional `has` / `set` / `get`
flows have a path-sensitive presence proof, and property-free object keys
carry owning identity. Typed host publication, general object/host identities
and component-instance values remain open. Existing pointers to frame-local
capture cells must not be reused for that lifetime.

## Validation

The measurements in this section predate the 2026-09-07 iterator correction.
The six affected execution fixtures and positive lowering cases now materialize
arrays explicitly without changing functions or expected observations. The
devbox build and all **162/162 lit cases** pass for correction `c7a849c`.
The integrated full generated-program gate now passes **474/474 CTests**,
including **163/163 lit cases**; see [HANDOFF.md](HANDOFF.md) for its measured
baseline and corpus counts.

The original confined-Map checkpoint `8286564` passed **283/283** devbox tests,
including **94** lit tests. The validation below records that checkpoint;
the call/return extension is recorded separately after it.

`native-map-representation.mlir` checks the associative and ordered choices,
the string alias/factory, NaN and signed-zero equality, key identity, retained
child ownership, insertion order after update/delete/reinsert, independent
snapshot copies and deforested string-key value projections. Eight expected
numeric observations run through GCC and Clang with explicit and deduced
declarations; Clang ASan/UBSan checks the owning cases. A missing ordered-runtime
contract refuses projection fusion instead of trusting helper names.

`native-map-fixture.js` covers aliasing, all three key carriers, NaN and signed
zero, insertion order, deletion and reinsertion, snapshot independence, empty
containers, repeated allocations in loops, and argument evaluation through
lifted shared cells. Its numeric observations are compared with the independent
interpreter by the standard native pipeline, with no-VM symbols, GCC/Clang
warning checks, deduced-type checks and an off-by-one negative control.

`native-maps.mlir` checks source-level refusals at the intended function.
`native-map-proof.mlir` checks wrong receivers, `new.target`, unknown invocation
and untrusted proof annotations directly in CTJS.

Removing the Map-binding, `new.target` and exact-receiver guards individually,
rebuilding each mutant and checking its source hash made the intended refusal
pins fail. Binding and target mutants admitted their witness functions; the
receiver mutant reached the later carrier refusal. The original guards were
restored and rebuilt before validation. These experiments check the specific
proof boundaries, not just whether some unrelated test fails.

The Map fixture admits **22/22** functions, resolves **19** globals and records
**23** resolver direct calls plus **2** native-lift rewrites. All **21** numeric
observations also agree under Clang ASan/UBSan, with leak detection and
halt-on-error enabled. A separate claim floor of 23 fails as required.

| Corpus | Native functions | Resolver direct calls | Native-lift rewrites |
|---|---:|---:|---:|
| Bootstrap | 19/574 | 21 | 208 |
| p5 | 39/4754 | 41 | 328 |
| Phaser | 43/7725 | 48 | 283 |

These counts are unchanged from the owning-string checkpoint `46facdc`;
all three corpora still resolve zero globals. This confined Map slice adds
**zero** admitted functions to the complete Bootstrap bundle.

Regenerated boxed Bootstrap C++ remains **10,976,150 bytes**, SHA-256
`8dfd8e45a6a69a032c6f7b325573dc584f130989f81e49e6805e7c5faa71a6ba`.
The importer and boxed lowering are unchanged.

### Closed calls, returns and captures

The closed-call checkpoint's complete devbox gate passed **290/290**, including
**96/96** lit tests;
the pinned formatting gate and `git diff --check` pass. A 30-function claim
floor fails against the measured 29. Bootstrap remains **19/574**, p5
**39/4754** and Phaser **43/7725**, with the same direct/lift counts above.
The real UMD/Data probes still refuse all 6/6/7 functions, and regenerated
boxed Bootstrap retains the byte count and SHA-256 above.

`native-map-flow-fixture.js` admits **29/29** functions, resolves **22** globals,
and records **44** resolver direct calls plus **8** native-lift rewrites.
Its **11** numeric observations cover factory-frame lifetime, independent
instances, forwarding and `set` aliases, local Data-style method tables,
nested captures, distinct formal schemas, recursive calls, boolean keys,
unused owning arguments, empty returns and retention across 100 allocations.
All agree with the interpreter, including under Clang ASan/UBSan with leak
detection. The normal pipeline also checks standalone GCC/Clang compilation,
no-VM symbols, deduced types and an off-by-one negative control.

`native-map-flow.mlir` pins mixed input/schema, optional return, phi, returned
method table, mutable binding and object-store refusals. The structural
`native-map-flow-proof.mlir` additionally checks open callees, missing inputs,
mixed return producers and forged schema annotations. Map carrier definitions
now precede function prototypes, and unused owning parameters are explicitly
discarded without removing argument evaluation. The deduced-output checker
recognizes only the three supported Map carrier spellings.

Build and run the complete gate from the repository root:

```sh
flock /tmp/ctbrowser-devbox-build.lock ./tools/remote-build.sh
tools/format.sh --check
```

Build before running CTest: native modules and deduced variants are generated
build outputs. Census JSON files live under `build/ctcompile/test/` on the
devbox.
