# Owning native strings

The native lowering maps a proved `!ctnative.str<utf8>` to `std::string`.
It supports literals, string/string concatenation and equality, truthiness,
parameters, returns, structured control flow, and lifted string captures.
Shared mutable captures refer to a frame-local owning string through a
pointer; the existing proof still forbids those captures from outliving
their frame.

This is a prerequisite for Bootstrap's placement selection. The real
`isRTL` helper also reads `document.documentElement.dir`; this change does
not provide that host binding. Bootstrap admission increases from **4/574**
at the callback checkpoint `c2d5521` to **19/574**. Its complete factory and
native startup remain refused.

## Representation and limits

Literals use readable ASCII text and explicit byte lengths, for example
`std::string("price", 5)`. The shared C++ literal formatter escapes control and
non-ASCII bytes with three-digit octal escapes, preserving embedded NUL and
lone surrogates without consuming following digits. Quotes and backslashes use
raw strings when the contents permit it. See [literal printing](native-literals.md).
Concatenation and equality follow the current interpreter's UTF-8/WTF-8 byte
semantics. In particular, concatenating
separately encoded surrogate halves does not normalize them into the bytes
of a single supplementary character. `split41` records this interpreter
behavior; it is different from a UTF-16 JavaScript engine's answer.

Optional strings now have a tagged owning carrier for the bounded consumers
described in [native-string-snapshots.md](native-string-snapshots.md).
The lowering still refuses mixed string/number coercion,
string ordering, string property access, and string globals or object fields.
The existing numeric global-printing convention is unchanged. The fixture
observes intermediate string results through numeric equality/truthiness
checks so the native comparison harness can check them.

A shared string cell's hoisted non-string initializer may be skipped only
when the existing assigned-before-read proof establishes that it is
unobservable. A binding with mixed string and numeric stores is inferred as
a variant and remains refused.

## Corrections made during review

- A property key can also be string data. Lower every constant and let the
  final sweep remove unused keys. Marking every field key for erasure left a
  live `ctjs.constant` in an accepted EmitC function.
- Emit string truthiness as comparison with an empty string. The dead-store
  pass can remove an unused comparison; it does not treat an arbitrary
  opaque `.empty()` call as pure.
- Specialize dead `ub.poison` uses by the destination's carrier. CFG
  structuring may use one poison in both numeric and string slots. String
  uses receive an empty owning string, while numeric uses retain NaN. A
  while-loop backedge uses the before-region argument type, which can differ
  from the result at the same index.

`native-string-control-flow.mlir` separately checks mixed poison uses,
for/while initializers and backedges, one SSA string used as both a field key
and data, and discarded truthiness. It verifies the raw native IR before
canonicalization and compiles the generated C++ under warning-as-error flags.
The generated string fixture compares 18 numeric observations with the
interpreter. Its mutable capture starts beyond small-string storage and
checks that a returned snapshot survives subsequent heap-backed mutation.

The refusal tests pin the actual callee's diagnostic for mixed coercion,
equality, ordering and string/number variants. Optional-string tests now pin
their owning carrier and the explicit widening at control-flow edges. The shared
variant test also requires the lifted pointer parameter, so a failure to lift
cannot stand in for the carrier refusal.

## Declaration-ordering limitation

The final hoisted declaration's closure can remain in bytecode register 0.
A following top-level ternary forwards the dead register through a CFG
branch, so the global resolver sees extra closure uses and cannot close the
function. SCF cleanup removes those uses after the resolver has run. This is
a general declaration-ordering
limitation; changing which function is last can hide it.

The current fixture ends with a parameterless function. Its string parameter
and capture cases remain present. This ordering avoids that particular
parameter refusal but is not a resolver fix. A second resolver run after SCF
cleanup is an experiment for a separate change, requiring fresh corpus and
boxed-output measurements.

The focused parameter and refusal tests use straight-line discarded calls.
Top-level result-checking ternaries originally triggered this resolver
limitation before those tests reached their intended string rules.

## Measurements, 2026-09-05

The full devbox build and suite pass **272/272**, including all **92** lit
tests, with corrected refusal fixtures and updated admission floors. The
string pipeline passes differential execution, the no-VM symbol check,
GCC/Clang compilation under `-Werror -Wconversion`, deduced-type checks and
the comparison gate's deliberate off-by-one mutation. The pinned formatting
gate and `git diff --check` also pass.

`tools/check/native-claims.py` measures:

| Corpus | Callback checkpoint | Owning strings | Resolver direct calls | Native-lift rewrites |
|---|---:|---:|---:|---:|
| Bootstrap | 4/574 | 19/574 | 21 | 208 |
| p5 | 37/4754 | 39/4754 | 41 | 328 |
| Phaser | 43/7725 | 43/7725 | 48 | 283 |

All three corpora still resolve zero globals. String support adds 15 admitted
Bootstrap functions and two p5 functions; it does not add any named calls or
make either complete library native. The string fixture admits **17/17**
functions, resolves 12 globals and records 28 resolver direct calls plus 11
native-lift rewrites. The admission floors are updated to these measurements.
Separate overstatement checks fail at floors 20, 40 and 18 for Bootstrap,
p5 and the string fixture, respectively, naming the measured counts.

The generated string fixture also compiles with Clang's ASan/UBSan at `-O1`.
All 18 observations match the interpreter with leak detection and
halt-on-error enabled, and no sanitizer diagnostics are emitted.

Regenerated boxed Bootstrap C++ remains **10,976,150 bytes**, SHA-256
`8dfd8e45a6a69a032c6f7b325573dc584f130989f81e49e6805e7c5faa71a6ba`.
The importer and boxed lowering are unchanged.

Run the complete gate from the repository root:

```sh
bash ../infra/azure-build-server/server.sh start
flock /tmp/ctbrowser-devbox-build.lock ./tools/remote-build.sh
```

Build before reading any CTest results, because the native pipeline's
generated fixtures are build outputs. Corpus reports are written under
`build/ctcompile/test/native-claims-{bootstrap,p5,phaser,strings}.json` on the
devbox.
