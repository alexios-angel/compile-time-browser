# Helper return payloads and negative-unit powers, 2026-09-24 UTC

Resumed clean `d4c29924` from the latest HANDOFF, Current native work, journal and
both areas' commits. `codex-wip-20260907` is already an ancestor. User continuations
interrupted iteration 152; parallel source/escape agents left frozen checkpoints,
and the parent completed their work after rate limits. The independent final
five-file review completed clean and includes the test-only naming correction.

## Landed

`163697b7` preserves a helper's independent return across an observed DOM call.
The helper still contains exactly one `hasAttribute` or `setAttribute` call, plus
inert preparation. Its return may be the call result, a proved formal, a constant
or a capture. Only the normal continuation's payload uses change. The original
leaf call still supplies `InvokeExit`'s result; failure payload, saved state and
both continuation effects remain intact. Inert suffix producers precede the
invocation so the helper return dominates its normal uses. Extra calls, coercions,
method-valued returns and other effects remain refused. Complete DOM reproof is
still required; this is a structural prerequisite, with **no new native source
admission**. Original getter `1a7fb166` and method `7acf503b` remain refused.

`b1724158` proves powers of exactly -1 with independently known finite integral
binary64 exponents. Existing Number provenance, depth 64, charged replay and
singleton refinement are reused. Exact parity handles negative integers, signed
zero, the 2^53 boundary and the maximum finite Number. Fractional/nonfinite
exponents and converted bits supply no new authority. No general power evaluator
or property-key proof is introduced.

Frozen source SHA-256
`83d507fea909522fed3a04331c7c5a82d44a07b506cb25a8941909189a2b8841`,
program `35386f26473b5ae2`, uses
`a[(((-1) ** (keys[i % 2] - 0.25)) + 1) | 0] = 0` with keys `[1.25,2.25]`.
Before: child Stored, lookup table Passed, **zero of four total sites confined**.
After: child and table **Confined**, **two of four total sites**, zero violations.
Node returns `[0,0,0]`; unlimited VM recording observes the child confined once.
All 179 earlier escape function bodies are unchanged; three new functions cover
the original, a saved child and mutation. The native Python sources are unchanged.

## Exact focused validation

- `ctcompile_exception_recovery`: **1/1 PASS**, 5.17 s.
- `ctcompile_host_contract`: **1/1 PASS**, 2.75 s; 7.93 s combined.
- Selected `native_dom_caught_node.py` sources `invocation-return` and
  `write-boolean-snapshot`, all refusal controls and both original outer sources:
  **32 native executions, 92 refusals, eight Node/VM observations PASS**.
  The temporary selector retains dictionary keys needed by refusal generation.
  **No whole caught-node lit pass is claimed.** Sixteen generated C++ files
  contain no Script/AOT namespace; harness binary symbol checks pass.
- `ctcompile_escape_analysis_arrays`: final **1/1 PASS**, 4.38 s, 4.39 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: **1/1 PASS**,
  0.20 s, 357 excluded, through the generated build-tree lit configuration.
- Frozen negative-unit source: before/after claims and unlimited VM recording,
  zero violations; Node result check. Source hash unchanged.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**.
  `git diff --check` passes. Five final code/test hashes match the devbox and
  the completed independent review.

Builds used locked `tools/remote-build.sh` with explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, `ctcompile-tool`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. The first arrays run failed only because the new
raw fixture reused the existing `%index` SSA name. Renaming it to `%powerIndex`
fixed the test; production was unchanged. No successful test gate was replayed.

Skipped: full CTest/compiler lit, complete caught-node/custom-iterator fixtures,
unaffected native replay, broad corpus/matrices, full Bootstrap, browser WPT/test262,
Windows and sanitizers. No browser/runtime/shared implementation, local C++ build,
push or history rewrite occurred. Linux process inspection read eleven executable
identities with 61 permission failures; Windows Get-CimInstance returned 341
records and no actual Claude match. Status remained uncertain; concurrent rules applied.

## Exact next boundary

Original observing outer getter `1a7fb166` and method `7acf503b` still stop in
`DOMIteratorClose.cpp` before general checked-invocation recovery. Protocol
discovery in `DOMCustomIteration.cpp` still requires a root-local open and unused,
state-free close suppression. Recovered original calls carry fifteen saved
registers each. Connect those open/next/close tuples to protocol discovery and
helper expansion with independent effect proofs. The real next method has two
writes and a result record; close writes then throws the anchor. The new inert
return substitution does not prove those effects or result construction.
Exhaustion, cleanup, saved returns and caught-node identity must survive; final
DOM proof still rejects general observed attribute tuples. Escaping nodes need
exception-lifetime owners.

The measured negative-unit escape witness is complete; no further witness was
measured. General powers, broader iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished.

Evidence: `../test-results/2026-09-24-helper-results-negative-unit-powers/`, with
sources/baselines, generated C++, exact logs, final hashes, independent review
and a verified `SHA256SUMS` manifest.
