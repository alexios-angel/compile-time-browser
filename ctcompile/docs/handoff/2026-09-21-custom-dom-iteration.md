# Confined custom DOM iteration, 2026-09-21

Continued clean `6e8cb697` and the custom-iterator boundary recorded in
`2026-09-21-nested-element-iteration.md`. No uncommitted predecessor work or
unmerged `codex-wip-20260907` remained. Two agents independently prepared the
source/native fixture and raw IR checks/review; root implemented and integrated
the normalization and ran focused devbox validation.

**e52bec52** lets one root-local fresh object supply ordinary self-iteration through
`[Symbol.iterator]() { return this; }`, `next` and optional `return` methods.
The identity hook must be closed, non-arrow and effect-free. The other methods
cannot observe their implicit receivers, but may use proved immutable captures.
`next` returns a fresh record with exactly own `done` and `value` fields;
`return`, when present, returns an empty fresh object. The measured fixture uses
captured element attributes as state, yields that element once, and records
the exact order of next, body and close writes.

The manifest promises original Object, Symbol and the three importer helper
identities. Unique unconditional callable slots, hook identity, method/result
shape, protocol ordering and record confinement are checked before rewriting.
Item uses require a fresh not-done continuation; done has truth-only observers.
Exhaustion must stop the loop before another next call and suppress return.
A proved effect-only break calls return once. Explicit source body returns,
throws and handlers remain refused; the current VM importer does not close
custom iterators for body return/throw.

Normalization runs on a private candidate before helper expansion. It selects
the custom protocol before expanding source completion, substitutes ordinary
method calls and transports done state through structured regions. Opaque record
aliases become poison and must disappear under complete continuation proof.
Empty result-free exit dispatches can be removed. Unused If result positions,
and unused While results whose after arguments are also unused, are pruned
without erasing their producers or effects. Only dead completion constants and
index casts are then removed. Budget is charged before tuple mutation.

Existing helper expansion proves immutable callable holders and captures.
Fresh own-field projection now also handles nested allocations and runs after
helper expansion, preserving every field producer for complete DOM proof.
The final entry is reproved for types, effects, ownership and lifetimes before
publication. Generated output uses ordinary scalar C++ loops and public DOM
calls, with no VM iterator, boxed result record, new runtime type or Script
dependency. Browser/shared implementation files did not change.

## Focused validation

- Explicit devbox builds used `tools/remote-build.sh ctjs-opt ctjs-translate
  ctcompile-test-host-contract`, serialized under the build lock. Final exact
  `ctcompile_host_contract` passed **1/1, 0.66 s; 0.67 s total**, using
  `ctest -R '^ctcompile_host_contract$' --output-on-failure --no-tests=error`.
- Independent raw IR tests cover normalization, helper expansion and complete
  DOM proof with and without return hooks under both providers. Ten controls
  reject early close, exhausted-item observations, stale done reads, observable
  uncoerced done values, skipped/malformed/lexical identity methods, missing
  result fields, failure to stop and escaping records. Stale fingerprints and
  sampled early/middle/last incomplete budgets expose no DOM evidence.
- Final custom lit filter
  `^ctcompile :: CTNative/Browser/native-dom-custom-iteration.test$` passed
  **1/1, 49.12 s**, excluding 405 other discovered tests. It completed **32 native
  executions, 94 refusals and 12 Node/VM source-double observations**. Native
  clients use GCC/Clang, both printing modes, both optimization settings and
  borrowed/owned providers. They check write order, exhaustion, effect-only
  break, repeated invocation, detached/foreign/invalid elements and session
  ownership. Generated text and linked binaries exclude Script; clients link
  DOM/Core only. Node and VM execute the exact function bodies with recording
  receivers; these are source doubles, not a claim of full browser differential
  execution.
- Affected nested-iteration and dataset cases passed in the preceding filter
  `^ctcompile :: CTNative/Browser/native-dom-(custom-iteration|nested-iteration|dataset).test$`.
  That run was **two passed, one failed, 141.19 s**: custom counted break exposed
  the completion limit below. Nested traversal completed **48 native executions,
  two previous-source lowering checks and 94 refusals**. Dataset completed
  **28 sources, 112 Node/VM observations, eight GCC/Clang binaries, its lifetime
  sanitizer and 432 refusals**. Later changes only affect the custom-protocol
  path; those passing regressions were not replayed.
- Development gates caught a detached-region builder context crash, VM string
  percent-encoding in the fixture, and a client assuming Style/document arguments
  for an attribute-only entry. These were corrected. Effect-only break required
  removal of empty exit dispatch and dead integer completion tuple positions.
  The original counted-break source remains an explicit refusal witness.
- All ten final code/test SHA-256 hashes match the devbox. Scoped pinned C++
  formatting, Python Black/AST checks and `git diff --check` pass. Required
  `tools/format.sh --check` still reports 16 untouched diagnostics in
  `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and `Facts.cpp`.
- Initial availability checking inspected 72 Linux process identities and 356
  Windows CIM Name/ExecutablePath/CommandLine records, with no actual Claude
  executable, Node CLI, live loop or errors. The stopped result was journaled;
  broader browser authorization was not needed.
- Full CTest/compiler lit, broad corpus/matrix, WPT, test262, Windows, broad
  sanitizer replays, local builds and push were skipped.

## Next boundary

The preserved `counted-break-exit` source in the new fixture returns a changed
counter across a conditional break. The importer carries inactive alternatives
and an exit selector through the While results; current completion proof cannot
resolve those live alternatives. Prove that continuation before removing this
refusal. Effect-only breaks do not establish general break-state support.

Mutable iterator cells/receiver fields, separate iterator factories, nested
custom opens, generators and body abrupt-close behavior also remain unproved.
Literal C++ range-for printing, unguarded Bootstrap defaults, broader Array
behavior and the application driver remain unfinished, along with the rest of
the native plan. Full Bootstrap is not admitted.
