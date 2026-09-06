# Native Maps toward Bootstrap Data

The native backend can lower standard `Map` instances with primitive or proved
identity-only object keys and numeric or finite acyclic Map values, including
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

Each allocation owns an insertion-ordered list of key/value pairs through
`std::shared_ptr<ctnative::number_map<K>>`. Primitive keys use `double`, `bool`
or owning `std::string`; object identities use an owning handle. Values use
`double`. Different allocation sites may have
different schemas. The result of `set` shares the original Map, so chained
calls and aliases observe the same mutations. Parameters and return values
copy owning handles; factory invocations still allocate independent Maps.
This is the numeric-leaf representation. Nested schemas use `map_storage<K,
V>` with an owning child handle as `V`; a separate containment proof rejects
all schema cycles. Every instance use is proved. Generated programs link
neither the interpreter nor its collector.

| Operation | Native behavior |
|---|---|
| `new Map()` | Empty owning allocation; constructor arguments are refused |
| `set(key, value)` | Replace in place or append; return the same Map identity |
| `get(key)` | Numeric value or absent, or an owning child Map when presence is proved |
| `has(key)` | Boolean membership test |
| `delete(key)` | Remove the entry and report whether it existed |
| `clear()` | Remove all entries and return absent |
| `size` | Number of current entries |
| `keys()` | Independent numeric or string array snapshot |
| `values()` | Independent numeric array snapshot |

Numeric lookup uses SameValueZero: NaN finds NaN, and positive and negative
zero are one key. Replacement preserves the original key and position;
deleting and reinserting moves the entry to the end. The current interpreter
retains the sign of the first inserted zero key, so native `keys()` does too.
This differs from ECMAScript Map's zero normalization.

The interpreter returns arrays from `keys()` and `values()`, rather than
ECMAScript iterators. Native snapshots therefore copy their elements and
survive subsequent mutation or clearing. Reads and `length` are supported;
snapshot mutation, escape and elements other than numbers or strings are refused.
Proved standard `Array.from` copies and optional string scalar reads are covered
by [native-string-snapshots.md](native-string-snapshots.md). Strings retain
the interpreter's UTF-8/WTF-8 byte semantics, including embedded NUL and lone
surrogates, as described in [native-strings.md](native-strings.md).

Missing `get` results use the existing `opt<number>` representation. Arithmetic
converts a missing value to NaN for numeric operations. Equality and `typeof`
retain the distinction between a missing value and a stored NaN through the
[tagged optional scalar carrier](native-optional-scalars.md). Stored values must be
definite numbers, including numeric NaN, or owning acyclic child Maps. Optional
values, mixed key types, general object keys and string values remain refused. See
the nested-Map document for its additional presence and containment proofs.

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

The original confined-Map checkpoint `8286564` passed **283/283** devbox tests,
including **94** lit tests. The validation below records that checkpoint;
the call/return extension is recorded separately after it.

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

The complete devbox gate passes **290/290**, including **96/96** lit tests;
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
