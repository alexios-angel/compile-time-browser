# Symbol source integration, 2026-09-21 UTC

Continued clean `a11c4878`, following the previous Symbol handoff. There was no
interrupted dirty work. Two parallel read-only audits identified the existing
DOM proof and test harness; subsequent implementation requests hit service rate
limits before edits. Root completed the implementation and review.

`083349ac` admits direct constant well-known Symbol reads in a fingerprinted
`ctbrowser-dom-v1` entry with `initial_intrinsics: ["Symbol"]`. The existing
complete source census rejects replacement, mutation, escape and unsupported
uses. Catalog lookup uses the shared public Core names and a bounded StringSet
lookup. Facts are published only after the complete proof succeeds; input
annotations do not authorize lowering.

Inference introduces `!ctnative.symbol` from that live query. The carrier is
`ctnative::js_symbol_t`; emitted initializers read `ctnative::Symbol.hasInstance`
and the other well-known properties. Strict/loose identity equality, Boolean
conditions, `!` and `typeof` preserve Symbol semantics. Local SSA aliases keep
the identity. Generated code calls the existing public DOM helpers and links
DOM/Core without Script. No runtime, browser or VM implementation changed.

## Focused validation

All builds and executable checks ran on the devbox under the build lock:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-host-contract ctcompile-test-native-reference </dev/null
```

The first 113-step build stopped at the new EmitC initializer: an LLVM Twine
needed `.str()` before constructing an opaque attribute. After that correction,
the remaining 37-step build passed. The exact CTest selection
`^(ctcompile_host_contract|ctcompile_native_runtime)$` passed **2/2 in 0.58s**.

Four distinct selected lit cases passed:

- `CTNative/Browser/native-dom-symbols.test`
- `CTNative/Browser/native-dom-prototype-query.test`
- `CTNative/Lowering/Scalars/symbol-boundary.test`
- `CTNative/Lowering/Admission/refusal-default-arm.mlir`

The first selection passed **3/4 in 84.89s**. The new fixture failed because
HTML attribute writes lowercase names but its raw C++ atom lookups used mixed
case. The fixture now uses lowercase attribute names, retaining the original
Symbol property spellings. Its focused rerun passed **1/1 in 13.74s**.

The new source checks all 15 well-known keys, local copies, strict/loose
same/different identity, negation, truthiness and `typeof`. A recording receiver
runs the identical source under Node and the VM, checking 21 attribute effects
and two return paths. Generated native code checks the same expected values and
write counts against real DOM objects over 16 document lifetimes in each of
**eight executions**: GCC/Clang × explicit/deduced output × optimization off/on.
Native text and linked symbols are audited for Script/AOT leakage.

**38 refusals** cover missing intrinsic contracts, replacement, alias mutation,
constructor escape, dynamic/unknown reads, fresh/registry construction, coercion,
symbol-keyed properties, mixed equality, `instanceof`, Symbol joins/loops/returns,
zero/tiny budgets, changed-source fingerprints and freshly fingerprinted forged
proof metadata. A compiled native identity mutation fails the expected assertion.
Existing no-contract Symbol and generic `instanceof` refusals still pass.

All **17 final code/test SHA-256 hashes** match the devbox. All 14 changed C++
files pass the pinned formatter; the new Python passes Black and syntax parsing.
Whitespace checks pass. Required `tools/format.sh --check` retains **16 known
diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h` (4), and
`Symbolic/Facts.cpp` (8). Its C++ failure skips broader Python/web stages.
An initial `python3 -m black` invocation lacked that module; the installed
`black` executable completed the scoped format/check.

Skipped: full CTest/lit, broad corpus/native matrices, WPT/test262, separate
sanitizers, full wtfjs and full Bootstrap. No local build or push occurred.
Logs: `/tmp/ctcompile-symbol-source-{build,gate,rerun,format,hashes}.log`.
Hash manifest: `/tmp/ctcompile-symbol-source-sha256.txt`.

## Next boundary

Resume the unchanged `join`, `loop` and `symbol-return` source controls in
`Browser/native_dom_symbols.py`. Structured state and returns need a typed
transport proof that does not manufacture an absent/default Symbol identity.
`js_symbol_t` intentionally has no default constructor. Mixed alternatives and
null/undefined remain distinct.

General Symbol-only scripts require a suitable intrinsic contract: DOM entries
currently require element inputs, while closed-source providers require an owned
root. Fresh Symbols, primitive methods, registry access and symbol-keyed fields
need their own proofs. `Symbol.hasInstance` remains a key; custom hook lookup,
effects, result conversion, exceptions and ordinary prototype matching are still
prerequisites for the planned `instanceof` wrapper. The previously measured VM
custom-hook gap is unchanged. String ordering, sibling captures, document views,
full Bootstrap and the application driver remain unfinished.
