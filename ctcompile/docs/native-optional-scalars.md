# Tagged optional scalars

Native lowering now gives `Opt<Bottom>`, `Opt<Num>` and `Opt<Bool>` a
`ctnative::nullable_scalar` carrier. Its tag distinguishes undefined, null,
number and boolean. The numeric payload retains negative zero and a present
NaN. Definite numeric and boolean values still use `double` and `bool`.
Closed boolean/number unions now share this carrier; their boundaries and
Bootstrap Data getter coverage are described in
[native-scalar-unions.md](native-scalar-unions.md).

Conversions at calls, returns, control-flow edges, fields and shared cells
preserve the tag. Arithmetic and ordering apply numeric conversion; equality,
truthiness and `typeof` inspect the tag. Numeric Map and dense-array misses
produce undefined, while a stored NaN remains a number. `Map.clear()` produces
undefined. Array indexing retains the interpreter's numeric-key rule: null,
undefined and boolean keys do not become numeric indices.

Globals use the same tagged storage so a read before the first store remains
undefined. The standalone output convention still requires definite numeric
stores. Its `global_number` check rejects an unwritten global instead of
mistaking it for a computed NaN. The differential harness's numeric mutation
preserves this storage contract.

`native-optional-scalars-fixture.js` has 29 functions and 52 numeric
observations. All 52 agree with the interpreter in a standalone native binary
with zero ctbrowser symbols. The census claims 29/29 functions, with 25 resolved
bindings, 119 direct calls before closure lifting and 11 additional lifted
calls. Its gates cover plain and deduced C++, both host compilers, mutation,
printing and the census floor.

The same 52 observations pass ASan/UBSan with leak detection. Additional
source probes verify null/undefined transitions through shared numeric and
boolean cells and optional captures retained after their factory returns;
all five observations match the interpreter without VM symbols.

Default corpus compile coverage is Bootstrap 19/574, p5 39/4754 and Phaser
45/7725. Phaser gains two constant-null helpers: NoAudioSound's `returnNull`
and `Phaser.Utils.NULL` (emitted as `fn_6463` and `fn_7571`). Both now return
tagged null. The Phaser census floor increases from 43 to 45; these two
helpers do not establish bundle execution.

The fixture includes a returned Data-like method table retained across
unrelated factory calls. Its `get` returns null for an absent key and preserves
0 and NaN for present keys. This tests the ownership and optional-scalar
boundary; the separately tracked Bootstrap publication probe still derives
its Data fragment verbatim from the vendor source.

Optional strings now have a separate owning carrier; see
[native-string-snapshots.md](native-string-snapshots.md). Number/string unions,
optional Map objects, nullable stored Map values and nullable array elements
remain refused. The literal Bootstrap
`has && get || null` expression still requires the separate boolean/number
union carrier. General component and host objects also remain outside this
subset.

The fixture exposed a printing-policy bug: C++ promotes `bool ^ bool` to int,
so changing the declaration to `auto` lost its required bool conversion.
`PrintDeduced` now retains explicit types for arithmetic and bitwise results
that would undergo this promotion. A focused bool-xor regression and the
fixture's type pins check the rule.
