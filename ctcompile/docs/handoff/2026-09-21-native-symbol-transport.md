# Symbol transport and ordinary class tests, 2026-09-21 UTC

Continued clean `2e505136`, resuming the retained Symbol `join`, `loop` and
`symbol-return` witnesses from the preceding handoff. Parallel agents prepared
the intrinsic provider and ordinary class proof; root reconciled their changes,
ran the focused gates and committed each concern separately. Linux and Windows
process checks found no Claude executable, CLI or loop (67 and 353 processes
inspected); the result was journaled. No browser or VM implementation changed.

## Landed

- `29039575` admits Symbol state through branches, loops and returns. The three
  original source witnesses now execute unchanged. Inference and emission retain
  the distinct Symbol type and identity; mixed Symbol/primitive joins refuse.
- `0c2a6657` follows the user's direction to suppress GCC 13's false positive
  instead of complicating the representation. Final structured storage uses
  `std::optional<ctnative::js_symbol_t>`; reads and returns copy `js_symbol_t`.
  Empty temporary storage is not a JavaScript absence or fabricated identity.
  The original `js_symbol_t` variant representation remains unchanged.
- `4400f0c4` adds `ctbrowser-intrinsics-v1` for named zero-argument primitive/Symbol
  exports. Its exact schema is `version`, `provider`, `module_sha256`, `entry`,
  and `initial_intrinsics: ["Symbol"]`. It permits an inert declaration wrapper,
  reuses the bounded source proof, charges cloning before allocation and reproves
  the private candidate before publication. Helpers, top-level effects, extra
  intrinsics and DOM/root/realm fields refuse. Generated programs need no DOM
  input and link Core without Script.
- `8091bb51` proves ordinary class `instanceof` before structural shape lowering.
  Exact same-block constructions and completed class setups supply nominal
  constructor identity and proved heritage. An explicit standard `Function`
  contract includes the original inherited `Symbol.hasInstance` method. The
  complete source census must exclude mutation, escape and reentry before the
  predicate folds; constructors and operand effects remain. Unrelated equal
  shapes do not match. Hooks, replacement objects, unknown constructor origins
  and cross-block constructions still refuse.

The warning is `-Wmaybe-uninitialized` on the inactive String alternative of a
well-known Symbol under GCC 13 optimization. `Runtime/Symbol.hpp`, included by
`ctnative.hpp`, pushes/ignores/pops that diagnostic around `js_symbol_t` only,
guarded for GCC 13 and excluding Clang. A genuine uninitialized local after the
header remains a compile error. No compiler-wide suppression, enum transport,
heap indirection or custom variant implementation was added. Remove the scoped
workaround when GCC 13 support ends.

## Focused validation

All builds and executable checks ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`. Explicit build targets were:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract \
  ctcompile-test-native-runtime ctcompile-test-native-reference </dev/null
```

The subsequent class build selected the same targets except native-runtime.
The exact CTest selection
`^(ctcompile_host_contract|ctcompile_native_runtime)$` passed **2/2 in 0.60s**.
After the class corrections, `^ctcompile_host_contract$` passed again
**1/1 in 0.56s**. These are two distinct CTests, not a full-suite pass.

Five distinct lit cases passed across the focused runs:

- `CTNative/Browser/native-dom-symbols.test`
- `CTNative/Lowering/Scalars/symbol-boundary.test`
- `CTNative/Exports/native-intrinsic-symbols.test`
- `CTNative/Lowering/Objects/class-entry.mlir`
- `CTNative/Lowering/Objects/class-instanceof.test`

The three Symbol cases passed together **3/3 in 45.23s**. The DOM fixture checks
the original 21 attribute observations and two return paths plus state transport,
zero-iteration loops, saved copies and repeated calls: **28 native executions,
38 refusals and one identity mutation**. The intrinsic export fixture checks
typed Symbol and String returns against Node/VM in **16 native executions**, with
**56 refusals**. Native source and linked-symbol checks exclude Script; the
intrinsic exports also exclude DOM dependencies. The API fixture retains its
25 Node/VM observations, GCC/Clang and cross-translation-unit checks, and adds
the real caller-warning compile-fail control.

The class fixture checks **six Node/VM cases, 48 native executions and 26
refusals**: same class, unrelated equal shapes, an unconstructed RHS class,
aliases, retained constructor effects and inheritance. Existing `class-entry.mlir`
passed with the final C++ changes. The new fixture's final run passed
**1/1 in 30.76s** after a fixture-only correction.

Earlier class gates exposed a boxed `ctjs.from_bool` result and an unused
RHS-only constructor left after folding; both were corrected using existing
rewrite/cleanup paths. The last fixture correction counts explicit source `new`
operations, because existing super normalization removes unreachable importer
Error constructions. All explicit constructor effects remain checked. Initial
transport attempts still triggered GCC's warning; the scoped suppression and
ordinary optional storage replace those attempts.

The generated intrinsic-export Symbol loop also passed GCC ASan/UBSan, printing
`true` without sanitizer diagnostics:

```sh
g++ -std=c++23 -O1 -g -Wall -Wextra -Werror -Wconversion -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ictcompile/include -Ictbrowser/include \
  build/ctcompile/test/CTNative/Exports/Output/native-intrinsic-symbols.test.tmp/state-False.explicit.cpp \
  -o /tmp/ctnative-symbol-state-sanitized
/tmp/ctnative-symbol-state-sanitized
```

All **27 final code/test hashes** match the devbox. All **20 changed C++ files**
pass scoped formatting; the three changed Python fixtures pass Black. Whitespace
checks pass. Required `tools/format.sh --check` retains **16 existing diagnostics**
in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h` (2),
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` (4), and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp` (8). That C++ failure prevents the
script's broader Python/web stages from running. The final HostContract header
clarification is comment-only and was scoped-formatted and hash-checked after
the executable gates.

Skipped: full CTest/lit, broad corpus/native matrix runs, WPT/test262, full wtfjs,
full Bootstrap and application-driver replay. No local C++ build or push occurred.
Logs: `/tmp/ctcompile-symbol-suppression-gate.log`,
`/tmp/ctcompile-instanceof-gate{,2,3,4}.log`, and
`/tmp/ctcompile-instanceof-format.log`.
Hash manifest: `/tmp/ctcompile-symbol-final-hashes.txt`.

## Next boundary

Prove fresh Symbol construction and primitive description/`toString`/`valueOf`
operations through the intrinsic entry contract, then admit useful parameters
and helper calls. Registry operations and symbol-keyed fields remain separate.

Custom `Symbol.hasInstance` requires coherent oracle, effect and exception work.
The earlier measured VM witness skips a custom hook (false/zero calls versus
Node true/one call); it was not rerun here. Separate VM OrdinaryHasInstance from
operator hook dispatch to avoid recursion through the default method. Audit
`may_reenter=0`, CTJS `InstanceOfOp`'s `NoEscape`, and the AOT Boolean/no-exception
boundary together. Only then add the typed wrapper, preserving receiver, lookup,
ToBoolean, retained operands and thrown exceptions. `std::holds_alternative`
requires a separately proved constructor/prototype mapping; equal C++ shapes
alone are insufficient.

The native type plan records the user's reentrant current-document suggestion:
a global-looking accessor may borrow an invocation-owned document and Style
engine, with scoped save/restore across nesting and exceptions and independent
concurrent bindings. Thread-local storage alone does not isolate interleaved
asynchronous invocations. Unbound access and borrowed-view escape must refuse;
explicit document parameters remain available. Implement it when a proved
document-global use needs it, not for primitive-only exports.

String ordering, captured sibling functions, broader containers/document views,
full Bootstrap and the application driver remain unfinished.
