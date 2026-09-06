# JavaScript names in native C++

Native output preserves JavaScript parameter and local names. A unique parameter
can remain `catalog`. Multiple values belonging to a local use a numbered family:

```cpp
constexpr double score_1 = -1.0;
double score_3;
// Branches and expression intermediates use score_4, score_5, ...
double const score_2 = score_3;
return score_2;
```

The first surviving binding and its returned snapshot receive the first numbers;
the remaining materialized intermediates follow in emission order. Numbers may
skip an occupied name. Each function has one name pool for parameters, locals,
loop induction variables and anonymous temporaries, including when declarations
are hoisted to the top of a function. Prototypes and definitions use the same
allocation. Type deduction and its diagnostic pins retain those names.
The [const policy](native-const-bindings.md) independently qualifies immutable
bindings while leaving branch storage writable. A separate forward
[constant-expression analysis](native-constexpr-bindings.md) promotes eligible
scalar initializers to `constexpr` without changing their names.

The importer reads the bytecode compiler's optional local-name table using its
half-open register scope ranges. It stores spelling in `FusedLoc` dictionary
metadata (`ctnative.source_name`, or result-indexed `ctnative.source_names`).
Instruction identity and source positions remain direct location children.
Assignments keep the first source binding for an SSA value; an alias does not
rename a parameter. No copy operation or semantic attribute is introduced.

The C++ printer follows assignments, loads and join storage to recover the name
of a carried local, and follows expression operands backwards to name its
intermediates. Other named bindings, parameters and shared literal inputs keep
their own identity. Conflicting inferred names fall back to anonymous names.
This changes spelling only: it does not recover mutable JavaScript variables by
merging different SSA values or alter optimization and evaluation order.

Unsupported identifier bytes, C++ keywords and reserved underscore forms get
safe spellings. The allocator reserves emitted symbols, opaque code identifiers
and native header macros before choosing names, and avoids collisions between
source suffixes and generated suffixes. The reservation is shared across a
module; allocation remains local to each function.

Native lowering enables this policy with `ctnative.readable_names`. Unmarked
modules retain upstream spelling. `native-pipeline.cmake` preserves source
locations through its intermediate MLIR; manual pipelines must also retain them
with `--mlir-print-debuginfo`. Removing locations or compiling without bytecode
debug names leaves generic temporaries. Optimized-away aliases do not acquire
new declarations just to display their original names.

Implementation lives in `lib/CTJS/Import/Bytecode/SourceNames.*` and
`lib/Target/Cpp/Names/`. The importer regression checks scope reuse, aliases,
multiple results, missing tables and identical location-free IR. Native tests
compile and run heap initialization, joins, loops, closures and collision cases
under GCC and Clang, including explicit and deduced types.
