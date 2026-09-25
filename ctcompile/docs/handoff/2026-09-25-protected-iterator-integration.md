# Protected iterator integration

Continued clean `ctcompile-v1` at `0f9ee8cd`, resuming the exact integration
boundary recorded in the protected-completion handoff and journals. The old
`codex-wip-20260907` branch was already an ancestor. No abandoned edits or other
branches were changed. Three agents investigated native source ancestry, test
adaptation and escape analysis; all hit rate limits before repository edits or
final signoff. The parent completed implementation, tests and bounded review.

## Landed behavior

`fca9a7e4` removes the explicit completion-normalization refusal in
`normalizeDOMCustomIteration` and runs the already-proved normalizer. It then
rediscovers next in the new body, checks its exact Call/InvokeExit relationship
and one normal block argument, and finds done only on that argument. The
invocation must be directly inside a root Try body. Existing direct-entry and
while-before handling remains; arbitrary nested invocations are not admitted.

The older suppression census examined only direct holder users and rejected
unobserved saved state. Observed invocations now use their existing complete
Try lifetime proof, which follows exact aliases and rejects later, caught or
published holder state. Completion normalization still rejects live poison.
No new inactive-value exemption was introduced.

The original structured getter and method pass custom normalization. Their
outputs verify with zero poison, three Invokes with 15 saved slots each, four
selected completion values, and one Try. Tests check ordered normal-only
done/value extraction, next failure without another close, the exhausted and
saved-return close guards, saved return 1, actual unwind payload/anchor arguments,
normalized throwing-close pre-call anchor values, and the original catch CellGet.
Existing hostile identity, ancestry, item, state and lifetime controls remain.

The previous intermediate body is no longer published. `DOMObservedState.hpp`
freezes the original getter's staged IR from iteration 10 state-transport
validation. The same nine independent completion variants still exercise live
poison in call/normal/unwind/catch, an unknown exit flag, a retained normal
observer, and zero/one-less-than-required budgets. Integrated assertions check
the newly published body separately. All eight existing raw DOMURI literals
and 39 Python JavaScript source strings are byte-identical to the starting tip.

## Focused validation

All devbox work held `/tmp/ctbrowser-devbox-build.lock`. Explicit build targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, and `ctcompile-test-native-reference`.

- Exact `ctcompile_exception_recovery`: final **1/1, 5.57 s** (5.58 s total).
- Exact `ctcompile_host_contract`: **1/1, 2.78 s**, on the final production code.
- Selected source differential: `invocation-return`, `write-boolean-snapshot`,
  `helper-next-record`, plus every existing refusal and original outer control.
  Existing provider, printing, optimization, GCC/Clang and Node/VM comparisons
  pass. This was not the whole fixture or a compiler lit replay.
- All **24 generated C++** files pass the forbidden-namespace checks; the source
  harness's binary checks pass. All **four** implementation/test hashes match
  the devbox.
- `tools/format.sh --check`: **1127 C++, 159 Python, 114 web files pass**.
  `git diff --check` passes. Parent bounded review is recorded; independent
  final review is not claimed.

Failed gates are preserved in evidence. They caught a Value/operation accessor,
a test ternary type mismatch, and parser/container/iterator API usage. Attempting
the old intermediate-state controls on already-normalized, modified output hit
existing exact-flag and source-visit refusals. Those attempts were discarded;
the final tests use the frozen original intermediate specimen, with no production
relaxation. The old direct-holder census and stale source diagnostic were updated
after the focused run exposed them.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrix
runs, raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS, sanitizers
and performance. No browser/shared implementation changed, no local C++ build,
no push or history rewrite occurred. Fresh availability checks inspected 11
Linux identities and 348 Windows Get-CimInstance records with no Claude matches;
60 Linux permission failures made status uncertain. Concurrent rules applied.

## Exact next work

Complete preparation of both original structured sources now stops at
`DOM helper capture cell has nonlocal or unordered uses`. In
`DOMSource::resolveCell`, root/cell reads may currently ascend through If,
While and Try, but Invoke has a separate call/normal/unwind and saved-state
contract. Trace the actual immutable captured anchor reads and saved aliases
through those continuations. Prove initialization and any permitted write before
every read, preserve the actual caught cell identity, and keep unknown or mutable
captures refused. Do not add a blanket region allowance. Then measure complete
preparation again; this internal normalization success is not native admission.

The parallel escape trace found that `structuredRetentionInputs` rejects nested
tracked allocations because `computeVerdicts` inventories root CFG sites. Existing
contents facts alone cannot supply the missing complete site/instance/identity
proof, particularly for repeated loop allocations. No escape implementation,
regression or precision measurement changed in this iteration.

Mutable exceptional state, Invoke loop-condition transport, nested allocation
origins, raw helper CFG normalization, escaping node owners, broader iterators,
unguarded Bootstrap defaults, the native application driver and full native
Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-25-protected-iterator-integration` relative to
the monorepo root: focused logs including failures, source selector, preservation
checks, final hashes, frozen intermediate IR, parent review and verified checksums.
