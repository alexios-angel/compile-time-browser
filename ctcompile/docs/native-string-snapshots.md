# Native string-key Map snapshots

Bootstrap 5.3.8's Data conflict diagnostic uses this expression at
`ctbrowser/vendor/bootstrap/bootstrap.bundle.js:15`:

```js
`Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(s.keys())[0]}.`
```

The native backend now lowers that expression for a proved standard Map with
string keys and a supported payload schema. This is a prerequisite for Data;
it does not admit console publication, UMD host environments, general component
instances, Data as a whole, or full Bootstrap.

## Carriers and absent reads

A confined string-key `keys()` snapshot owns a `std::vector<std::string>`. An
admitted `Array.from` call copies that vector. Both preserve insertion order,
replacement position and the reference interpreter's byte strings, including
embedded NUL and lone-surrogate encodings. Subsequent deletion, reinsertion or
clearing of the Map does not change either snapshot.

An indexed read has the existing `opt<str<utf8>>` inferred type and uses
`ctnative::nullable_string`: separate undefined, null and string tags with an
owning string payload. A missing element is undefined; a present empty string
retains its string tag. Null is retained when scalar control flow or a closed
direct call joins a snapshot read with null. No map-size or nonempty assumption
is needed to read index zero.

Indexing follows the current interpreter and numeric-snapshot convention:
numeric keys truncate toward zero, then bounds are checked before integer
conversion. Thus `-0.5` reads element zero; negative integers, NaN, infinity,
out-of-bounds keys and nonnumeric scalar tags produce undefined. This is the
repository's reference behavior, not ECMAScript property-key conversion.

Scalar consumers support truthiness, `!`, `typeof`, equality with another
string or absence, and concatenation with a definite owning string. Generic
`+` still requires a definite string on at least one side: two optional strings
could both contain undefined and require numeric addition. Numeric coercion,
mixed string/number equality, string property access and storing optional
strings as Map payloads remain refused. Numeric global observations are used
by the fixture; arbitrary string publication is outside this increment.

## Standard builtin and confinement proof

`NativeMap/SnapshotCopies.cpp` accepts `Array.from` only when all these facts
hold:

- `from` is read through a constant property of the standard `Array` global,
  and the call receiver is that exact loaded value.
- There is exactly one argument, produced by an already proved Map `keys()`
  or `values()` call. Mapping callbacks and general arrays are refused.
- The module does not reassign `Array`, inspect or mutate other Array
  properties, expose the builtin, read an unproved host/global value, or invoke
  an unknown call or constructor. The existing Map identity proof remains in
  force.
- Both snapshots stay confined to scalar index/length reads and the proved
  copy operation. Mutation, return, argument escape and structured array flow
  are refused.

The copy and erased builtin annotations are cleared and rederived with the
Map proof on every pass invocation. User-supplied markers cannot establish
builtin identity. Numeric snapshots also support the proved copy path. Boolean
and object snapshots remain refused. Deforestation's numeric carrier checks
exclude string snapshots; this work introduces no new allocation-elimination
assumption.

## Validation

The fixture admits **10/10 functions** and compares **24 numeric observations**.
The generated standalone C++ also passes ASan/UBSan with leak detection, preserving
all 24 observations. Snapshot emission explicitly loads the source lvalue before
copy assignment; the EmitC verifier checks that boundary. Type inference retains
an unresolved key type until callers provide evidence, avoiding a spurious
numeric alternative when a later caller supplies a string.

`native-string-snapshots-fixture.js` compares the exact diagnostic expression
with the independent interpreter, including an empty Map and a present empty
key. It covers null/undefined/string tags across direct calls and conditional
flow, numeric index edge cases, byte-string preservation, ordered independent
snapshots, and numeric `Array.from` copies. The standard native pipeline checks
both compilers' warnings, standalone/no-VM output and an off-by-one negative
control.

`native-string-snapshots.mlir` pins source refusals for builtin reassignment,
replacement, detachment, callback arity, non-Map input, prototype/host mutation,
snapshot mutation/escape, unsupported coercion, optional addition and boolean
keys. `native-string-snapshot-proof.mlir` pins exact receiver identity,
unknown-call invalidation and forged proof-marker clearing, including a second
lowering pass.
